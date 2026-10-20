/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * USB-C paddle port and paddle keyer,
 * Copyright 2026 Johan Denoyer F4WAT
 * https://github.com/jdenoy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * CW Keyer - self-contained A1A-style manual keyer overlay.
 *
 * PTT is always a straight key. The paddle port is either SIDE1/SIDE2 or a
 * paddle on the USB-C connector (D+ = dit, D- = dah, closed to ground). Modes:
 * STRAIGHT and PADDLE (AUTO legacy, IAMB A or IAMB B with opposite-paddle
 * memory). get_key() reports one key only, so a squeeze is only visible
 * on the USB port, whose two contacts are read together from GPIOA_IDR; on
 * SIDE1/SIDE2 the paddle mode behaves as a single-paddle keyer.
 *
 * USB port: on entry the app sets PA11/PA12 to inputs with pull-up and stops
 * the USB clock (a powered USB device holds D+ for about two seconds after a
 * contact closes). Only those bits are touched, under masked IRQs, and they are
 * put back on exit, so the PC link works again once the app is closed.
 *
 * Element timing is measured on the SysTick counter rather than by counting
 * 1 ms waits: each wait iteration also scans the keypad (about 0.2 ms), which
 * would otherwise stretch every element by roughly 20 %.
 *
 * The BK4829 VRAMP PA-bias byte in REG_36 is stepped across four milliseconds
 * at each edge. The board PA GPIO is enabled before the rise and disabled after
 * the fall. TX modulation remains muted throughout a session; the BK4829
 * alarm-audio route provides a local sidetone without modulating the carrier.
 */

#include <stdbool.h>
#include <stdint.h>
#include "../app_api.h"
#include "cwkeyer_assets.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define CFG_MAGIC_V4       0xCAu   /* 0.2: crystal correction in centi-ppm */
#define CFG_MAGIC          0xCBu   /* 0.4: crystal correction in REG_3B steps */
#define WPM_DEFAULT        18u
#define WPM_MIN             5u
#define WPM_MAX            60u
#define CAL_DEFAULT        14   /* REG_3B steps: 14 x 0.1923 = +2.69 ppm */
#define CAL_MIN           -50
#define CAL_MAX            50
#define REG_XTAL         0x3Bu
#define RX_PITCH_DEFAULT  700u
#define RX_PITCH_MIN      300u
#define RX_PITCH_MAX      900u
#define RX_PITCH_STEP      50u
#define BREAKIN_DEFAULT   300u
#define BREAKIN_MIN       250u
#define BREAKIN_MAX      1500u
#define BREAKIN_STEP       50u
#define TUNE_STEP_10HZ      10
#define TUNE_MAX_10HZ     5000
#define UI_DEBOUNCE_MS      20u
#define NAV_REPEAT_DELAY_MS 400u
#define NAV_REPEAT_MS       100u
#define RAMP_MS              4u
#define PLL_LOCK_MS           2u
#define MARK_MAX_MS      10000u
#define SERVICE_MS          10u
#define BATTERY_MS         500u
#define F_X                  72u
#define DEBOUNCE_POLLS        3u   /* straight key: ~3.5 ms of stable reading */

_Static_assert(1200u / WPM_MAX > 2u * RAMP_MS,
               "CW mark must remain longer than both RF ramps");

#define REG_TX_LINK        0x30u
#define REG_AM_CTRL        0x31u
#define REG_PA_BIAS        0x36u
#define REG_FREQ_LOW       0x38u
#define REG_FREQ_HIGH      0x39u
#define REG_AUDIO_FILTER   0x3Du
#define REG_AUDIO_GAIN     0x48u
#define REG_SUBAUDIO       0x51u
#define REG_AUDIO_1        0x54u
#define REG_AUDIO_2        0x55u
#define REG_AFC            0x73u
#define REG_RX_FILTER_1    0x2Au
#define REG_RX_FILTER_2    0x2Bu
#define REG_RX_FILTER_3    0x2Fu
#define REG_TONE           0x70u
#define REG_TONE_FREQ      0x71u
#define REG_RX_LEVEL       0x42u
#define AF_ALAM              2u
#define AF_USB               5u
#define TX_LINK_OFF      0x0000u
#define TX_LINK_NO_MIC   0xC3FAu
#define TX_LINK_NO_PA    0xC3F2u
#define SIDETONE_GAIN        76u
#define TONE1_CONFIG      (0x8000u | (SIDETONE_GAIN << 8))
#define AUDIO_GAIN_BASE_MASK 0xFFF0u
#define AUDIO_DAC_GAIN_MAX     15u

/* These values are already returned by get_key(), but API level 2 only names
 * codes through PTT. They mirror KEY_SIDE2=17 and KEY_SIDE1=18. */
