 /* Copyright 2026 NR7Y
 * https://github.com/briand
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

 // Hardware input helpers for CW keyer (port config, debounced reads, etc.)
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "misc.h"
#include "app/cwhardware.h"
#include "settings.h"
#include "audio.h"
#ifdef ENABLE_CODE_PRACTICE
#include "app/cpo.h"
#endif
#include "py32f071_ll_dma.h"
#include "py32f071_ll_gpio.h"
#include "py32f071_ll_bus.h"
#include "py32f071_ll_rcc.h"
#include "py32f071_ll_usart.h"
#include "py32f071_ll_adc.h"
#include "driver/gpio.h"
#include "driver/systick.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "app/uart.h"
#include "driver/millis.h"
#ifdef ENABLE_USB
#include "driver/vcp.h"
#endif
#include "external/printf/printf.h"

// Local debug toggle for CW hardware reads
#ifndef ENABLE_CW_HARDWARE_DEBUG
#define ENABLE_CW_HARDWARE_DEBUG 0
#endif

// Local state for last sampled paddles (edge detection)
static bool     s_last_dit   = false;
static bool     s_last_dah   = false;
static uint32_t s_dit_count  = 0;  // consecutive raw reads disagreeing with confirmed dit state
static uint32_t s_dah_count  = 0;  // consecutive raw reads disagreeing with confirmed dah state
static bool     s_last_is_dah = false;  // which paddle was pressed most recently (for Ultimatic mode)

// Read button ring input (SIDE1)
static void CW_ReadSideButton(bool *ring_out)
{
#if ENABLE_CW_HARDWARE_DEBUG
    char dbg_buf[80];
#endif

    // The keyboard matrix lives on GPIOB (columns on PB3..PB6, rows on PB12..PB15).
    // KEY_SIDE1 is in the "zero" column, row 0 => PB15.
    const uint32_t cols_mask = LL_GPIO_PIN_3 | LL_GPIO_PIN_4 | LL_GPIO_PIN_5 | LL_GPIO_PIN_6; // PB3..PB6
    const uint32_t side1_row  = LL_GPIO_PIN_15; // PB15

    // Drive columns high (same as keyboard scan initial state)
    LL_GPIO_SetOutputPin(GPIOB, cols_mask);

    bool ring = false;
    uint32_t reg = 0, reg2 = 0;
    uint32_t match_count = 0;

    // Debounce: take several samples with short delays and require consecutive matching reads
    for (unsigned int k = 0; k < 8; k++) {
        SYSTICK_DelayUs(10);
        reg2 = LL_GPIO_ReadInputPort(GPIOB) & side1_row;

        if (reg2 != reg) {
            match_count = 0;
            reg = reg2;
        } else {
            match_count++;
        }

        if (match_count >= 2) {
            break;
        }
    }

    if (match_count >= 2) {
        // Stable reading achieved - active low when pressed
        ring = !(reg);
    }

    static bool last_reported = false;
    if (ring != last_reported) {
#if ENABLE_CW_HARDWARE_DEBUG
        sprintf_(dbg_buf, "CW_ReadSideButton: stable=%u reg=%u match=%u ring=%u\r\n", (unsigned)(match_count>=2), (unsigned)reg, (unsigned)match_count, (unsigned)ring);
        UART_Send(dbg_buf, strlen(dbg_buf));
#endif
        last_reported = ring;
    }

    // Cleanup: leave columns high as keyboard does
    LL_GPIO_SetOutputPin(GPIOB, cols_mask);

    *ring_out = ring;
}


// Generic GPIO deglitch function - reads with de-noise using consecutive-sample algorithm.
// Returns true if pin is active (low), false if inactive (high).
static bool CW_ReadGpioDeglitched(GPIO_TypeDef *gpio_port, uint32_t pin_mask, bool heavy)
{
    bool result = false;
    uint16_t reg = 0, reg2;
    unsigned int i, k;
    uint32_t limit = heavy ? 500 : 100; // more samples for heavy de-noise
    uint32_t goal = heavy ? 300 : 60;  // need this many stable samples

    for (i = 0, k = 0, reg = 0; i < goal && k < limit; i++, k++) {
        SYSTICK_DelayUs(1);
        // Read using LL helper: returns non-zero if pin input is set
        reg2 = LL_GPIO_IsInputPinSet(gpio_port, pin_mask) ? pin_mask : 0;
        i *= (reg == reg2);  // Reset i if readings differ
        reg = reg2;
    }

    if (i >= goal) {
        // Stable reading achieved - active low
        result = !reg;
    }
#if ENABLE_CW_HARDWARE_DEBUG
    char dbg_buf[80];
    sprintf_(dbg_buf, "%s: s=%u r=%u m=%u r=%u\r\n", label, (unsigned)(i>=goal), (unsigned)reg, (unsigned)i, (unsigned)result);
    // UART_Send is a no-op here: USART1 (PA10) is disabled whenever
    // CW_ConfigurePortGround(true) is active (Port Handkey and friends), which
    // is exactly when this print is most useful. Route over USB CDC instead -
    // it's untouched by any port-ground mode and cheap when nothing's listening.
#ifdef ENABLE_USB
    VCP_SendStr(dbg_buf);
#else
    UART_Send(dbg_buf, strlen(dbg_buf));
#endif
#endif
    return result;
}

// ==================== RF-tolerant GPIO deglitch (USB paddle) ====================
// Oversamples the pin many times in a short window and decides by super-majority
// with a dead zone (hysteresis):
//   - steady closure -> nearly all samples active   -> GLITCH_CLOSED
//   - steady open     -> nearly all samples inactive -> GLITCH_OPEN
//   - RF oscillation  -> samples disagree (~50/50)    -> GLITCH_INDETERMINATE
// GLITCH_INDETERMINATE is what breaks a "stuck keyed" feedback loop: while the
// PA is splattering the line, the reader refuses to assert a confident state
// rather than latching a false one - CW_ReadUSBPaddleRaw holds its last
// confident per-pin state through the RF burst instead.

// Samples taken per read window. More = better averaging, more time.
#define GLITCH_SAMPLES          64u
// Nominal delay between samples, microseconds. Total window is roughly
// (GLITCH_SAMPLES-1) * GLITCH_SAMPLE_US. Confirmed via the debug mask that a
// real, brief tap can land entirely inside one window and only cover a
// fraction of it (e.g. 10/32 active, a clean contiguous run, correctly
// rejected as indeterminate because it's below GLITCH_ASSERT_COUNT even
// though it's a genuine closure) - shrinking the window makes a real tap of
// a given duration cover a larger fraction of it, rather than getting
// outvoted by open samples on either side from unlucky window alignment.
#define GLITCH_SAMPLE_US        1u
// Super-majority thresholds, in counts of "active" samples out of
// GLITCH_SAMPLES. The gap between them is the dead zone that rejects
// ambiguous / RF-corrupted windows. Widen the gap for more RF immunity;
// narrow it if legitimate closures get rejected as indeterminate.
#define GLITCH_ASSERT_COUNT     24u   // >=GLITCH_ASSERT_COUNT/GLITCH_SAMPLES (62.5%)  -> closed
#define GLITCH_DEASSERT_COUNT   8u    // <=GLITCH_DEASSERT_COUNT/GLITCH_SAMPLES (25%)  -> open
// Minimum unbroken run of same-value samples required, on top of the count
// thresholds above, before a window is trusted. Plain majority counting can't
// tell a genuine closure from RF toggling that happens to land on the right
// total: e.g. 1,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0 clears GLITCH_ASSERT_COUNT
// (10/16 active) but is really five separate runs bouncing every few us -
// exactly what a real, settled contact never does. Requiring a long
// contiguous run (not just a total) is what CW_ReadGpioDeglitched already
// does for the non-RF-tolerant inputs; this brings the same idea here
// alongside the majority/dead-zone logic instead of replacing it.
#define GLITCH_MIN_RUN          48u
// 1 if a closed key pulls the pin LOW (active-low); 0 if active-high.
#define GLITCH_ACTIVE_LOW       1u
// Dither the sample spacing so an evenly-clocked sampler can't alias onto a
// periodic RF-induced oscillation and read a false-steady level.
//#define GLITCH_DITHER           1u
// Peak-to-peak dither in microseconds (only if GLITCH_DITHER = 1).
#define GLITCH_DITHER_US        4u

typedef enum {
    GLITCH_OPEN          = 0,  // confident: key open
    GLITCH_CLOSED        = 1,  // confident: key closed
    GLITCH_INDETERMINATE = 2   // not confident (bounce / RF noise)
} glitch_state_t;

#if GLITCH_ACTIVE_LOW
#define GLITCH_IS_ACTIVE(raw)  ((raw) == 0u)
#else
#define GLITCH_IS_ACTIVE(raw)  ((raw) != 0u)
#endif

#if GLITCH_DITHER
// Cheap xorshift32 for spacing dither. Self-seeds; state is fine to share.
static uint32_t glitch_prng(void)
{
    static uint32_t s = 0x1234567u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
#endif

// Settle time after CW_ConfigureUsbPaddlePins() switches the pins to input,
// before the first sample is taken.
#define GLITCH_SETTLE_US 50u

// One RF-tolerant read of an already-configured input pin.
// Worst-case wall time ~= (GLITCH_SAMPLES - 1) * max_gap + sample overhead.
// With the defaults: 15 gaps * ~11 us ~= 165 us + a few us -> well under 200 us.
// out_active/out_mask (optional, may be NULL) receive the raw active-sample
// count and a bit-per-sample mask (bit 0 = first sample; only the low
// GLITCH_SAMPLES bits are meaningful, and only the low 32 samples are
// captured if GLITCH_SAMPLES > 32) - purely diagnostic, doesn't affect the
// result. The mask is what shows *where in the window* activity landed -
// e.g. all-low-then-high (0x0000FFFF-ish) points at a startup transient
// (pull-down -> pull-up charge time) rather than genuine RF/bounce, which
// would look scattered instead.
static glitch_state_t glitch_gpio_read(GPIO_TypeDef *gpio_port, uint32_t pin_mask, uint32_t *out_active, uint32_t *out_mask)
{
    uint32_t active = 0u;
    uint32_t mask = 0u;

    // Longest unbroken run seen so far, split by which side (active/inactive)
    // the run was on - a run only counts toward GLITCH_MIN_RUN for the side
    // it actually occurred on. prev_raw has no valid sample yet at i==0, so
    // the first sample always starts a fresh run of length 1 rather than
    // (wrongly) extending a run against an uninitialized value.
    uint32_t run = 0u;
    uint32_t prev_raw = 0u;
    uint32_t longest_active_run = 0u;
    uint32_t longest_inactive_run = 0u;

    for (uint32_t i = 0u; i < GLITCH_SAMPLES; i++) {
        uint32_t raw = LL_GPIO_IsInputPinSet(gpio_port, pin_mask) ? 1u : 0u;
        bool is_active = GLITCH_IS_ACTIVE(raw);
        if (is_active) {
            active++;
            if (i < 32u)
                mask |= (1u << i);
        }

        run = (i > 0u && raw == prev_raw) ? (run + 1u) : 1u;
        prev_raw = raw;

        if (is_active) {
            if (run > longest_active_run)   longest_active_run = run;
        } else {
            if (run > longest_inactive_run) longest_inactive_run = run;
        }

        // No delay after the last sample.
        if (i + 1u < GLITCH_SAMPLES) {
#if GLITCH_DITHER
            uint32_t half = GLITCH_DITHER_US / 2u;
            uint32_t jit  = glitch_prng() % (GLITCH_DITHER_US + 1u);
            uint32_t d    = (GLITCH_SAMPLE_US > half)
                            ? (GLITCH_SAMPLE_US - half + jit)
                            : (GLITCH_SAMPLE_US + jit);
            SYSTICK_DelayUs(d);
#else
            //SYSTICK_DelayUs(GLITCH_SAMPLE_US);
#endif
        }
    }
    if (out_active) *out_active = active;
    if (out_mask)   *out_mask   = mask;

    if (active >= GLITCH_ASSERT_COUNT   && longest_active_run   >= GLITCH_MIN_RUN) return GLITCH_CLOSED;
    if (active <= GLITCH_DEASSERT_COUNT && longest_inactive_run >= GLITCH_MIN_RUN) return GLITCH_OPEN;
    return GLITCH_INDETERMINATE;
}

// Read the PTT/tip with de-noise
static void CW_ReadPtt(bool *ptt_out)
{
    *ptt_out = CW_ReadGpioDeglitched(GPIOB, LL_GPIO_PIN_10, false);
}

// Temporary diagnostic - flip to 1, rebuild, and watch the log while testing.
// Remove this flag and everything below it in this section once things are
// settled; not meant to be a permanent debug flag.
#ifndef USB_PADDLE_DEBUG
#define USB_PADDLE_DEBUG 0
#endif

#if USB_PADDLE_DEBUG
// UART_Send() is blocking (~20ms+ for one of these lines at this baud rate).
// A previous version of this diagnostic drained one queued entry per
// CW_ReadUSBPaddleRaw() call, intending to keep printing off the call that
// detected the edge - but during a RAPID, sustained oscillation (edges on
// nearly every call) the queue never empties, so nearly every call ends up
// paying the ~20ms tax anyway. That's a real problem here specifically:
// glitch_gpio_read() leaves the pin pulled down between reads, so an
// artificially long ~20ms gap (from printing) gives it time to fully
// discharge that it would never get at the real, sub-millisecond polling
// rate - which can manufacture a slow charge-up-doesn't-finish-in-the-window
// artifact that wouldn't happen in normal (non-debug) operation at all.
//
// So: capture silently into a larger buffer with ZERO UART calls during the
// interesting window, and only dump everything in one burst once the buffer
// fills. The dump itself blocks for a while, but only *after* the behavior
// being observed has already happened and been faithfully recorded.
typedef struct {
    uint32_t t_ms;
    uint32_t tip_active, tip_mask;
    uint32_t ring_active, ring_mask;
    glitch_state_t tip_state, ring_state;
} usb_dbg_entry_t;

#define USB_DBG_BUF_LEN 64
static usb_dbg_entry_t s_dbg_buf[USB_DBG_BUF_LEN];
static uint8_t s_dbg_count = 0;

static void usb_dbg_dump_all(void)
{
    for (uint8_t i = 0; i < s_dbg_count; i++) {
        usb_dbg_entry_t *e = &s_dbg_buf[i];
        char buf[96];
        sprintf_(buf, "USB %u t=%u/%u(%u) tm=%u r=%u/%u(%u) rm=%u\r\n",
            (unsigned)e->t_ms, e->tip_active, GLITCH_SAMPLES, (unsigned)e->tip_state, e->tip_mask,
            e->ring_active, GLITCH_SAMPLES, (unsigned)e->ring_state, e->ring_mask);
        UART_Send(buf, strlen(buf));
    }
    s_dbg_count = 0;
}

// Mask is printed LSB-first == first sample first, so a leading run of 1s
// (active-low) followed by 0s reads left-to-right as "started low, went
// high" - a startup/charge transient signature, vs. scattered bits which
// would point at RF/bounce.
static uint32_t s_dbg_last_push_ms = 0;

static void usb_dbg_push(uint32_t tip_active, uint32_t tip_mask, glitch_state_t tip_state,
                          uint32_t ring_active, uint32_t ring_mask, glitch_state_t ring_state)
{
    if (s_dbg_count >= USB_DBG_BUF_LEN) {
        usb_dbg_dump_all(); // buffer full - flush now (only place this ever blocks)
    }
    s_dbg_buf[s_dbg_count++] = (usb_dbg_entry_t){
        .t_ms = millis(),
        .tip_active = tip_active, .tip_mask = tip_mask, .tip_state = tip_state,
        .ring_active = ring_active, .ring_mask = ring_mask, .ring_state = ring_state,
    };
    s_dbg_last_push_ms = millis();
}

// Also flush after a period with no new edges, so a short burst of activity
// (a couple of taps) shows up without waiting for 64 entries to accumulate.
// Only checked here (a cheap millis() compare, no UART) and only dumps once
// things have gone quiet - by definition nothing interesting is happening
// right now when this fires, so blocking for the dump can't corrupt an
// in-progress observation the way dumping mid-burst would.
static void usb_dbg_flush_if_idle(void)
{
    if (s_dbg_count > 0 && millis_since(s_dbg_last_push_ms) >= 300)
        usb_dbg_dump_all();
}
#endif

// Raw read of the USB paddle pins, independent of key-input mode flags.
// Pins are already configured as pulled-up inputs by CW_ConfigureUsbPaddlePins()
// and left alone - no per-read setup needed. Each pin is read tri-state
// (open/closed/indeterminate) via glitch_gpio_read() and held at its last
// confident state through an indeterminate (RF-noisy) window, so a burst of
// RF on the line can't latch a false key-down.
void CW_ReadUSBPaddleRaw(bool *tip_out, bool *ring_out)
{
    static bool s_last_tip  = false;
    static bool s_last_ring = false;

    uint32_t tip_active = 0, tip_mask = 0, ring_active = 0, ring_mask = 0;



    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_12, LL_GPIO_PULL_DOWN);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_12, LL_GPIO_MODE_OUTPUT);    
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_12); 
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_11, LL_GPIO_PULL_DOWN);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_11, LL_GPIO_MODE_OUTPUT);    
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_11); 
    SYSTICK_DelayUs(20);

    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_11, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_11, LL_GPIO_PULL_UP);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_12, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_12, LL_GPIO_PULL_UP);
    //SYSTICK_DelayUs(5);
    glitch_state_t ring = glitch_gpio_read(GPIOA, LL_GPIO_PIN_11, &ring_active, &ring_mask);
    if (ring != GLITCH_INDETERMINATE)
        s_last_ring = (ring == GLITCH_CLOSED);
    
    glitch_state_t tip = glitch_gpio_read(GPIOA, LL_GPIO_PIN_12, &tip_active, &tip_mask);
    if (tip != GLITCH_INDETERMINATE)
        s_last_tip = (tip == GLITCH_CLOSED);


    *tip_out  = s_last_tip;
    *ring_out = s_last_ring;

#if USB_PADDLE_DEBUG
    static glitch_state_t s_last_logged_tip = GLITCH_OPEN, s_last_logged_ring = GLITCH_OPEN;
    if (tip != s_last_logged_tip || ring != s_last_logged_ring) {
        s_last_logged_tip = tip;
        s_last_logged_ring = ring;
        usb_dbg_push(tip_active, tip_mask, tip, ring_active, ring_mask, ring);
    }
    usb_dbg_flush_if_idle();
#endif
}

// Read raw paddle inputs for a specific mode
// Returns true if mode is valid, false otherwise
bool CW_ReadKeysForMode(uint8_t mode, bool *dit_out, bool *dah_out)
{
    // Check if keyer is disabled (handkey modes)
    if (mode & CW_KEY_FLAG_NO_KEYER && !(mode & CW_KEY_FLAG_PORT_GROUND)) {
        return false;
    }

    // Read PTT (PC5) as tip - this is how the rework wires it
    bool hw_ring = false;
    bool hw_tip = false;
    CW_ReadPtt(&hw_tip);

    // SIDE1 is the second iambic paddle contact (ring - dah standard), 
    // independent of PTT (tip, dit standard). This mode isn't
    // flagged NO_KEYER, so it runs the full iambic keyer state machine and
    // needs two separate contacts.
    if (mode & CW_KEY_FLAG_SIDE1) {
        CW_ReadSideButton(&hw_ring);
    }

    // Read port ring input if enabled and OR with button ring. PORT_GROUND is
    // checked rather than PORT_RING so that Port Handkey mode (PORT_GROUND set,
    // no PORT_RING flag of its own) also reads the ring contact as an alternate
    // key input alongside tip (either pin active) - every mode that sets
    // PORT_RING also sets PORT_GROUND, so this covers both cases.
    // Short-circuit: if SD1 already resolved true, skip the expensive heavy deglitch —
    // the OR result is the same and we avoid ~500us of sampling overhead on every poll.
    if ((mode & CW_KEY_FLAG_PORT_GROUND) && !hw_ring) {
        // New hardware: port-ring is on PA13 (SWDIO when not used). Use deglitch helper on GPIOA bit 13.
        hw_ring = CW_ReadGpioDeglitched(GPIOA, LL_GPIO_PIN_13, false);
    }

    // USB port mode: PA11 acts as ring/dah (DM), PA12 acts as tip/dit (DP)
    if (mode & CW_KEY_FLAG_USB_PORT) {
        CW_ReadUSBPaddleRaw(&hw_tip, &hw_ring);
    }

    // Determine if keys are reversed
    bool reverse = (mode & CW_KEY_FLAG_REVERSED);

    // Map tip/ring to dit/dah based on reversed flag
    *dit_out = reverse ? hw_ring : hw_tip;
    *dah_out = reverse ? hw_tip : hw_ring;

    return true;
}

// Keypad lock with PTT lockout (SET LCK = PTT or ACTIONS+PTT) locks out CW key
// input entirely, the same as the PTT button: nothing is keyed, not even the
// local-only sidetone that break-in off would otherwise give. Operates on raw
// reads, so the keyer FSMs just see the key go up and finish the current
// element normally if the lock lands mid-character.
//
// A press while locked gets the same feedback as a locked PTT: double beep and
// the unlock hint. While unlocked, keying counts as activity for auto keypad
// lock - the keyer owns PTT and the paddles in CW mode, so none of it passes
// through ProcessKey(), and the auto lock would otherwise lock the keyer out
// in the middle of a QSO.
bool CW_KeyInputLockedOut(bool pressed)
{
    static bool s_locked_pressed;

    bool locked = false;
#ifdef ENABLE_FEAT_F4HWN
    locked = gEeprom.KEY_LOCK && (gSetting_set_lck & SET_LCK_PTT);
#endif
#ifdef ENABLE_CODE_PRACTICE
    locked = locked && !gCW_CpoActive;  // practice never transmits
#endif

    if (!locked) {
        s_locked_pressed = false;
        if (pressed && gEeprom.AUTO_KEYPAD_LOCK)
            gKeyLockCountdown = gEeprom.AUTO_KEYPAD_LOCK * 30;  // 15 seconds step
        return false;
    }

    if (pressed && !s_locked_pressed) {
        gBeepToPlay    = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        gKeypadLocked  = 4;  // 2 seconds
        gUpdateDisplay = true;
    }
    s_locked_pressed = pressed;
    return true;
}

// Read GPIO inputs based on configured mode
void CW_ReadKeys(CW_Input *in)
{
    bool n_dit = false;
    bool n_dah = false;

    // Read inputs using helper function
    if (!CW_ReadKeysForMode(gEeprom.CW_KEY_INPUT, &n_dit, &n_dah)) {
        // Handkey mode or invalid - no keyer input
        n_dit = false;
        n_dah = false;
    }

    if (CW_KeyInputLockedOut(n_dit || n_dah)) {
        n_dit = false;
        n_dah = false;
    }

    bool deb_dit = s_last_dit;
    bool deb_dah = s_last_dah;
    if (gEeprom.CW_KEY_INPUT & CW_KEY_FLAG_USB_PORT) {
        // USB port paddle already went through its own tri-state glitch
        // filter. Honor the single read as-is.
        deb_dit = n_dit;
        deb_dah = n_dah;
    } else {
        // Asymmetric debounce: a press must be confirmed by three consecutive
        // raw reads (glitch immunity on key-down), but a release takes effect
        // on the first raw-open read so the iambic FSM sees paddle-open
        // promptly. A symmetric release delay here starves the count-based
        // debounce in the iambic path (CW_ReadKeys is skipped while
        // s_pending_alternate is set) and makes mode B re-latch the still-high
        // level into extra trailing elements. The straight-key/bug modes that
        // want a release glitch-filter apply their own 40ms release debounce
        // in cwkeyer.c and do not depend on this. Any read that agrees with
        // the confirmed state resets the counter.
        if (n_dit == deb_dit)   s_dit_count = 0;                 // agrees: no change
        else if (!deb_dit) {                                     // rising: three-strike
            if (++s_dit_count >= 3) { deb_dit = true; s_dit_count = 0; }
        } else { deb_dit = false; s_dit_count = 0; }             // falling: immediate

        if (n_dah == deb_dah)   s_dah_count = 0;
        else if (!deb_dah) {
            if (++s_dah_count >= 3) { deb_dah = true; s_dah_count = 0; }
        } else { deb_dah = false; s_dah_count = 0; }
    }

    // Edges computed against previous debounced state.
    in->dit_rise = (!s_last_dit && deb_dit);
    in->dah_rise = (!s_last_dah && deb_dah);
    in->dit = deb_dit;
    in->dah = deb_dah;

    // Track which paddle was pressed most recently (a fresh press is exactly
    // a rising edge), for Ultimatic mode's "both held -> last one wins" rule.
    if (in->dit_rise) s_last_is_dah = false;
    else if (in->dah_rise) s_last_is_dah = true;
    in->last_is_dah = s_last_is_dah;

    s_last_dit = deb_dit;
    s_last_dah = deb_dah;
}

// Configure port ground pin (PA10) for tip/ring paddle input
// When enabled: PA10 becomes GPIO output low (acts as ground for paddle port)
// When disabled: restore UART1 RX functionality (call UART_Init to reconfigure)
void CW_ConfigurePortGround(bool enable)
{
    // Use LL drivers to reconfigure PA10 (USART1 RX on AF1) to GPIO output low
    if (enable) {
        // Disable USART1 so the pin can be used as GPIO
        LL_USART_Disable(USART1);

        // Disable RX DMA channel used by USART1 receive (configured in UART_Init)
        LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);

        // Ensure GPIOA clock is enabled then configure PA10 as push-pull output and drive low
        //LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
        LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_10, LL_GPIO_MODE_OUTPUT);
        LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_10, LL_GPIO_OUTPUT_PUSHPULL);
        LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_10, LL_GPIO_PULL_DOWN);
        LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_10); // drive low (ground)
    } else {
        // Restore UART configuration which will reassign PA10 to AF1 (USART1_RX),
        // re-enable USART1 and restart the RX DMA channel.
        UART_Init();
        // UART_Init() restarts the DMA write position at 0; resync the command
        // parser's read pointer or it stays parked in last session's stale bytes.
        UART_ResetRxParser();
    }
#if ENABLE_KEYER_DEBUG
    char buf[50];
    sprintf_(buf, "Port Ground %s\r\n", enable ? "Enabled" : "Disabled");
    UART_Send(buf, strlen(buf));
#endif
}

// Configure USB port paddle pins (PA11 = DM, PA12 = DP).
// When enabling: both become GPIO inputs with pull-ups (active-low paddle convention).
// When disabling: restore to alternate-function mode for USB (AF10 on PY32F071).
// Note: The USB CDC stack continues running in the background; only the pin mux changes.
void CW_ConfigureUsbPaddlePins(bool enable)
{
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    if (enable) {
        // Assert USB reset then gate the APB1 clock so the PHY stops driving PA11/PA12.
        SET_BIT(USBD->CR, USBD_CR_Reset);
        LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_USBD);

        // DON'T Enable the SYSCFG analog filter for PA11 and PA12 (PA_ENS bits).
        // when the glitches are filtered out, we can't tell what's real and what's RF.
        //SET_BIT(SYSCFG->PAENS, LL_GPIO_PIN_11 | LL_GPIO_PIN_12);

        // Both paddle pins are plain pulled-up inputs, referenced against
        // real ground via the paddle's own sleeve/common wire - each switch
        // shorts its own conductor straight to sleeve, not to the other
        // conductor, so there's no need to drive one pin low to read the
        // other. Configured once here and left alone; CW_ReadUSBPaddleRaw
        // just samples them, no per-read reconfiguration.
        LL_GPIO_InitTypeDef init = {0};
        init.Pin        = LL_GPIO_PIN_12 | LL_GPIO_PIN_11;
        init.Mode       = LL_GPIO_MODE_INPUT;
        init.Pull       = LL_GPIO_PULL_UP;
        init.Speed      = LL_GPIO_SPEED_FREQ_LOW;
        LL_GPIO_Init(GPIOA, &init);

        SYSTICK_DelayUs(GLITCH_SETTLE_US); // let the pins settle once, up front

    } else {
        // Restore PA11 and PA12 to USB alternate function.
        // On PY32F071 the USB DM/DP function is AF10.
        LL_GPIO_InitTypeDef init = {0};
        init.Pin        = LL_GPIO_PIN_11 | LL_GPIO_PIN_12;
        init.Mode       = LL_GPIO_MODE_ALTERNATE;
        init.Alternate  = LL_GPIO_AF_10;
        init.Pull       = LL_GPIO_PULL_NO;
        init.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH;
        LL_GPIO_Init(GPIOA, &init);

        // Clear USB reset and re-enable the APB1 clock to restore USB functionality.
        // Thse *should* be safe to do again if they were already correct
        CLEAR_BIT(USBD->CR, USBD_CR_Reset);
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USBD);

    }
}

// FM Radio is disabled on this firmware, we *always* configure
// PB15 as an input, because the radio might have the line reworked
// onto the mic input, and we don't want to affect that.
void CW_ConfigurePortRing(bool enable)
{
    // On new hardware the port-ring signal is on PA13 (shared with SWDIO).
    // When enabling we configure PA13 as a GPIO input with pull-up so
    // it can be sampled. When disabling we leave PA13 alone so the
    // debugger (SWD) continues to work — do not enable port-ring if
    // you need SWD.
    if (enable) {
        LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
        LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_13, LL_GPIO_MODE_INPUT);
        LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_13, LL_GPIO_PULL_UP);
    } else {
        // leave PA13 as SWDIO/default, but no pullup so it doesn't mess with the mic
        LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_13, LL_GPIO_PULL_NO);
    }
#if ENABLE_KEYER_DEBUG
    char buf[50];
    sprintf_(buf, "Port Ring %s\r\n", enable ? "Enabled" : "Disabled");
    UART_Send(buf, strlen(buf));
#endif
}

// Reset sampled key states (used from keyer init)
void CW_HW_ResetKeySamples(void)
{
    s_last_dit   = false;
    s_last_dah   = false;
    s_last_is_dah = false;
    s_dit_count  = 0;
    s_dah_count  = 0;
}


