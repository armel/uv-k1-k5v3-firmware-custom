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

 // CW Iambic Keyer implementation
// Lean FSM for iambic A/B with optional reversed mapping

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "app/cwkeyer.h"
#include "app/cwhardware.h"
#include "app/cwmacro.h"
#include "audio.h"
#include "settings.h"
#include "misc.h"
#include "radio.h"
#include "py32f071_ll_dma.h"
#include "py32f071_ll_tim.h"
#include "py32f071_ll_gpio.h"
#include "py32f071_ll_rcc.h"
#include "py32f071_ll_usart.h"
#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#include "driver/gpio.h"
#include "driver/systick.h"
#include "driver/millis.h"
#include "driver/system.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/keyboard.h"
#include "driver/backlight.h"
#include "ui/welcome.h"

#include "external/printf/printf.h"

// Debug logging control - set to 1 to enable UART debug output
#define CW_KEYER_DEBUG 0

// Timer scale: 1 kHz tick → 1 ms per tick
// 32-bit counter rolls over at 4294967295 ms (~49.7 days)
#define DITS_PER_WORD 50
#define TICKS_PER_MS 1
#define TICKS_PER_MINUTE (60 * 1000 * TICKS_PER_MS)  // 60,000

// CW_ErrorBeep's dit runs at 20 WPM whatever the keyer speed: 60 ms, then a 180 ms
// char gap before another can start
#define CW_ERROR_DIT_TICKS (TICKS_PER_MINUTE / (20 * DITS_PER_WORD))

// Keyer FSM states
typedef enum {
    CWK_STATE_IDLE = 0,
    CWK_STATE_ACTIVE_ELEMENT,
    CWK_STATE_INTER_ELEMENT_GAP,
    CWK_STATE_INTER_CHAR_GAP,    // Extended gap = end of character
    CWK_STATE_INTER_WORD_GAP,    // Long gap = inter-word space
    CWK_STATE_EMIT_NONE,         // Force a NONE action from the keyer
} CW_KeyerFSMState_t;

static CW_KeyerFSMState_t s_KeyerFSMState = CWK_STATE_IDLE;

// Bug (semi-automatic) keyer FSM states
typedef enum {
    BUG_STATE_IDLE = 0,
    BUG_STATE_DIT_ELEMENT,  // carrier on, timing dit duration
    BUG_STATE_DIT_GAP,      // carrier off, inter-element gap after a dit or a dah
    BUG_STATE_DAH_HOLD,     // carrier on, operator-held dah
    BUG_STATE_CHAR_GAP,     // carrier off, timing inter-char gap (mirrors CWK_STATE_INTER_CHAR_GAP)
    BUG_STATE_WORD_GAP,     // carrier off, timing inter-word gap (mirrors CWK_STATE_INTER_WORD_GAP)
} CW_BugFSMState_t;

static CW_BugFSMState_t s_bug_state       = BUG_STATE_IDLE;
static uint32_t         s_bug_phase_start = 0;
static bool             s_bug_after_dah   = false;  // the current BUG_STATE_DIT_GAP follows a dah
static bool             s_bug_dit_memory  = false;  // a dit flicked during a dah or the gap after it

// Internal keyer runtime state
static uint16_t       s_dit_count  = 0;      // duration in timer ticks (16-bit)
static uint16_t       s_dah_count  = 0;      // duration in timer ticks (16-bit)
static uint16_t       s_gap_count  = 0;      // inter-element gap in ticks (1 dit)
static uint16_t       s_ext_gap_count = 0;       // tick count when auto-char extension kicks in (16-bit)
static uint16_t       s_char_gap_count = 0;  // inter-char gap in ticks (3 dits)
static uint16_t       s_word_gap_count = 0;  // inter-word gap in ticks (7 dits)
static uint32_t       s_elem_start_count = 0;// element start counter (32-bit millis)
static uint16_t       s_elem_deadline_extra_ms = 0; // extra ms added to first-element deadline when audio path must be opened
static bool           s_active_is_dit = false;
static bool           s_pending_alternate = false; // alternate element queued
/* last sampled paddles moved to app/cwhardware.c */
static bool           s_last_handkey_ptt = false; // last accepted/confirmed PTT state for handkey mode

// Shared between handkey modes and bug dah handling
static bool           s_handkey_release_pending = false; // true while waiting out the release debounce
static uint32_t       s_handkey_release_pending_since_ms = 0; // millis() timestamp the release was first observed

#define CW_HANDKEY_RELEASE_DEBOUNCE_MS 10

// Macro playback state
#define CW_ROGER_EXTRA_GAP_DITS 2 // proper roger: extra space before its long dah, in dits
static char s_playback_buf[CW_MACRO_MAX_LEN * 2 + 1]; // decoded with spaces inserted
static uint16_t s_playback_buf_len = 0; // strlen of the buffer
static uint16_t s_playback_pos = 0; // index into playback buffer
static uint8_t s_play_char_pattern = 0; // current char morse pattern (LSB-first)
static uint8_t s_play_char_len = 0; // number of elements in current char
static uint8_t s_play_elem_index = 0; // current element index within char
static bool s_play_long_dah = false; // proper roger: hold each dah for gEeprom.CW_ROGER_DAH_DITS
static uint16_t s_play_setup_ms = 0; // TX/sidetone setup the current element lost at its start
static bool s_play_hidden = false; // keep played characters off the TX display line
static bool s_play_cue = false; // the sidetone dit of CW_ErrorBeep
static uint8_t s_play_eff_wpm = 0; // Farnsworth effective speed of the playback, 0 = keyer spacing
static uint16_t s_play_char_gap_count = 0; // playback's char gap: s_char_gap_count, or stretched to s_play_eff_wpm
static uint16_t s_play_word_gap_count = 0; // playback's word gap, likewise

// Playback FSM states
typedef enum {
    PB_STATE_IDLE = 0,
    PB_STATE_ACTIVE_ELEMENT,
    PB_STATE_INTER_ELEMENT_GAP,
    PB_STATE_INTER_CHAR_GAP,
    PB_STATE_INTER_WORD_GAP,
} PB_State_t;
static PB_State_t s_pb_state = PB_STATE_IDLE;
static bool s_play_space_pending = false;  // indicates next char should be shown with a leading space

// Reconfigure requested (apply at idle or after gap)
static volatile bool s_cfg_dirty = true;

// Keyer enabled flag - this means ALL CW keying, not just paddle FSM.
static volatile bool s_enable_keyer = false;

// Last fully configured key input mode. 0xFF means "not configured".
static uint8_t s_last_key_input_mode = 0xFF;

#ifdef ENABLE_FLASHLIGHT
bool gCW_FlashlightSending = false;
#endif