#define KEY_SIDE2          17u
#define KEY_SIDE1          18u

enum { MODE_STRAIGHT, MODE_PADDLE };
enum { PORT_SIDE, PORT_USB };
#define GPIOA_MODER (*(volatile uint32_t *)0x50000000u)
#define GPIOA_PUPDR (*(volatile uint32_t *)0x5000000Cu)
#define GPIOA_IDR   (*(volatile uint32_t *)0x50000010u)
#define RCC_APBENR1 (*(volatile uint32_t *)0x4002103Cu)
#define SYST_LOAD   (*(volatile uint32_t *)0xE000E014u)
#define SYST_VAL    (*(volatile uint32_t *)0xE000E018u)
#define USBEN       (1u << 23)
#define PIN_DM      11u   /* PA11 USB D-, adapter ring  */
#define PIN_DP      12u   /* PA12 USB D+, adapter tip   */
#define USB_PINS    (0xFu << (2u * PIN_DM))   /* PA11/PA12 MODER and PUPDR */
#define USB_PULLUP  (0x5u << (2u * PIN_DM))
#define P_DIT 1u
#define P_DAH 2u

typedef struct {
    uint8_t magic, wpm, mode, reverse;
    int16_t calSteps;
    uint16_t rxPitchHz, breakInMs;
    uint8_t speaker, port;
    uint8_t keyer, keyerTag; /* offsets 12/13; legacy prefix unchanged */
} config_t;

_Static_assert(sizeof(config_t) <= 16u, "CW Keyer config exceeds overlay slot");
_Static_assert(__builtin_offsetof(config_t, keyer) == 12u && sizeof(config_t) == 14u,
               "Config must preserve the legacy 12-byte prefix");

static struct {
    const app_api_t *A;
    bool running, txReady, keyDown, denied, fArm, redraw, statusRedraw;
    bool uiHandled;
    bool cfgDirty;
    bool reverse, speaker, portOpen;
    uint8_t wpm, mode, port, uiCandidate, paBias, paControl;
    uint8_t padRaw, padStable, padPolls;
    uint8_t keyer, element, memory; /* AUTO/A/B; element bit 2 = mark */
    uint32_t moder, pupdr, apbenr1;   /* saved USB-port bits only */
    uint32_t ticksPerMs, lastVal;
    int16_t calSteps, tuneOffset10Hz;
    uint16_t rxPitchHz, breakInMs, xtalBase;
    uint16_t serviceMs, batteryMs, markMs, hangElapsedMs;
    uint32_t uiCandidateAt, navRepeatAt;
} g;

#define A (g.A)

static char *text(char *buf, uint16_t offset)
{
    A->asset_read(offset, buf, TEXT_MAX);
    return buf;
}

static uint8_t slen(const char *s)
{
    uint8_t n = 0u;
    while (s[n])
        n++;
    return n;
}

static char *put(char *out, const char *s)
{
    while (*s)
        *out++ = *s++;
    return out;
}

static char *putu(char *out, uint32_t value)
{
    char tmp[10];
    uint8_t n = 0u;
    do {
        const uint64_t qr = A->uidivmod(value, 10u);
        tmp[n++] = (char)('0' + (uint32_t)(qr >> 32));
        value = (uint32_t)qr;
    } while (value);
    while (n)
        *out++ = tmp[--n];
    return out;
}

static char *putCalibration(char *out)
{
    int16_t value = (int16_t)(uint32_t)A->idivmod(g.calSteps * 1923 + (g.calSteps < 0 ? -50 : 50), 100);
    *out++ = value < 0 ? '-' : '+';
    uint16_t magnitude = (uint16_t)(value < 0 ? -value : value);
    const uint64_t qr = A->uidivmod(magnitude, 100u);
    out = putu(out, (uint32_t)qr);
    *out++ = '.';
    uint8_t fraction = (uint8_t)(qr >> 32);
    uint8_t tens = 0u;
    while (fraction >= 10u) {
        fraction -= 10u;
        tens++;
    }
    *out++ = (char)('0' + tens);
    *out++ = (char)('0' + fraction);
    return out;
}

/* The crystal correction is a signed count of BK4829 REG_3B steps (5 Hz on
 * the 26 MHz reference, 0.1923 ppm each). Positive means the radio's measured
 * carrier is high: the chip is told its crystal is faster, which lowers every
 * RX and TX frequency in proportion. The display shows it in ppm. */
static uint32_t tunedFrequency(uint32_t frequency)
{
    const int16_t offset = g.tuneOffset10Hz;
    if (offset < 0) {
        const uint16_t delta = (uint16_t)-offset;
        return frequency > delta ? frequency - delta : 0u;
    }
    const uint16_t delta = (uint16_t)offset;
    return frequency <= 0xFFFFFFFFu - delta ? frequency + delta : 0xFFFFFFFFu;
}