void CW_KeyerResetRuntime(void)
{
    s_KeyerFSMState = CWK_STATE_EMIT_NONE;

    // Do not reset s_elem_start_count here: resetting the element start
    // timestamp can prematurely cancel detection of an inter-word gap
    // when a reconfigure is requested. Preserve the existing timestamp
    // so the idle/word-gap logic remains accurate.

    s_active_is_dit = false;
    s_pending_alternate = false;
    s_last_handkey_ptt = false;
    s_handkey_release_pending = false;
    s_handkey_release_pending_since_ms = 0;
    s_bug_state = BUG_STATE_IDLE;
    s_bug_phase_start = 0;
    s_bug_after_dah = false;
    s_bug_dit_memory = false;

    // clear any stale edge memory
    CW_HW_ResetKeySamples();
}

// Called when changing to non-CW mode
static void CW_KeyerDeinit()
{
    CW_KeyerResetRuntime();

    // PA10 (USART1 RX) must be handed back first: CW_ConfigurePortGround(true)
    // disabled USART1, stopped the RX DMA channel and left PA10 driving low, and
    // nothing else in the firmware undoes that short of a power cycle. Leaving it
    // captured kills the hardware serial port (CHIRP sees no reply at all, which
    // surfaces as a "short header") for the rest of the session.
    CW_ConfigurePortGround(false);
    CW_ConfigurePortRing(false); // make sure PB15 is an input with no pullup, to avoid affecting the line if shorted to mic (keyer rework)
    CW_ConfigureUsbPaddlePins(false); // restore PA11/PA12 to USB AF if USB paddle mode was active

    gCW_KeyerManagesPtt = false;
    gCW_KeyerUsingSD1 = false;
    s_enable_keyer = false;
    s_last_key_input_mode = 0xFF;
}

void CW_KeyerReconfigure(bool enable)
{
#if CW_KEYER_DEBUG
    if (enable) {
        UART_Send("reconfig true\r\n", 14);
    } else {
        UART_Send("reconfig false\r\n", 15);
    }
#endif

    if (!enable) { 
        CW_KeyerDeinit();
        s_cfg_dirty = false;
        return;
    }

    // take over PTT and SIDE1 (if was or is in the INPUT set) immediately
    gCW_KeyerManagesPtt = true;
    gCW_KeyerUsingSD1 |= gEeprom.CW_KEY_INPUT & CW_KEY_FLAG_SIDE1;
    s_cfg_dirty = true; // Defer init until idle or gap
}

// Sidetone gain curve: level 0 is off (handled by BK4819_REG_70_TONE1_VALUE).
// The Tone1 tuning gain alone bottoms out at 1, which is still loud with the volume
// knob turned up for a weak station, so each level also sets the AF DAC gain
// (REG_48, ~2 dB/step) while the sidetone plays. The DAC values are absolute: while
// listening, RX audio's DAC gain is 0xF (RADIO_SetModulation), which is what the old
// 6-level sidetone played at, so level 15 is the old top level and the scale doesn't
// depend on the DAC calibration. Relative to level 15:
//   1-7:   DAC 0, tuning gain halves per step     -72 to -36 dB, 6 dB/step
//   8-10:  full tuning gain, DAC 0/3/6            -30, -24, -18 dB
//   11-15: full tuning gain, DAC 8/11/13/14/15    -14, -8, -4, -2, 0 dB
// Levels 6 and 11-15 are v1.x's levels 1-6 (CHIRP's v1.x image conversion maps them).
// RX audio gets its own DAC gain back after the hang time (RADIO_SetModulation, RADIO_SetupRegisters).
// Retune by editing the table and rebuilding.
typedef struct {
    uint8_t tone_gain;  // BK4819 Tone1 tuning gain, 0-127
    uint8_t dac_gain;   // AF DAC gain, 0-15
} CW_SidetoneStep_t;

static const CW_SidetoneStep_t s_sidetone[CW_SIDETONE_LEVEL_MAX + 1] = {
    {   0,  0 },  // level 0 = off
    {   1,  0 }, {   2,  0 }, {   4,  0 }, {   8,  0 }, {  16,  0 }, {  32,  0 }, {  64,  0 },
    { 127,  0 }, { 127,  3 }, { 127,  6 },
    { 127,  8 }, { 127, 11 }, { 127, 13 }, { 127, 14 }, { 127, 15 },
};

void CW_ApplySidetoneGain(void)
{
    uint8_t level = gEeprom.CW_SIDETONE_LEVEL;
    if (level > CW_SIDETONE_LEVEL_MAX) {
        level = CW_SIDETONE_LEVEL_MAX;
    }
    const CW_SidetoneStep_t *step = &s_sidetone[level];

    BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_TONE1_VALUE(step->tone_gain));
    BK4819_SetAfDacGain(step->dac_gain);
}

// Farnsworth spacing (ARRL, KE3Z): characters keep the keyer's timing and only the
// char and word gaps stretch, so the text averages s_play_eff_wpm. PARIS is 31 dits
// of elements and element gaps plus 19 of char and word gaps (four 3s and a 7);
// at the effective speed those gaps get whatever is left of its 60/eff seconds.
static void CW_UpdatePlaybackGaps(void)
{
    if (s_play_eff_wpm == 0 || s_play_eff_wpm >= gEeprom.CW_KEY_WPM) {
        s_play_char_gap_count = s_char_gap_count;
        s_play_word_gap_count = s_word_gap_count;
        return;
    }
    const uint32_t gaps_ticks = TICKS_PER_MINUTE / s_play_eff_wpm - 31U * s_dit_count;
    s_play_char_gap_count = 3U * gaps_ticks / 19U;
    s_play_word_gap_count = 7U * gaps_ticks / 19U;
}

void CW_UpdateWPM()
{
    const uint32_t wpm = gEeprom.CW_KEY_WPM;

    // Each length is rounded from the exact dit (1200 / wpm ms), not built up
    // from a truncated dit: that made a 45 WPM dah 78 ms instead of 80.
    #define CW_HALF_DITS_TO_TICKS(h) \
        (((h) * TICKS_PER_MINUTE + wpm * DITS_PER_WORD) / (2U * wpm * DITS_PER_WORD))

    s_dit_count = CW_HALF_DITS_TO_TICKS(2U);
    s_dah_count = CW_HALF_DITS_TO_TICKS(6U);
    s_gap_count = s_dit_count; // inter-element gap = 1 dit
    s_ext_gap_count = CW_HALF_DITS_TO_TICKS(3U); // after 1.5 dits, hold char gap if key pressed
    s_char_gap_count = CW_HALF_DITS_TO_TICKS(6U); // inter-char gap = 3 dits
    s_word_gap_count = CW_HALF_DITS_TO_TICKS(14U); // inter-word gap = 7 dits

    #undef CW_HALF_DITS_TO_TICKS
    CW_UpdatePlaybackGaps();

#if CW_KEYER_DEBUG
    char buf[80];
    sprintf_(buf, "CW_SetWPM: WPM=%u dit=%u dah=%u gap=%u\r\n", 
             wpm, s_dit_count, s_dah_count, s_gap_count);
    UART_Send(buf, strlen(buf));
#endif
}