static void setAudioGain(uint16_t base, uint8_t gain)
{
    A->bk_write(REG_AUDIO_GAIN, (uint16_t)(base | gain));
}

static void fadeAudioGain(uint16_t base, bool up)
{
    for (uint8_t step = 1u; step <= AUDIO_DAC_GAIN_MAX; step++) {
        const uint8_t gain = up ? step : (uint8_t)(AUDIO_DAC_GAIN_MAX - step);
        setAudioGain(base, gain);
        A->delay_ms(1u);
    }
}

static void configureCwRx(bool enableAudio)
{
    uint32_t frequency = tunedFrequency(A->rx_freq());
    const uint32_t pitch = (uint32_t)A->uidivmod(g.rxPitchHz, 10u);
    if (frequency > pitch)
        frequency -= pitch;

    A->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    A->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));
    const uint16_t rxLink = A->bk_read(REG_TX_LINK);
    A->bk_write(REG_TX_LINK, TX_LINK_OFF);
    A->bk_write(REG_TX_LINK, rxLink);

    /* Use the BK4829 USB baseband output as a CW product detector. Tuning the
     * receiver below the carrier produces the configured receive tone. */
    A->bk_write(REG_AM_CTRL, A->bk_read(REG_AM_CTRL) & 0xFFFEu);
    static const uint16_t rxProfile[][2] = {
        { REG_RX_LEVEL, 0x6B5Au }, { REG_RX_FILTER_1, 0x7400u },
        { REG_RX_FILTER_2, 0u }, { REG_RX_FILTER_3, 0x9890u },
        { REG_AUDIO_1, 0x9009u }, { REG_AUDIO_2, 0x31A9u },
        { REG_AUDIO_FILTER, 0u },
    };
    for (uint8_t i = 0u; i < 7u; i++)
        A->bk_write((uint8_t)rxProfile[i][0], rxProfile[i][1]);
    A->bk_write(REG_AFC, A->bk_read(REG_AFC) | 0x0010u);
    A->set_agc(true);
    A->set_af(AF_USB);
    const uint16_t gainBase = A->bk_read(REG_AUDIO_GAIN) & AUDIO_GAIN_BASE_MASK;
    setAudioGain(gainBase, enableAudio ? AUDIO_DAC_GAIN_MAX : 0u);
    /* CW must not use the FM squelch gate: its opening delay clips dits and
     * repeatedly chops the recovered tone at every carrier transition. */
    A->audio_path(enableAudio && g.speaker);
}

static void returnToRx(void)
{
    /* Fade the AF DAC before toggling AUDIO_PA_EN, then restore it only after
     * the USB output and the amplifier have both settled. */
    uint16_t gainBase = A->bk_read(REG_AUDIO_GAIN) & AUDIO_GAIN_BASE_MASK;
    if (g.speaker)
        fadeAudioGain(gainBase, false);
    else
        setAudioGain(gainBase, 0u);
    A->set_af(AF_USB);
    A->delay_ms(20u);
    A->audio_path(false);
    A->delay_ms(10u);
    A->tx_end();
    g.txReady = false;
    g.hangElapsedMs = 0u;
    configureCwRx(false);
    if (g.speaker) {
        A->delay_ms(20u);
        A->audio_path(true);
        A->delay_ms(20u);
        fadeAudioGain(gainBase, true);
    } else {
        setAudioGain(gainBase, AUDIO_DAC_GAIN_MAX);
    }
    g.statusRedraw = true;
}

static void formatFrequency(char *out)
{
    char *end = putu(out, tunedFrequency(A->tx_freq()));
    uint8_t len = (uint8_t)(end - out);
    *end = '\0';
    if (len <= 5u)
        return;
    for (uint8_t i = (uint8_t)(len + 1u); i > len - 5u; i--)
        out[i] = out[i - 1u];
    out[len - 5u] = '.';
}

static uint16_t dotMs(void)
{
    return (uint16_t)(uint32_t)A->uidivmod(1200u, g.wpm);
}

static bool isSide(uint8_t key)
{
    return key == KEY_SIDE1 || key == KEY_SIDE2;
}

static void drawConfigTag(const char *s, bool right, uint8_t line)
{
    A->print_inverse(s, right ? 87u : 2u, line, false, true,
                     right ? 127u : 42u);
}

static void drawStatus(uint16_t title, uint8_t titleChars)
{
    A->status_clear();
    char t[TEXT_MAX];
    A->print_inverse(text(t, title), 2u, 0u, true, true,
                     (uint8_t)(2u + titleChars * 4u));
    A->print_tiny(APP_VERSION, 92u, 1u, true, false);
    A->draw_battery();
}

static void drawBigFrequency(char *s)
{
    formatFrequency(s);
    const uint8_t n = slen(s);
    if (n < 3u) {
        A->print_normal(s, (uint8_t)((128u - n * 7u) >> 1), 127u, 1u);
        return;
    }
    const uint8_t width = (uint8_t)((n - 3u) * 13u + 17u);
    const uint8_t x = (uint8_t)((128u - width) >> 1);
    char tail[3] = { s[n - 2u], s[n - 1u], '\0' };
    s[n - 2u] = '\0';
    A->display_freq(s, x, 0u, false);
    /* End must stay zero as in APRS; a larger End re-centers the small tail. */
    A->print_normal(tail, (uint8_t)(x + (n - 3u) * 13u + 3u), 0u, 1u);
}

static void drawMainStatus(void)
{
    char t[TEXT_MAX];
    drawStatus(T_TITLE, T_TITLE_CHARS);
    if (g.denied || !g.txReady) {
        text(t, g.denied ? T_DENIED : T_READY);
        A->print_inverse(t, 43u, 0u, true, true,
                         (uint8_t)(43u + slen(t) * 4u));
    }
    if (g.fArm)
        A->asset_read(BMP_F, A->status_line + F_X, BMP_F_LEN);
    else if (g.speaker)
        A->asset_read(BMP_SPEAKER, A->status_line + F_X, BMP_SPEAKER_LEN);
}

static void drawMain(void)
{
    char s[24], right[24], t[TEXT_MAX];
    drawMainStatus();

    drawBigFrequency(s);

    char *out = put(s, text(t, T_MODE + (g.mode ? 1u + g.keyer : 0u) * T_MODE_STRIDE));
    *out = '\0';
    out = put(right, text(t, T_SPEED));
    out = putu(out, g.wpm);
    *out = '\0';
    drawConfigTag(s, false, 4u);
    drawConfigTag(right, true, 4u);

    out = put(s, text(t, T_RX_PITCH));
    out = putu(out, g.rxPitchHz);
    *out = '\0';
    out = put(right, text(t, T_BREAKIN));
    out = putu(out, g.breakInMs);
    *out = '\0';
    drawConfigTag(s, false, 5u);
    drawConfigTag(right, true, 5u);

    out = put(s, text(t, T_PORT + g.port * T_PORT_STRIDE));
    out = put(out, g.reverse ? "DA-DI" : "DI-DA");
    *out = '\0';
    out = put(right, text(t, T_XTAL));
    out = putCalibration(out);
    *out = '\0';
    drawConfigTag(s, false, 6u);
    drawConfigTag(right, true, 6u);
}

static void draw(void)
{
    A->display_clear();
    drawMain();
    A->blit_status();
    A->blit_full();
    g.redraw = false;
    g.statusRedraw = false;
}

static void redrawMainStatus(void)
{
    drawMainStatus();
    A->blit_status();
    g.statusRedraw = false;
}

static void writeBias(uint8_t bias)
{
    A->bk_write(REG_PA_BIAS, (uint16_t)((uint16_t)bias << 8) | g.paControl);
}

static uint16_t toneControlWord(uint16_t frequency)
{
    return (uint16_t)((frequency * 1353245u + (1u << 16)) >> 17);
}

static void sidetone(bool on)
{
    A->bk_write(REG_TONE_FREQ, on ? toneControlWord(g.rxPitchHz) : 0u);
}

static void observe(uint8_t key);

static void rampDelay(void)
{
    A->delay_ms(1u);
    if (g.keyer && g.element && g.port == PORT_USB)
        observe(APP_KEY_INVALID);
}

static void rampUp(void)
{
    const uint8_t quarter = (uint8_t)(g.paBias >> 2);
    const uint8_t half = (uint8_t)(g.paBias >> 1);
    A->bk_write(REG_TX_LINK, TX_LINK_NO_MIC);
    A->tx_carrier(true);
    writeBias(quarter);                         rampDelay();
    writeBias(half);                            rampDelay();
    writeBias((uint8_t)(g.paBias - quarter));  rampDelay();
    writeBias(g.paBias);                        rampDelay();
}

static void rampDown(void)
{
    const uint8_t quarter = (uint8_t)(g.paBias >> 2);
    const uint8_t half = (uint8_t)(g.paBias >> 1);
    writeBias((uint8_t)(g.paBias - quarter));  rampDelay();
    writeBias(half);                            rampDelay();
    writeBias(quarter);                         rampDelay();
    writeBias(0u);                              rampDelay();
    A->tx_carrier(false);
    A->bk_write(REG_TX_LINK, TX_LINK_NO_PA);
}

static uint32_t irqSave(void)
{
    uint32_t primask;
    __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
    return primask;
}