// Initialize keyer from gEeprom settings
static void CW_KeyerInit()
{
    const uint8_t key_input_mode = gEeprom.CW_KEY_INPUT;

#if CW_KEYER_DEBUG
    char buf[80];
    sprintf_(buf, "CW_KeyerInit: enabled:%u WPM=%u last_mode=%u key_input_mode=%u\r\n", 
             s_enable_keyer, gEeprom.CW_KEY_WPM, s_last_key_input_mode, key_input_mode);
    UART_Send(buf, strlen(buf));
#endif

    CW_UpdateWPM();
    CW_KeyerResetRuntime();
    s_cfg_dirty = false;
    // Keyer is already enabled with this mode
    if (s_enable_keyer && s_last_key_input_mode == key_input_mode) {
        return;
    }

    bool uses_port_ground = (key_input_mode & CW_KEY_FLAG_PORT_GROUND) != 0;
    bool uses_usb_port    = (key_input_mode & CW_KEY_FLAG_USB_PORT) != 0;

    // Handkey-family modes have no timing engine, so nothing will ever decode
    // a fresh character into the TX display while active. Clear out whatever
    // was left behind by a prior paddle/macro session so it can't resurface
    // (e.g. via the post-TX holdoff window) and look like a live decode.
    if (key_input_mode & CW_KEY_FLAG_NO_KEYER) {
        CW_ClearTxDisplay();
        gCW_TxDisplayHoldoff_10ms = 0;
    }

    // Every mode that sets PORT_RING also sets PORT_GROUND, and Port Handkey mode
    // (PORT_GROUND without PORT_RING) also needs the ring pin configured since it
    // treats either port contact as the key - so PORT_GROUND alone is sufficient
    // to decide whether the ring pin should be configured.
    CW_ConfigurePortRing(uses_port_ground);
    // Unconditional, not "if (uses_port_ground)": switching straight from a port
    // mode to a non-port CW mode never runs CW_KeyerDeinit(), so this is the only
    // place that gives PA10/USART1 back in that path.
    CW_ConfigurePortGround(uses_port_ground);
    CW_ConfigureUsbPaddlePins(uses_usb_port);

    gCW_KeyerUsingSD1 = (key_input_mode & CW_KEY_FLAG_SIDE1) != 0;
    s_last_key_input_mode = key_input_mode;
    s_enable_keyer = true;
    gCW_KeyerManagesPtt = true;
}

// --- Macro playback API implementation ---

// Prime the playback FSM to send whatever is in s_playback_buf from the start.
// eff_wpm is the Farnsworth effective speed, 0 for the keyer's own spacing.
static void CW_BeginPlayback(bool repeat, uint8_t eff_wpm)
{
    s_playback_buf_len = (uint16_t)strlen(s_playback_buf);
    s_playback_pos = 0;
    s_play_elem_index = 0;
    s_play_char_len = 0;
    s_play_char_pattern = 0;

    // Store repeat flag
    gCW_PlaybackRepeat = repeat;
    gCW_MessageRepeatCountdown_500ms = 0;  // Clear any pending countdown

    // Prime the playback FSM to start immediately
    s_play_space_pending = false;
    s_play_hidden = false;
    s_play_cue = false;
    s_play_eff_wpm = eff_wpm;
    CW_UpdatePlaybackGaps();
    s_pb_state = PB_STATE_INTER_CHAR_GAP;
    // Lead in with the keyer's own char gap: only the gaps between characters stretch
    s_elem_start_count = millis() - (s_play_char_gap_count - s_char_gap_count);
    gCW_PlaybackActive = (s_playback_buf_len > 0);
}

void CW_StartMacroPlayback(uint8_t macroIndex, bool repeat)
{
    // Do nothing if recording or playback already in progress
    if (gCW_Recording || gCW_PlaybackActive) return;

    // Load the macro into a local buffer (decoded with spaces)
    memset(s_playback_buf, 0, sizeof(s_playback_buf));
    CW_LoadMacro(macroIndex, s_playback_buf, sizeof(s_playback_buf));
    gCW_PlaybackMacroIndex = macroIndex;
    s_play_long_dah = false;
    CW_ClearTxDisplay();
    CW_BeginPlayback(repeat, 0);

#if CW_KEYER_DEBUG
    if (gCW_PlaybackActive) {
        char buf[80];
        sprintf_(buf, "Playback started: idx=%u buf='%s'\r\n", macroIndex, s_playback_buf);
        UART_Send(buf, strlen(buf));
    }
#endif
}

// Proper roger: R with the dah held for CWrgr dits (default 9) and 3 dits of space
// before it, di---daaaaaaaaah-dit. CWrtyp picks what leads into it: nothing, E S
// (skipping: dit dididit di---daaaaaaaaah-dit) or E E (stuttering: dit dit
// di---daaaaaaaaah-dit).
void CW_StartProperRoger(void)
{
    static const char *const roger_text[CW_ROGER_TYPE_COUNT] = { "R", "ESR", "EER" };

    if (gCW_Recording || gCW_PlaybackActive) return;

    strcpy(s_playback_buf, roger_text[gEeprom.CW_ROGER_TYPE]);
    s_play_long_dah = true;
    CW_BeginPlayback(false, 0);

    // Skip the char-gap lead-in so the first dit keys on the next poll. Reaching
    // the side key already takes longer than a char gap after any keyed element.
    s_elem_start_count = millis() - s_char_gap_count;

    // Keep what was keyed before so the R reads as a reply to it
    s_play_space_pending = (gCW_TX_DisplayIndex > 0);
}

bool CW_PlaybackIsProperRoger(void)
{
    return gCW_PlaybackActive && s_play_long_dah;
}

void CW_StartTextPlayback(const char *text, bool show, uint8_t eff_wpm)
{
    if (gCW_Recording || gCW_PlaybackActive) return;

    strncpy(s_playback_buf, text, sizeof(s_playback_buf) - 1);
    s_playback_buf[sizeof(s_playback_buf) - 1] = '\0';
    s_play_long_dah = false;
    CW_BeginPlayback(false, eff_wpm);
    s_play_hidden = !show;
}

void CW_ErrorBeep(void)
{
    if (CW_PlaybackIsCue())
        return;  // one at a time: the dit already playing answers this press too

    // Only CW runs the sidetone path, and the dit must not cut into keying, other
    // playback, a repeating macro's pause (starting playback cancels the repeat) or
    // a recording. Those get the double beep, which is silent in CW.
    if (gTxVfo->Modulation != MODULATION_CW || gCW_Recording || gCW_PlaybackActive
        || gCW_PlaybackRepeat || !CW_KeyerIsIdle()) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }

    CW_StartTextPlayback("E", false, 0);
    s_play_cue = true;

    // skip the char-gap lead-in so the dit answers the key press right away
    s_elem_start_count = millis() - 3U * CW_ERROR_DIT_TICKS;
}

bool CW_PlaybackIsCue(void)
{
    return gCW_PlaybackActive && s_play_cue;
}

bool CW_KeyerIsIdle(void)
{
    // A held straight key (the handkey modes, or PTT in the USB paddle modes) is
    // keying too, though the paddle FSMs sit idle under it
    return (s_KeyerFSMState == CWK_STATE_IDLE || s_KeyerFSMState == CWK_STATE_EMIT_NONE)
        && s_bug_state == BUG_STATE_IDLE && !s_last_handkey_ptt;
}