static void irqRestore(uint32_t primask)
{
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

/* open: pull-up, then input mode, then USB clock off, as validated on the bench
 * with Key Probe 0.3; close puts the saved bits back. Read-modify-write of the
 * named bits only, IRQs masked. */
static void portSet(bool open)
{
    if (open == g.portOpen || (open && g.port != PORT_USB))
        return;
    const uint32_t irq = irqSave();
    if (open) {
        g.moder = GPIOA_MODER & USB_PINS;
        g.pupdr = GPIOA_PUPDR & USB_PINS;
        g.apbenr1 = RCC_APBENR1 & USBEN;
    }
    GPIOA_PUPDR = (GPIOA_PUPDR & ~USB_PINS) | (open ? USB_PULLUP : g.pupdr);
    GPIOA_MODER = (GPIOA_MODER & ~USB_PINS) | (open ? 0u : g.moder);
    RCC_APBENR1 = (RCC_APBENR1 & ~USBEN) | (open ? 0u : g.apbenr1);
    irqRestore(irq);
    g.portOpen = open;
}

/* CPU clock ticks since the previous call, from the 10 ms SysTick down-counter.
 * Valid while calls are less than 10 ms apart. */
static uint32_t ticksSince(void)
{
    const uint32_t now = SYST_VAL;
    const uint32_t prev = g.lastVal;
    g.lastVal = now;
    return prev >= now ? prev - now : prev + SYST_LOAD + 1u - now;
}

/* Both contacts are sampled together, so a squeeze is visible. */
static uint8_t paddles(uint8_t key)
{
    uint8_t p = 0u;
    if (g.port == PORT_USB) {
        const uint32_t a = ~GPIOA_IDR;
        if (a & (1u << PIN_DP)) p = P_DIT;
        if (a & (1u << PIN_DM)) p |= P_DAH;
    } else if (key == KEY_SIDE1) {
        p = P_DIT;
    } else if (key == KEY_SIDE2) {
        p = P_DAH;
    }
    /* A straight key keys on the tip only, so a mono plug (ring shorted to
     * sleeve) works; the two SIDE keys stay parallel straight keys. */
    if (g.mode == MODE_STRAIGHT)
        return g.port == PORT_SIDE ? (uint8_t)(p != 0u) : (uint8_t)(p & P_DIT);
    return g.reverse ? (uint8_t)(((p & 1u) << 1) | (p >> 1)) : p;
}

/* A contact must read the same on DEBOUNCE_POLLS consecutive polls before the
 * straight-key and keyer-start decisions see it. */
static uint8_t debounced(uint8_t p)
{
    if (p != g.padRaw) {
        g.padRaw = p;
        g.padPolls = 0u;
    } else if (g.padPolls < DEBOUNCE_POLLS) {
        g.padPolls++;
    } else {
        g.padStable = p;
    }
    return g.padStable;
}

/* A remembers isolated opposite taps. B additionally remembers a squeeze
 * during the mark, including both ramps. Squeeze during the space alone does
 * not queue a tail. Consuming a queued element clears the memory. */
static void observe(uint8_t key)
{
    const uint8_t p = paddles(key);
    /* Two consecutive samples for active memory; the slower legacy debounce
     * remains in place for starting a sequence and for straight keying. */
    if (p != g.padRaw) {
        g.padRaw = p;
        return;
    }
    const uint8_t opposite = (g.element & 3u) ^ 3u;
    if (p == opposite || (p == 3u && g.keyer == 2u && (g.element & 4u)))
        g.memory = opposite;
}

static bool prepareTx(void)
{
    if (A->tx_state() != 0u) {
        g.denied = true;
        g.redraw = true;
        return false;
    }

    A->audio_path(false);
    A->tx_set_params();
    const uint16_t pa = A->bk_read(REG_PA_BIAS);
    g.paBias = (uint8_t)(pa >> 8);
    g.paControl = (uint8_t)pa;

    /* tx_set_params is atomic and returns with RF enabled. Remove PA drive
     * immediately, apply the calibrated frequency, then start every mark with
     * the same controlled ramp. REG_38/39 use 10 Hz synthesizer units. */
    A->tx_carrier(false);
    writeBias(0u);

    /* REG_50 mutes modulation, not the RF carrier. Keep it muted for the whole
     * keying session, remove every modulation source, and disable the mic ADC.
     * RF is keyed with internal PA gain, external PA bias and PA_ENABLE. */
    A->tx_mute(true);
    A->bk_write(REG_SUBAUDIO, 0u);
    A->bk_write(REG_TONE, 0u);
    const uint32_t frequency = tunedFrequency(A->tx_freq());
    A->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    A->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));

    /* REG_38/39 are written after tx_set_params, so restart the TX link to
     * recalibrate the VCO and latch the corrected synthesizer frequency. Keep
     * internal PA gain disabled when the link comes back up. */
    A->bk_write(REG_TX_LINK, TX_LINK_OFF);
    A->bk_write(REG_TX_LINK, TX_LINK_NO_PA);
    A->delay_ms(PLL_LOCK_MS);

    /* AF_ALAM is the BK4829's local alarm/tone output. Unlike AF_BEEP, it
     * remains audible while REG_50 keeps the TX modulation path muted. Keep
     * Tone1 enabled and key it through REG_71; rewriting REG_70 on every Morse
     * edge produces an audible pop. */
    A->bk_write(REG_TONE, TONE1_CONFIG);
    sidetone(false);
    A->set_af(AF_ALAM);
    A->audio_path(g.speaker);
    g.txReady = true;
    g.denied = false;
    g.statusRedraw = true;
    return true;
}