void CW_PlaybackExtendElement(uint32_t setup_ms)
{
    s_play_setup_ms = (setup_ms > UINT16_MAX) ? UINT16_MAX : (uint16_t)setup_ms;
}

// Stop playback immediately (user interrupted)
void CW_StopPlayback(void)
{
    gCW_PlaybackActive = false;
    gCW_PlaybackRepeat = false;       // Cancel any pending repeat
    gCW_MessageRepeatCountdown_500ms = 0;
    s_pb_state = PB_STATE_IDLE;
    s_playback_buf_len = 0;
    s_playback_pos = 0;
}

// Playback handler - returns CW_Action_t similar to CW_HandleState
CW_Action_t CW_PlaybackHandleState(void)
{
    if (!gCW_PlaybackActive) return CW_ACTION_NONE;

    const uint32_t cur_count = millis();
    CW_Input in = {0};

    // If user hits a paddle during playback (or PTT, in the USB paddle modes), stop
    // playback entirely
    CW_ReadKeys(&in);
    if (in.dit || in.dah || in.ptt) {
        CW_StopPlayback();
        return CW_ACTION_NONE;
    }

    switch (s_pb_state) {
    case PB_STATE_ACTIVE_ELEMENT: {
        const uint32_t target = s_play_cue      ? CW_ERROR_DIT_TICKS
                              : s_active_is_dit ? s_dit_count
                              : s_play_long_dah ? gEeprom.CW_ROGER_DAH_DITS * (uint32_t)s_dit_count
                              : s_dah_count;
        const uint32_t elapsed = millis_since(s_elem_start_count);
        if (elapsed < target + s_play_setup_ms) {
            return CW_ACTION_CARRIER_HOLD_ON;
        } else {
            // End element
            s_play_setup_ms = 0;
            s_elem_start_count = cur_count;
            s_pb_state = PB_STATE_INTER_ELEMENT_GAP;
            return CW_ACTION_CARRIER_OFF;
        }
        break;
    }

    case PB_STATE_INTER_ELEMENT_GAP: {
        const uint32_t elapsed = millis_since(s_elem_start_count);
        // A roger leaves extra space before its long dah
        const uint8_t next = s_play_elem_index + 1;
        const bool long_dah_next = s_play_long_dah && next < s_play_char_len
                                && ((s_play_char_pattern >> next) & 1);
        if (elapsed >= s_gap_count
                       + (long_dah_next ? CW_ROGER_EXTRA_GAP_DITS * (uint32_t)s_dit_count : 0)) {
            // Advance to next element in current char
            s_play_elem_index++;
            if (s_play_elem_index < s_play_char_len) {
                // Start next element
                bool is_dit = (((s_play_char_pattern >> s_play_elem_index) & 1) == 0);
                s_active_is_dit = is_dit;
                s_elem_start_count = cur_count;
                s_pb_state = PB_STATE_ACTIVE_ELEMENT;
                return CW_ACTION_CARRIER_ON;
            } else {
                // Char complete - go to char gap. Keep s_elem_start_count at the
                // element-end origin (set at element end above) so char/word gaps
                // are one timeline measured from the last keyed element, like the
                // recorder in CW_HandleState() - do not reset it here.
                s_pb_state = PB_STATE_INTER_CHAR_GAP;
                return CW_ACTION_NONE;
            }
        }
        return CW_ACTION_NONE;
        break;
    }

    case PB_STATE_INTER_CHAR_GAP: {
        const uint32_t elapsed  = millis_since(s_elem_start_count);
        const uint32_t char_gap = s_play_cue ? 3U * CW_ERROR_DIT_TICKS : s_play_char_gap_count;
        if (elapsed < char_gap) {
            return CW_ACTION_NONE;
        }
        // Move to next character (if any)
        if (s_playback_pos >= s_playback_buf_len) {
            // Done with current pass
            if (gCW_PlaybackRepeat && gEeprom.CW_MESSAGE_REPEAT_DELAY > 0) {
                // Start repeat countdown (value is in 500ms units)
                gCW_MessageRepeatCountdown_500ms = gEeprom.CW_MESSAGE_REPEAT_DELAY * 2;
                gCW_PlaybackActive = false;  // Pause playback while counting down
                s_pb_state = PB_STATE_IDLE;
            } else {
                // No repeat or delay is 0 - just stop
                CW_StopPlayback();
            }
            return CW_ACTION_NONE;
        }
        char ch = s_playback_buf[s_playback_pos++];
        if (ch == ' ') {
            // set pending space for next visible character and play word gap.
            // Origin stays at the last element end (single timeline), so the
            // word boundary lands at 7 units total, not 3 (char gap) + 7.
            s_play_space_pending = true;
            s_pb_state = PB_STATE_INTER_WORD_GAP;
            return CW_ACTION_NONE;
        }
        // Update TX centerline display with the next char (respect pending space).
        // A roger shows only its R, so the letters leading into it read as part of it.
        if (!s_play_hidden && !(s_play_long_dah && ch != 'R')) {
            CW_AddToTxDisplay(ch, s_play_space_pending);
            s_play_space_pending = false;
        }

        // Get morse pattern for char 
        uint8_t patt = 0, len = 0;
        if (!CW_GetMorseForChar(ch, &patt, &len)) {
            // Unknown char - skip
            s_pb_state = PB_STATE_INTER_CHAR_GAP;
            s_elem_start_count = cur_count;
            return CW_ACTION_NONE;
        }
        s_play_char_pattern = patt;
        s_play_char_len = len;
        s_play_elem_index = 0;
        // Start first element
        bool is_dit = (((s_play_char_pattern >> s_play_elem_index) & 1) == 0);
        s_active_is_dit = is_dit;
        s_elem_start_count = cur_count;
        s_pb_state = PB_STATE_ACTIVE_ELEMENT;
        return CW_ACTION_CARRIER_ON;
        break;
    }

    case PB_STATE_INTER_WORD_GAP: {
        const uint32_t elapsed = millis_since(s_elem_start_count);
        if (elapsed >= s_play_word_gap_count) {
            // Word gap done - advance to char-gap state with the same origin
            // preserved. elapsed (>=7) already exceeds s_play_char_gap_count (3),
            // so the next char is read immediately with no extra wait.
            s_pb_state = PB_STATE_INTER_CHAR_GAP;
        }
        return CW_ACTION_NONE;
        break;
    }

    case PB_STATE_IDLE:
    default:
        return CW_ACTION_NONE;
    }
}


void CW_PlaybackIndicatorDeadline(void)
{
    /* Called from APP_TimeSlice10ms() every 10ms. Use a local 10ms-tick counter
     * (kept private to the function) and toggle the indicator every 25 ticks
     * (~250 ms). Keep the indicator off when playback is inactive.
     */
    static uint8_t s_tick_count_10ms = 0;
    const uint8_t TARGET_TICKS = 25; /* ~250 ms */

    if (gCW_PlaybackActive || gCW_MessageRepeatCountdown_500ms > 0) {
        if (++s_tick_count_10ms >= TARGET_TICKS) {
            s_tick_count_10ms = 0;
            gCW_PlayIndicatorOn = !gCW_PlayIndicatorOn;
            gUpdateDisplay = true;
        }
    } else {
        /* Reset counter and clear indicator when not playing */
        s_tick_count_10ms = 0;
        if (gCW_PlayIndicatorOn) {
            gCW_PlayIndicatorOn = false;
            gUpdateDisplay = true;
        }
    }
}

// Check keyer inputs before mode change
// Returns true if inputs are valid (released), false to abort mode change
bool CW_CheckKeyerInputs(uint8_t new_mode)
{    
    // hard deconfig, get all pins in a known state
    CW_KeyerDeinit();

    // Determine if we need to configure port pins for this mode (use bit flags).
    // PORT_GROUND alone is sufficient to decide whether the ring pin (PA13) needs
    // checking too: every PORT_RING mode also sets PORT_GROUND, and Port Handkey
    // mode (PORT_GROUND without PORT_RING) needs the ring pin checked as well
    // since it treats either port contact as the key.
    bool uses_port_ground = (new_mode & CW_KEY_FLAG_PORT_GROUND);
    bool uses_usb_port  = (new_mode & CW_KEY_FLAG_USB_PORT);

    // Handkey mode without port ground doesn't need further validation.
    // USB Port Handkey still has real electrical pins to check for stuck keys.
    if (new_mode & CW_KEY_FLAG_NO_KEYER
        && !uses_port_ground
        && !uses_usb_port) {
        return true;
    }

    // Button-only modes don't need validation (no port pins to check)
    if (!uses_port_ground && !uses_usb_port) {
        return true;
    }

#if CW_KEYER_DEBUG
    UART_Send("Checking CW keyer inputs\r\n", 26);
#endif

    // Temporarily configure port pins if needed
    if (uses_port_ground)
    {
#if CW_KEYER_DEBUG
        UART_Send("Configuring port ground and ring for CW keyer check\r\n", 55);
#endif
        CW_ConfigurePortGround(true);
        CW_ConfigurePortRing(true);
    }
    if (uses_usb_port) {
#if CW_KEYER_DEBUG
        UART_Send("Configuring USB paddle pins for CW keyer check\r\n", 49);
#endif
        CW_ConfigureUsbPaddlePins(true);
    }

    // Allow pins to stabilize after configuration
    SYSTEM_DelayMs(50);

#if CW_KEYER_DEBUG
    UART_Send("done with config, starting validation\r\n", 40);
#endif

    // Check inputs with 1ms intervals - consider stuck if key stays down for 3 checks out of 20
    int stuck_count = 0;
    bool any_stuck = false;
#if CW_KEYER_DEBUG
    int total_checks = 0;
#endif
    
    for (int i = 0; i < 20; i++) {  // Check up to 20 times = 200ms max
        bool dit = false, dah = false;
        if (uses_usb_port) {
            // Raw pin read - bypasses CW_ReadKeysForMode's NO_KEYER gating so
            // USB Port Handkey's pins get checked for stuck keys too.
            CW_ReadUSBPaddleRaw(&dit, &dah);
        } else {
            CW_ReadKeysForMode(new_mode, &dit, &dah);
        }
#if CW_KEYER_DEBUG
        total_checks++;
        
        // Debug output every 5 checks
        if (i % 5 == 0) {
            char dbg[50];
            sprintf_(dbg, "check %d: dit=%d dah=%d stuck=%d\r\n", i, dit, dah, stuck_count);
            UART_Send(dbg, strlen(dbg));
        }
#endif
        
        if (dit || dah) {
            stuck_count++;
            if (stuck_count > 2) {  // 3 strikes
                any_stuck = true;
                break;
            }
        }
        
        SYSTEM_DelayMs(10);
    }
    
#if CW_KEYER_DEBUG
    char buf[60];
    sprintf_(buf, "total_checks=%d stuck_count=%d stuck=%d\r\n", 
             total_checks, stuck_count, any_stuck);
    UART_Send(buf, strlen(buf));
#endif

    CW_KeyerDeinit();  // put ports back, disable keyer

    // If no stuck keys detected, mode is valid
    if (!any_stuck) {
#if CW_KEYER_DEBUG
        UART_Send("CW keyer inputs valid\r\n", 24);
#endif
        return true;
    }
#if CW_KEYER_DEBUG
    UART_Send("CW keyer inputs stuck\r\n", 24);
#endif


    return false;
}

CW_Action_t ptt_action(void)
{
    CW_Action_t action = CW_ACTION_NONE;
    bool ptt;

    if (gEeprom.CW_KEY_INPUT & CW_KEY_FLAG_USB_PORT) {
        // USB port: PTT is a straight key next to the USB key, for a proper roger
        // sent by hand. In USB Port Handkey either pin is one too; the paddle modes
        // leave the pins to the keyer (see CW_HandleState).
        bool tip = false, ring = false;
        if (gEeprom.CW_KEY_INPUT & CW_KEY_FLAG_NO_KEYER)
            CW_ReadUSBPaddleRaw(&tip, &ring);
        ptt = tip || ring || GPIO_IsPttPressed();
        if (CW_KeyInputLockedOut(ptt))
            ptt = false;
    } else if (gEeprom.CW_KEY_INPUT & CW_KEY_FLAG_PORT_GROUND) {
        // Port Handkey: either port contact (tip or ring) acts as a straight key.
        // (This branch is only reached for NO_KEYER modes - see the caller in
        // CW_HandleState - and USB_PORT is already handled above, so PORT_GROUND
        // alone is enough to identify Port Handkey mode here.)

        // Go through CW_ReadKeys() rather than CW_ReadKeysForMode() directly -
        // CW_ReadKeys() applies the cross-poll 3-strike debounce on top of the
        // per-read deglitch.
        CW_Input in = {0};
        CW_ReadKeys(&in);
        ptt = in.dit || in.dah;
    } else {
        // Read PTT button (PC5) via wrapper (active low)
        ptt = GPIO_IsPttPressed();
        if (CW_KeyInputLockedOut(ptt))
            ptt = false;
    }

    if (ptt) {
        // Closed (on either pin) is trusted immediately - see rationale above.
        s_handkey_release_pending = false;
    } else {
        // Both open. Don't believe it's a real release until it's held for
        // the full window; a fresh closure before then (same pin re-bouncing,
        // or the other pin taking over) cancels the pending release entirely.
        if (!s_handkey_release_pending) {
            s_handkey_release_pending = true;
            s_handkey_release_pending_since_ms = millis();
        }
        if (millis_since(s_handkey_release_pending_since_ms) < CW_HANDKEY_RELEASE_DEBOUNCE_MS) {
            ptt = s_last_handkey_ptt;
        }
    }

    if (ptt && !s_last_handkey_ptt) {
        // PTT pressed
        action = CW_ACTION_CARRIER_ON;
#if CW_KEYER_DEBUG
        UART_Send("handkey PTT on\r\n", 17);
#endif
    } else if (!ptt && s_last_handkey_ptt) {
        // PTT released
        action = CW_ACTION_CARRIER_OFF;
#if CW_KEYER_DEBUG
        UART_Send("handkey PTT off\r\n", 18);
#endif
    } else if (ptt && s_last_handkey_ptt) {
        // PTT being held - keep carrier active
        action = CW_ACTION_CARRIER_HOLD_ON;
    }

    s_last_handkey_ptt = ptt;
    return action;
}