static bool keyOn(void)
{
    if (g.txReady && A->tx_state() != 0u) {
        returnToRx();
        g.denied = true;
        return false;
    }
    if (!g.txReady && !prepareTx())
        return false;
    sidetone(true);
    rampUp();
    g.keyDown = true;
    g.markMs = 0u;
    g.hangElapsedMs = 0u;
    return true;
}

static void keyOff(void)
{
    rampDown();
    sidetone(false);
    g.keyDown = false;
}

static void disarm(void)
{
    if (g.keyDown)
        keyOff();
    if (g.txReady)
        returnToRx();
    g.denied = false;
    g.statusRedraw = true;
}

static void serviceOneMs(void)
{
    A->delay_ms(1u);
    if (g.keyDown)
        g.hangElapsedMs = 0u;
    if (g.keyDown && ++g.markMs >= MARK_MAX_MS) {
        keyOff();
        if (g.txReady)
            returnToRx();
        g.denied = true;
        g.statusRedraw = true;
    }
    if (g.txReady && !g.keyDown && ++g.hangElapsedMs >= g.breakInMs) {
        returnToRx();
    }
    if (++g.serviceMs >= SERVICE_MS) {
        g.serviceMs = 0u;
        A->backlight_update();
        g.batteryMs = (uint16_t)(g.batteryMs + SERVICE_MS);
        if (g.batteryMs >= BATTERY_MS) {
            g.batteryMs = 0u;
            if (!g.txReady) {
                A->battery_sample();
                g.statusRedraw = true;
            }
            /* A sleeping overlay consumes the first SIDE press as a wake key.
             * Keep the keyer awake so a dit or dah is never discarded. */
            A->backlight_on();
        }
    }
}

/* Element wait: observe A/B on every scan; AUTO keeps its legacy behavior.
 * EXIT aborts the wait and the caller safely ramps the carrier down. */
static bool keyerWait(uint16_t ms)
{
    uint32_t left = ms * g.ticksPerMs;
    ticksSince();
    for (;;) {
        const uint8_t key = A->get_key();
        if (key == APP_KEY_EXIT) {
            g.running = false;
            return false;
        }
        if (g.keyer)
            observe(key);
        const uint32_t spent = ticksSince();
        if (spent >= left)
            return g.running;
        left -= spent;
        /* Poll without sleeping for the last millisecond: overshoot stays
         * below one keypad scan instead of a whole 1 ms step. */
        if (left > g.ticksPerMs)
            serviceOneMs();
    }
}

static void runPaddle(uint8_t p)
{
    uint8_t last = 0u;
    while (p) {
        const uint8_t el = p != (P_DIT | P_DAH) ? p : last == P_DIT ? P_DAH : P_DIT;
        g.element = el | 4u;
        g.memory = 0u;
        if (!keyOn())
            break;
        const uint16_t dot = dotMs();
        /* The PA bias ramps take RAMP_MS at each edge and cross half drive
         * about one millisecond off centre, so the waits are trimmed to give a
         * 1:1 mark/space at the half-drive points and an exact 2-dot period. */
        const bool done = keyerWait((uint16_t)((el == P_DAH ? 3u * dot : dot) - RAMP_MS - 1u));
        keyOff();
        g.element = el;
        if (!done || !keyerWait((uint16_t)(dot - RAMP_MS + 1u)))
            break;
        last = el;
        p = g.keyer && g.memory ? g.memory : paddles(A->get_key());
    }
    g.element = 0u;
}

static bool adjustFrequency(int8_t direction)
{
    if (direction > 0) {
        if (g.tuneOffset10Hz > TUNE_MAX_10HZ - TUNE_STEP_10HZ)
            return false;
        g.tuneOffset10Hz += TUNE_STEP_10HZ;
    } else if (direction < 0) {
        if (g.tuneOffset10Hz < -TUNE_MAX_10HZ + TUNE_STEP_10HZ)
            return false;
        g.tuneOffset10Hz -= TUNE_STEP_10HZ;
    } else {
        return false;
    }

    if (g.txReady)
        returnToRx();
    else
        configureCwRx(true);
    g.denied = false;
    return true;
}