// Semi-automatic bug keyer FSM.  Dah is a raw handkey; dit auto-repeats at WPM timing.
// Dah takes priority immediately; releasing either key stops it with no trailing gap or element.
static CW_Action_t CW_HandleBugState(void)
{
    CW_Input in = {0};
    CW_ReadKeys(&in);
    const uint32_t now = millis();

    switch (s_bug_state) {
    case BUG_STATE_IDLE:
        if (in.dah) {
            s_bug_state = BUG_STATE_DAH_HOLD;
            return CW_ACTION_CARRIER_ON;
        }
        if (in.dit) {
            s_bug_phase_start = now;
            s_elem_deadline_extra_ms = AUDIO_IsAudioPathOn() ? 0 : 20;
            s_bug_state = BUG_STATE_DIT_ELEMENT;
            return CW_ACTION_CARRIER_ON;
        }
        return CW_ACTION_NONE;

    case BUG_STATE_DIT_ELEMENT:
        if (millis_since(s_bug_phase_start) >= (uint32_t)(s_dit_count + s_elem_deadline_extra_ms)) {
            // Element complete — always finish the full dit before stopping
            CW_EncoderProcessElement(CW_ELEMENT_DIT);
            s_elem_deadline_extra_ms = 0;
            s_bug_phase_start = now;
            // Gap and repeat only if dit is still held at completion; else start the char gap
            //s_bug_state = in.dit ? BUG_STATE_DIT_GAP : BUG_STATE_CHAR_GAP;
            s_bug_state = BUG_STATE_DIT_GAP;
            return CW_ACTION_CARRIER_OFF;
        }
        return CW_ACTION_CARRIER_HOLD_ON;

    case BUG_STATE_DIT_GAP:
        // Dah is a raw handkey and takes priority immediately, even mid-gap. A
        // remembered dit can't go ahead of it, so it's dropped.
        if (in.dah) {
            s_bug_after_dah = false;
            s_bug_dit_memory = false;
            s_bug_phase_start = now;
            s_bug_state = BUG_STATE_DAH_HOLD;
            return CW_ACTION_CARRIER_ON;
        }
        // After a dah, a dit flicked during the gap is remembered, so it still
        // plays when it's released before the gap ends
        if (s_bug_after_dah && in.dit_rise) {
            s_bug_dit_memory = true;
        }
        if (millis_since(s_bug_phase_start) < (uint32_t)s_gap_count) {
            // Let the gap complete before deciding what to do next.
            return CW_ACTION_NONE;
        }
        s_bug_phase_start = now;
        s_bug_after_dah = false;
        if (in.dit || s_bug_dit_memory) {
            s_bug_dit_memory = false;
            s_bug_state = BUG_STATE_DIT_ELEMENT;
            return CW_ACTION_CARRIER_ON;
        }
        // Released by the end of the full gap — no next element, start the char gap
        s_bug_state = BUG_STATE_CHAR_GAP;
        return CW_ACTION_NONE;

    case BUG_STATE_DAH_HOLD:
        // A dit flicked while the dah is still down (rolling from dah to dit) is
        // remembered and plays after the gap that follows the dah
        if (in.dit_rise) {
            s_bug_dit_memory = true;
        }
        if (in.dah) {
            // Held (or re-closed before a pending release below was confirmed) -
            // a fresh closure is always trusted immediately, same as ptt_action().
            s_handkey_release_pending = false;
            return CW_ACTION_CARRIER_HOLD_ON;
        }

        // Dah paddle reads open. Don't trust it as a real release until it's
        // held for the full debounce window - same rationale and constant as
        // ptt_action(): dah is a raw handkey on this same hardware, so it
        // bounces the same way. Without this, a bounce chain reads as
        // "released" on its first clean open sample, chopping one held dah
        // into several short ones.
        if (!s_handkey_release_pending) {
            s_handkey_release_pending = true;
            s_handkey_release_pending_since_ms = now;
        }
        if (millis_since(s_handkey_release_pending_since_ms) < CW_HANDKEY_RELEASE_DEBOUNCE_MS) {
            return CW_ACTION_CARRIER_HOLD_ON;
        }
        s_handkey_release_pending = false;

        if(millis_since(s_bug_phase_start) >= (uint32_t)s_gap_count) {
            CW_EncoderProcessElement(CW_ELEMENT_DAH);
        }

        s_bug_phase_start = now;
        s_bug_after_dah = true;
        //s_bug_state = BUG_STATE_CHAR_GAP;
        s_bug_state = BUG_STATE_DIT_GAP; // make an element gap
        return CW_ACTION_CARRIER_OFF;

    case BUG_STATE_CHAR_GAP:
        if (in.dah) {
            s_bug_phase_start = now;
            s_bug_state = BUG_STATE_DAH_HOLD;
            return CW_ACTION_CARRIER_ON;
        }
        if (in.dit) {
            s_bug_phase_start = now;
            s_bug_state = BUG_STATE_DIT_ELEMENT;
            return CW_ACTION_CARRIER_ON;
        }
        if (millis_since(s_bug_phase_start) >= (uint32_t)s_char_gap_count) {
            // Char gap complete — character boundary reached. Carry over the
            // gap timer (mirrors CWK_STATE_INTER_CHAR_GAP -> CWK_STATE_INTER_WORD_GAP).
            CW_EncoderProcessElement(CW_ELEMENT_INTER_CHAR_SPACE);
            s_bug_state = BUG_STATE_WORD_GAP;
        }
        return CW_ACTION_NONE;

    case BUG_STATE_WORD_GAP:
        if (in.dah) {
            s_bug_phase_start = now;
            s_bug_state = BUG_STATE_DAH_HOLD;
            return CW_ACTION_CARRIER_ON;
        }
        if (in.dit) {
            s_bug_phase_start = now;
            s_bug_state = BUG_STATE_DIT_ELEMENT;
            return CW_ACTION_CARRIER_ON;
        }
        if (millis_since(s_bug_phase_start) >= (uint32_t)s_word_gap_count) {
            // Word gap complete
            CW_EncoderProcessElement(CW_ELEMENT_INTER_WORD_SPACE);
            s_bug_state = BUG_STATE_IDLE;
        }
        return CW_ACTION_NONE;
    }

    return CW_ACTION_NONE;
}

// Manage CW sending - called via main->CW_AppUpdate() every 1 ms.
// Uses millis() to manage deadlines for element duration and spacing.
CW_Action_t CW_HandleState(void)
{
    // Default action: carrier is off and stays off (gap or idle)
    CW_Action_t action = CW_ACTION_NONE;

    // Apply pending init if needed.
    if (s_cfg_dirty && s_KeyerFSMState == CWK_STATE_IDLE && s_bug_state == BUG_STATE_IDLE) {
        // Defer applying configuration while we're inside a potential word gap
        // so we don't reset timing before an inter-word space can be detected.
#if CW_KEYER_DEBUG
        {
            char dbg_init[80];
            sprintf_(dbg_init, "CFG_DIRTY at idle: now=%u s_elem_start=%u idle_elapsed=%u word_gap=%u\r\n", millis(), s_elem_start_count, millis_since(s_elem_start_count), s_word_gap_count);
            UART_Send(dbg_init, strlen(dbg_init));
        }
#endif
        if (millis_since(s_elem_start_count) >= (uint32_t)s_word_gap_count) {
            CW_KeyerInit();
        }
        return CW_ACTION_NONE;
    } else if (s_KeyerFSMState == CWK_STATE_EMIT_NONE || !s_enable_keyer) {
        s_KeyerFSMState = CWK_STATE_IDLE;
        return CW_ACTION_NONE;
    }

    const uint32_t cur_count = millis();

    // Check if keyer is disabled (handkey modes have NO_KEYER flag set)
    if (gEeprom.CW_KEY_INPUT & CW_KEY_FLAG_NO_KEYER) {
        return ptt_action();
    }

    // USB port paddle modes: PTT is a straight key next to the paddle, so a proper
    // roger can be sent by hand. It takes over whenever the paddle isn't keying an
    // element and keeps the carrier until it's released; the paddle FSMs wait under
    // it and pick up where they were. Its elements aren't decoded, the same as the
    // handkey modes.
    if ((gEeprom.CW_KEY_INPUT & CW_KEY_FLAG_USB_PORT)
        && (s_last_handkey_ptt
            || (GPIO_IsPttPressed() && s_KeyerFSMState != CWK_STATE_ACTIVE_ELEMENT
                && s_bug_state != BUG_STATE_DIT_ELEMENT && s_bug_state != BUG_STATE_DAH_HOLD))) {
        action = ptt_action();
        // The release debounce is shared with the bug's dah: don't hand it a stale one
        if (!s_last_handkey_ptt)
            s_handkey_release_pending = false;
        return action;
    }

    // Semi-automatic bug keyer runs its own FSM
    if (gEeprom.CW_KEYER_MODE == CW_IAMBIC_MODE_BUG) {
        return CW_HandleBugState();
    }

    // Input struct - will be sampled at appropriate times in each state
    CW_Input in = {0};

    switch (s_KeyerFSMState) {
    ///
    ///  IDLE
    ///
    case CWK_STATE_IDLE:
        // IDLE: Sample continuously for any key press
        CW_ReadKeys(&in);
#if CW_KEYER_DEBUG
        if (in.dit || in.dah) {
            char buf[60];
            sprintf_(buf, "%u IDLE sees dit=%d dah=%d\r\n", (unsigned)cur_count, in.dit, in.dah);
            UART_Send(buf, strlen(buf));
        }
        static uint32_t idle_count = 0;
        if (idle_count++ % 3000 == 0)
            UART_Send("keyer is idle\r\n", 15);
#endif
        if (in.dit || in.dah) {
            if (in.dit && !in.dah) {
                s_active_is_dit = true;
            } else if (in.dah && !in.dit) {
                s_active_is_dit = false;
            } else if (gEeprom.CW_KEYER_MODE == CW_KEYER_MODE_ULTIMATIC) {
                // both pressed -> send whichever paddle was pressed last
                s_active_is_dit = !in.last_is_dah;
            } else {
                // both pressed -> toggle previous element type (dit->dah, dah->dit)
                s_active_is_dit = !s_active_is_dit;
            }

            s_pending_alternate = false;
            s_elem_start_count = cur_count;
            s_elem_deadline_extra_ms = AUDIO_IsAudioPathOn() ? 0 : 20;
            s_KeyerFSMState = CWK_STATE_ACTIVE_ELEMENT;
#if CW_KEYER_DEBUG
            UART_Send("keyer going active\r\n", 20);
#endif
            action = CW_ACTION_CARRIER_ON;
        }
        break;

    ///
    ///  ACTIVE
    ///
    case CWK_STATE_ACTIVE_ELEMENT:
        const uint32_t target = s_active_is_dit ? s_dit_count : s_dah_count;
        const uint32_t elapsed_elem = millis_since(s_elem_start_count);

#if CW_KEYER_DEBUG
        {
            static uint32_t debug_count = 0;
            if (++debug_count % 10 == 0) {  // Print every 10th iteration
                char buf[80];
                sprintf_(buf, "elem: elapsed=%u target=%u start=%u cur=%u\r\n", 
                         elapsed_elem, target, s_elem_start_count, (unsigned)cur_count);
                UART_Send(buf, strlen(buf));
            }
        }
#endif
        // Sample paddles for memory logic - skip if we already know alternate is pending.
        if(!s_pending_alternate)
        {
            CW_ReadKeys(&in);

            // Memory logic: depends on keyer mode and element type.
            if (gEeprom.CW_KEYER_MODE == CW_IAMBIC_MODE_A || gEeprom.CW_KEYER_MODE == CW_KEYER_MODE_ULTIMATIC) {
                // Type A (and Ultimatic): edge detection for opposite paddle throughout
                // element, so a brief tap that's released before this element ends is
                // still remembered and fires next instead of being silently dropped.
                if (s_active_is_dit && in.dah_rise) {
                    s_pending_alternate = true;
                } else if (!s_active_is_dit && in.dit_rise) {
                    s_pending_alternate = true;
                }
            } else if (gEeprom.CW_KEYER_MODE == CW_IAMBIC_MODE_B) {
                // "Elecraft style" Type B with an edge-trigger during the first third of a dah
                // (so holding the alternate on the way into the element won't trigger it)
                if ((!s_active_is_dit) && (elapsed_elem < s_dit_count)) {
                    if (in.dit_rise) {
                        s_pending_alternate = true;
                    }
                } else {
                    // Standard Type B logic: state detection
                    if (s_active_is_dit && in.dah) {
                        s_pending_alternate = true;
                    } else if (!s_active_is_dit && in.dit) {
                        s_pending_alternate = true;
                    }
                }
            }
        }

        if (elapsed_elem >= target + s_elem_deadline_extra_ms) {
            s_elem_deadline_extra_ms = 0;
            action = CW_ACTION_CARRIER_OFF;
            s_elem_start_count = cur_count;
#if CW_KEYER_DEBUG
            {
                char dbg3[96];
                sprintf_(dbg3, "ELEMENT_END: cur=%u target=%u set s_elem_start=%u\r\n", cur_count, target, s_elem_start_count);
                UART_Send(dbg3, strlen(dbg3));
            }
#endif
            // Emit element to encoder on state exit
            CW_EncoderProcessElement(s_active_is_dit ? CW_ELEMENT_DIT : CW_ELEMENT_DAH);
            s_KeyerFSMState = CWK_STATE_INTER_ELEMENT_GAP;
        } else {
            // Carrier is on and should remain on during element
            action = CW_ACTION_CARRIER_HOLD_ON;
        }
        break;

    ///
    ///   ELEMENT GAP
    ///
    case CWK_STATE_INTER_ELEMENT_GAP: {
        const uint32_t elapsed_gap = millis_since(s_elem_start_count);
        
        // Read only if needed
        if(!s_pending_alternate)
        {
            // keep doing sampling during gap for memory logic
            CW_ReadKeys(&in);

            // Mode A style edge detection for opposite key throughout element AND gap,
            // for iambic A/B and Ultimatic alike, so a brief tap released before the
            // gap ends is still remembered instead of being silently dropped.
            if (s_active_is_dit && in.dah_rise) {
                s_pending_alternate = true;
            } else if (!s_active_is_dit && in.dit_rise) {
                s_pending_alternate = true;
            }
        }
        if (elapsed_gap >= s_gap_count) {

            bool next_is_dit = false;
            bool have_next = false;

            // alternating is already decided
            if(s_pending_alternate)
            {
                if(!s_active_is_dit)
                    next_is_dit = true;
                have_next = true;
            }
            else  // no pending alternate
            {            
                // Gap complete with no alternate pending - take a sample
                CW_ReadKeys(&in);

                if (in.dit || in.dah) {
                    // Explicit handling: only create a dit if dit was actually pressed.
                    // If both paddles are pressed, choose the opposite of the prior element:
                    // prior dit -> choose dah, otherwise choose dit.
                    if (in.dit && !in.dah) {
                        next_is_dit = true;
                    } else if (in.dah && !in.dit) {
                        next_is_dit = false;
                    } else if (gEeprom.CW_KEYER_MODE == CW_KEYER_MODE_ULTIMATIC) {
                        // both pressed -> send whichever paddle was pressed last
                        next_is_dit = !in.last_is_dah;
                    } else { /* both pressed -> choose opposite of previous */
                        next_is_dit = !s_active_is_dit;
                    }
                    have_next = true;
                }
            } 

            s_pending_alternate = false;

            if (have_next) {
                s_elem_start_count = cur_count;  // start new count for the new element
                s_KeyerFSMState = CWK_STATE_ACTIVE_ELEMENT;
                s_active_is_dit = next_is_dit;
                // Sample now and log if this creates a dit while only dah is pressed
                CW_ReadKeys(&in);
                action = CW_ACTION_CARRIER_ON;				
            } else {
                // No key input - transition to inter-char gap (carry over timing)
                s_KeyerFSMState = CWK_STATE_INTER_CHAR_GAP;
            }
        }
		break;
	}

    ///
    ///    CHAR GAP
    ///
	case CWK_STATE_INTER_CHAR_GAP: {
            const uint32_t elapsed_gap = millis_since(s_elem_start_count);

		if (elapsed_gap < s_char_gap_count) {  // until char gap complete

            CW_ReadKeys(&in);
            bool have_key = (in.dit || in.dah);

            if (elapsed_gap < s_ext_gap_count) {
			    // Early period: immediate send, same character continues
                if (have_key) {
                    // If both pressed: choose the opposite of the prior element (if prior was dit, send dah; otherwise send dit)
                    if (in.dit && !in.dah) {
                        s_active_is_dit = true;
                    } else if (in.dah && !in.dit) {
                        s_active_is_dit = false;
                    } else if (gEeprom.CW_KEYER_MODE == CW_KEYER_MODE_ULTIMATIC) {
                        // both pressed -> send whichever paddle was pressed last
                        s_active_is_dit = !in.last_is_dah;
                    } else { // both pressed -> toggle previous
                        s_active_is_dit = !s_active_is_dit;
                    }

                    s_elem_start_count = cur_count;
                    s_KeyerFSMState = CWK_STATE_ACTIVE_ELEMENT;
                    action = CW_ACTION_CARRIER_ON;
                }
		    } else {
			    // Hold period (ext_gap <= elapsed < char_gap): queue key but wait for char_gap deadline
			    if (have_key && !s_pending_alternate) {
				    s_pending_alternate = true;
				    if (gEeprom.CW_KEYER_MODE == CW_KEYER_MODE_ULTIMATIC && in.dit && in.dah) {
				        // both pressed -> send whichever paddle was pressed last
				        s_active_is_dit = !in.last_is_dah;
				    } else {
				        s_active_is_dit = in.dit;
				    }
			    }
            }

		} else {
			// Char gap complete - character boundary reached
            CW_EncoderProcessElement(CW_ELEMENT_INTER_CHAR_SPACE);
#if CW_KEYER_DEBUG
            {
                char dbg4[96];
                const uint32_t now = millis();
                sprintf_(dbg4, "CHAR_GAP_COMPLETE: now=%u s_elem_start=%u elapsed=%u char_gap=%u\r\n", now, s_elem_start_count, millis_since(s_elem_start_count), s_char_gap_count);
                UART_Send(dbg4, strlen(dbg4));
            }
#endif
			if (s_pending_alternate) {
				// Send queued key now because the gap is over
				s_pending_alternate = false;
				s_elem_start_count = cur_count;
				s_KeyerFSMState = CWK_STATE_ACTIVE_ELEMENT;
				action = CW_ACTION_CARRIER_ON;
			} else {
				s_KeyerFSMState = CWK_STATE_INTER_WORD_GAP;
			}
		}
		break;
	}

    ///
    ///   WORD GAP
    ///
	case CWK_STATE_INTER_WORD_GAP: {
        // Post char-gap: monitor and send immediately, or goto idle at word_gap
        const uint32_t elapsed_gap = millis_since(s_elem_start_count);
        // Debug: log elapsed vs configured word gap and current paddle state
#if CW_KEYER_DEBUG
        {
            char dbg[80];
            sprintf_(dbg, "INTER_WORD_GAP: elapsed=%u word=%u dit=%d dah=%d\r\n", elapsed_gap, s_word_gap_count, in.dit, in.dah);
            UART_Send(dbg, strlen(dbg));
        }
#endif
        CW_ReadKeys(&in);
		
        if (in.dit || in.dah) {
            if (gEeprom.CW_KEYER_MODE == CW_KEYER_MODE_ULTIMATIC && in.dit && in.dah) {
                // both pressed -> send whichever paddle was pressed last
                s_active_is_dit = !in.last_is_dah;
            } else {
                s_active_is_dit = in.dit;
            }
			s_elem_start_count = cur_count;
			s_KeyerFSMState = CWK_STATE_ACTIVE_ELEMENT;
			action = CW_ACTION_CARRIER_ON;
            } else if (elapsed_gap >= s_word_gap_count) {
#if CW_KEYER_DEBUG
                {
                    char dbg2[80];
                    sprintf_(dbg2, "INTER_WORD_GAP: triggering INTER_WORD_SPACE (elapsed=%u >= %u)\r\n", elapsed_gap, s_word_gap_count);
                    UART_Send(dbg2, strlen(dbg2));
                }
#endif
                CW_EncoderProcessElement(CW_ELEMENT_INTER_WORD_SPACE);
                s_KeyerFSMState = CWK_STATE_IDLE;
            }
		break;
	}

	default:
		s_KeyerFSMState = CWK_STATE_IDLE;
		break;
	}

	return action;
}