static void adjustSetting(uint8_t key, int8_t direction)
{
    bool retune = false;
    switch (key) {
        case APP_KEY_0:
            g.speaker = !g.speaker;
            A->audio_path(g.speaker);
            break;
        case APP_KEY_1:
            g.mode = (uint8_t)(g.mode ^ 1u);
            break;
        case APP_KEY_8:
            g.keyer = g.keyer == 2u ? 0u : g.keyer + 1u;
            break;
        case APP_KEY_7:
            portSet(false);
            g.port = (uint8_t)(g.port ^ 1u);
            portSet(true);
            break;
        case APP_KEY_2:
            if (direction > 0) {
                if (g.wpm >= WPM_MAX)
                    return;
                g.wpm++;
            } else {
                if (g.wpm <= WPM_MIN)
                    return;
                g.wpm--;
            }
            break;
        case APP_KEY_3:
            if (direction > 0 && g.rxPitchHz <= RX_PITCH_MAX - RX_PITCH_STEP) {
                g.rxPitchHz = (uint16_t)(g.rxPitchHz + RX_PITCH_STEP);
            } else if (direction < 0 &&
                       g.rxPitchHz >= RX_PITCH_MIN + RX_PITCH_STEP) {
                g.rxPitchHz = (uint16_t)(g.rxPitchHz - RX_PITCH_STEP);
            } else {
                return;
            }
            retune = true;
            break;
        case APP_KEY_4:
            if (direction > 0 && g.breakInMs <= BREAKIN_MAX - BREAKIN_STEP) {
                g.breakInMs = (uint16_t)(g.breakInMs + BREAKIN_STEP);
            } else if (direction < 0 &&
                       g.breakInMs >= BREAKIN_MIN + BREAKIN_STEP) {
                g.breakInMs = (uint16_t)(g.breakInMs - BREAKIN_STEP);
            } else {
                return;
            }
            break;
        case APP_KEY_5:
            g.reverse = !g.reverse;
            break;
        case APP_KEY_6:
            if (direction > 0) {
                if (g.calSteps >= CAL_MAX)
                    return;
                g.calSteps++;
            } else {
                if (g.calSteps <= CAL_MIN)
                    return;
                g.calSteps--;
            }
            A->bk_write(REG_XTAL, (uint16_t)(g.xtalBase + g.calSteps));
            retune = true;
            break;
        default:
            return;
    }
    if (retune) {
        if (g.txReady)
            returnToRx();
        else
            configureCwRx(true);
    }
    g.cfgDirty = true;
    g.redraw = true;
}

static void handleUiKey(uint8_t key)
{
    int8_t direction = A->nav_dir(key);
    if (direction != 0) {
        if (adjustFrequency(direction))
            g.redraw = true;
        g.fArm = false;
        g.statusRedraw = true;
        return;
    }

    switch (key) {
        case APP_KEY_EXIT:
            g.running = false;
            break;
        case APP_KEY_F:
            g.fArm = !g.fArm;
            g.statusRedraw = true;
            break;
        case APP_KEY_0: case APP_KEY_1: case APP_KEY_2: case APP_KEY_3:
        case APP_KEY_4: case APP_KEY_5: case APP_KEY_6: case APP_KEY_7: case APP_KEY_8:
            adjustSetting(key, g.fArm ? -1 : 1);
            g.fArm = false;
            g.statusRedraw = true;
            break;
        default:
            break;
    }
}

static void resetUiInput(void)
{
    g.uiCandidate = APP_KEY_INVALID;
    g.uiHandled = false;
    g.navRepeatAt = 0u;
}

static void handleUiInput(uint8_t key)
{
    const uint32_t now = A->ticks_ms();
    const int8_t direction = A->nav_dir(key);
    if (key != g.uiCandidate) {
        g.uiCandidate = key;
        g.uiCandidateAt = now;
        g.uiHandled = false;
        g.navRepeatAt = 0u;
        return;
    }
    if (!g.uiHandled) {
        if ((uint32_t)(now - g.uiCandidateAt) < UI_DEBOUNCE_MS)
            return;
        A->backlight_on();
        handleUiKey(key);
        g.uiHandled = true;
        if (direction != 0)
            g.navRepeatAt = now + NAV_REPEAT_DELAY_MS;
        return;
    }
    if (direction != 0 && (int32_t)(now - g.navRepeatAt) >= 0) {
        A->backlight_on();
        handleUiKey(key);
        g.navRepeatAt = now + NAV_REPEAT_MS;
    }
}

static void loadConfig(void)
{
    config_t cfg;
    A->cfg_load((uint8_t *)&cfg, sizeof(cfg));
    if (cfg.magic == CFG_MAGIC_V4) {
        /* 0.2 config: no port byte, correction in centi-ppm. */
        cfg.port = PORT_SIDE;
        cfg.calSteps = (int16_t)(uint32_t)A->idivmod(
            cfg.calSteps * 100 + (cfg.calSteps < 0 ? -961 : 961), 1923);
        cfg.magic = CFG_MAGIC;
    }
    if (cfg.magic == CFG_MAGIC &&
        cfg.wpm >= WPM_MIN && cfg.wpm <= WPM_MAX &&
        cfg.mode <= MODE_PADDLE && cfg.port <= PORT_USB && cfg.reverse <= 1u) {
        g.wpm = cfg.wpm;
        g.mode = cfg.mode;
        g.reverse = cfg.reverse;
        g.port = cfg.port;
        g.keyer = cfg.keyerTag == 0xD3u && cfg.keyer <= 2u ? cfg.keyer : 0u;
        g.calSteps = cfg.calSteps >= CAL_MIN && cfg.calSteps <= CAL_MAX
            ? cfg.calSteps : CAL_DEFAULT;
    } else {
        g.wpm = WPM_DEFAULT;
        g.mode = MODE_STRAIGHT;
        g.reverse = false;
        g.calSteps = CAL_DEFAULT;
    }
    g.rxPitchHz = cfg.magic == CFG_MAGIC &&
        cfg.rxPitchHz >= RX_PITCH_MIN && cfg.rxPitchHz <= RX_PITCH_MAX
        ? cfg.rxPitchHz : RX_PITCH_DEFAULT;
    g.breakInMs = cfg.magic == CFG_MAGIC &&
        cfg.breakInMs >= BREAKIN_MIN && cfg.breakInMs <= BREAKIN_MAX
        ? cfg.breakInMs : BREAKIN_DEFAULT;
    g.speaker = cfg.magic == CFG_MAGIC && cfg.speaker <= 1u
        ? cfg.speaker : true;
}

static void saveConfig(void)
{
    const config_t cfg = {
        CFG_MAGIC, g.wpm, g.mode, (uint8_t)g.reverse, g.calSteps,
        g.rxPitchHz, g.breakInMs, (uint8_t)g.speaker, g.port, g.keyer, 0xD3u
    };
    if (g.cfgDirty)
        A->cfg_save((const uint8_t *)&cfg, sizeof(cfg));
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    g.keyer = 0u;
    g.cfgDirty = false;
    g.element = 0u;
    g.running = true;
    g.redraw = true;
    g.statusRedraw = false;
    resetUiInput();
    loadConfig();
    portSet(true);
    g.ticksPerMs = (uint32_t)A->uidivmod(SYST_LOAD + 1u, 10u);
    A->backlight_on();
    g.xtalBase = A->bk_read(REG_XTAL);
    A->bk_write(REG_XTAL, (uint16_t)(g.xtalBase + g.calSteps));
    configureCwRx(true);
    draw();

    while (g.running) {
        uint8_t key = A->get_key();
        if (key == APP_KEY_SAVER) {
            resetUiInput();
            serviceOneMs();
            continue;
        }
        if (key == APP_KEY_WAKE)
            key = APP_KEY_INVALID;

        /* A denied key-down is latched until every keying control is released;
         * otherwise a held PTT would call tx_state and repaint every millisecond. */
        if (g.denied) {
            if (key == APP_KEY_PTT || isSide(key) || paddles(key)) {
                resetUiInput();
                serviceOneMs();
                continue;
            }
            g.denied = false;
            g.statusRedraw = true;
        }

        const uint8_t pad = debounced(paddles(key));
        const bool straight =
            (key == APP_KEY_PTT || (g.mode == MODE_STRAIGHT && pad));
        if (straight) {
            resetUiInput();
            if (!g.keyDown)
                keyOn();
        } else if (g.keyDown) {
            keyOff();
            resetUiInput();
        } else if (g.mode != MODE_STRAIGHT && pad) {
            resetUiInput();
            A->backlight_on();
            runPaddle(pad);
            g.padRaw = g.padStable = g.padPolls = 0u;   /* no stale restart */
        } else if (key != APP_KEY_INVALID) {
            handleUiInput(key);
        } else {
            resetUiInput();
        }

        if (g.redraw && !g.keyDown)
            draw();
        else if (g.statusRedraw && !g.keyDown)
            redrawMainStatus();
        serviceOneMs();
    }

    disarm();
    A->bk_write(REG_XTAL, g.xtalBase);
    portSet(false);
    saveConfig();
    A->set_af(APP_AF_MUTE);
    A->audio_path(false);
}
