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
 * STRAIGHT and PADDLE (automatic dits and dahs, alternating on a squeeze, no
 * paddle memory). get_key() reports one key only, so a squeeze is only visible
 * on the USB port, whose two contacts are read together from GPIOA_IDR; on
 * SIDE1/SIDE2 the paddle mode behaves as a single-paddle keyer.
 *
 * USB port: on entry the app sets PA11/PA12 to inputs with pull-up and stops
 * the USB clock (a powered USB device holds D+ for about two seconds after a
 * contact closes). Only those bits are touched, under masked IRQs, and they are
 * put back on exit, so the PC link works again once the app is closed.
 * After about 0.5 s without contact, the radio board cuts a closing contact
 * off from the MCU: it reads closed for a few ms, then open for about 200 ms
 * while still held (measured with a plain wire too, USB core in reset, no
 * TX). So from receive, a first paddle press only arms: TX is prepared,
 * nothing is sent, the paddles are ignored for ARM_HOLDOFF_MS, then each
 * element starts on the first closed reading. The break-in time must exceed
 * the word spaces, or each word starts with an arming press again.
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

#define CFG_MAGIC          0xCBu   /* 0.4: crystal correction in REG_3B steps;
                                      0.2 used 0xCA, no longer converted */
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
/* The S-meter is the main-screen RSSI line (ui/main.c DisplayRSSIBar):
 * dBm, S reading and 13 bars, 7 rows high. It is drawn on line 2, then
 * moved down by 4 rows to sit in the 17 free rows between the frequency
 * (down to y = 14) and the settings capsules (from y = 32): y = 20 to 26. */
#define SMETER_LINE          2u
#define SMETER_MAX_LEVEL    13u   /* S1-S9, then S9+10 to S9+40 */
#define SMETER_TEXT_X       38u
#define SMETER_BAR_X        62u
#define SMETER_DBM_MAX     -53    /* S9+40, as on the main screen */
#define SMETER_DBM_START  -160    /* below the BK4829 floor: S0 */
#define SMETER_FALL_DB       3    /* per update: one S unit every 200 ms */
#define SMETER_EVERY_MS    100u
#define BATTERY_MS         500u
#define F_X                  72u
#define TAG_LEFT_X            2u
#define TAG_RIGHT_X          87u
#define HELP_RIGHT_X         79u
#define DEBOUNCE_POLLS        3u   /* straight key: ~3.5 ms of stable reading */
#define RX_SETTLE_MS         50u   /* RX return: REG_30 cycle to amplifier on */
#define AMP_SETTLE_MS        10u   /* TX: AF output settle before the amplifier */
#define CONFIRM_CYCLES     9600u   /* paddle start: second read 200 us later */
#define ARM_HOLDOFF_MS      300u   /* arming press: paddles ignored this long */
#define ARM_WAIT_MS        3000u   /* armed, no mark yet: back to RX after this */

_Static_assert(SMETER_BAR_X + SMETER_MAX_LEVEL * 5u <= 128u,
               "CW S-meter bars exceed the display width");
_Static_assert(SMETER_DBM_MAX + 93 == (SMETER_MAX_LEVEL - 9) * 10,
               "CW S-meter dBm cap must end on the last bar");
_Static_assert(1200u / WPM_MAX > 2u * RAMP_MS,
               "CW mark must remain longer than both RF ramps");

#define REG_TX_LINK        0x30u
#define REG_AM_CTRL        0x31u
#define REG_PA_BIAS        0x36u
#define REG_FREQ_LOW       0x38u
#define REG_FREQ_HIGH      0x39u
#define REG_AUDIO_GAIN     0x48u
#define REG_SUBAUDIO       0x51u
#define REG_AFC            0x73u
#define REG_TONE           0x70u
#define REG_TONE_FREQ      0x71u
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
} config_t;

_Static_assert(sizeof(config_t) <= 16u, "CW Keyer config exceeds overlay slot");

/* Field order follows the Thumb-1 load/store immediate ranges: bytes first
 * (offsets up to 31), then halfwords (up to 62), then words (up to 124). A
 * field past its range costs an extra instruction at every access. */
static struct {
    const app_api_t *A;
    bool running, txReady, keyDown, denied, fArm, redraw, statusRedraw;
    bool uiHandled, help, armWait;
    bool reverse, speaker, portOpen;
    uint8_t wpm, mode, port, uiCandidate, paBias, paControl;
    uint8_t padRaw, padStable, padPolls;
    uint8_t smMs, ampMs;   /* ampMs: amplifier on in that many service ms */
    int16_t calSteps, tuneOffset10Hz;
    uint16_t rxPitchHz, breakInMs, xtalBase;
    uint16_t serviceMs, batteryMs, markMs, hangElapsedMs, armMs;
    uint32_t moder, pupdr, apbenr1;   /* saved USB-port bits only */
    uint32_t ticksPerMs, lastVal;
    int32_t smDbm;   /* held reading, capped at SMETER_DBM_MAX */
    uint32_t uiCandidateAt, navRepeatAt;
    uint32_t exitTicks;   /* EXIT read for this long in the keyer waits */
} g;

#define A (g.A)

static char *text(char *buf, uint16_t offset)
{
    A->asset_read(offset, buf, TEXT_MAX);
    return buf;
}

/* Appends an asset text read straight into the output; returns its end,
 * which is already NUL-terminated. Needs TEXT_MAX bytes free at out. */
static char *putText(char *out, uint16_t offset)
{
    A->asset_read(offset, out, TEXT_MAX);
    while (*out)
        out++;
    return out;
}

/* The resident formatter (API level 3). One local stub keeps every call a
 * plain bl: the service lies past the ldr immediate range of the table. */
__attribute__((noinline))
static char *putu(char *out, uint32_t value)
{
    return A->append_u32(out, value);
}

/* The correction in ppm with two decimals, rounded half away from zero:
 * |steps| x 0.1923. It stays below 10 ppm, so magnitude + 1000 prints as
 * four digits, "1" then the units and the two decimals: the units digit
 * moves over the "1" and the dot takes its place. */
_Static_assert((CAL_MAX * 1923 + 50) / 100 < 1000 &&
               (-(CAL_MIN) * 1923 + 50) / 100 < 1000,
               "XTAL correction must stay below 10 ppm");

static char *putCalibration(char *out)
{
    const int16_t steps = g.calSteps;
    *out++ = steps < 0 ? '-' : '+';
    const uint32_t magnitude = (uint32_t)A->uidivmod(
        (uint32_t)(steps < 0 ? -steps : steps) * 1923u + 50u, 100u);
    char *end = putu(out, magnitude + 1000u);
    out[0] = out[1];
    out[1] = '.';
    return end;
}

/* The crystal correction is a signed count of BK4829 REG_3B steps (5 Hz on
 * the 26 MHz reference, 0.1923 ppm each). Positive means the radio's measured
 * carrier is high: the chip is told its crystal is faster, which lowers every
 * RX and TX frequency in proportion. The display shows it in ppm. */

/* The VFO frequency moved by the UP/DOWN offset, in 10 Hz units. The
 * offset stays within +/-TUNE_MAX_10HZ (50 kHz) and the VFO never goes below
 * 18 MHz, so the sum can neither wrap below zero nor overflow. */
static uint32_t tunedFrequency(uint32_t frequency)
{
    return frequency + (uint32_t)(int32_t)g.tuneOffset10Hz;
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
    const app_api_t *const a = A;   /* one load: calls may change g */
    uint32_t frequency = tunedFrequency(a->rx_freq());
    const uint32_t pitch = (uint32_t)a->uidivmod(g.rxPitchHz, 10u);
    if (frequency > pitch)
        frequency -= pitch;

    a->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    a->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));
    const uint16_t rxLink = a->bk_read(REG_TX_LINK);
    a->bk_write(REG_TX_LINK, TX_LINK_OFF);
    a->bk_write(REG_TX_LINK, rxLink);

    /* Use the BK4829 USB baseband output as a CW product detector. Tuning the
     * receiver below the carrier produces the configured receive tone. */
    a->bk_write(REG_AM_CTRL, a->bk_read(REG_AM_CTRL) & 0xFFFEu);
    /* The RX level, filter and audio register / value pairs (RX_PROFILE in
     * gen_assets.py). */
    uint16_t rxProfile[RX_PROFILE_LEN / 4u][2];
    a->asset_read(RX_PROFILE, rxProfile, sizeof(rxProfile));
    for (uint8_t i = 0u; i < RX_PROFILE_LEN / 4u; i++)
        a->bk_write((uint8_t)rxProfile[i][0], rxProfile[i][1]);
    a->bk_write(REG_AFC, a->bk_read(REG_AFC) | 0x0010u);
    a->set_agc(true);
    a->set_af(AF_USB);
    const uint16_t gainBase = a->bk_read(REG_AUDIO_GAIN) & AUDIO_GAIN_BASE_MASK;
    setAudioGain(gainBase, enableAudio ? AUDIO_DAC_GAIN_MAX : 0u);
    /* CW must not use the FM squelch gate: its opening delay clips dits and
     * repeatedly chops the recovered tone at every carrier transition. */
    a->audio_path(enableAudio && g.speaker);
}

/* The amplifier goes off before anything moves the AF output: a gain fade
 * with the amplifier on steps the AF_ALAM DC level (a click), and so does
 * the REG_30 cycle in configureCwRx. */
static void speakerOff(uint16_t gainBase)
{
    A->audio_path(false);
    setAudioGain(gainBase, 0u);
}

/* After configureCwRx(false): the AF output re-centres (as in EPIRB 406),
 * then the amplifier comes back and the gain fades in. */
static void speakerOn(uint16_t gainBase)
{
    const app_api_t *const a = A;
    a->delay_ms(RX_SETTLE_MS);
    a->audio_path(true);
    a->delay_ms(20u);
    fadeAudioGain(gainBase, true);
}

static uint16_t gainBase(void)
{
    return A->bk_read(REG_AUDIO_GAIN) & AUDIO_GAIN_BASE_MASK;
}

static void returnToRx(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    const uint16_t base = gainBase();
    if (g.speaker)
        speakerOff(base);
    else
        setAudioGain(base, 0u);
    a->set_af(AF_USB);
    a->delay_ms(10u);
    a->tx_end();
    g.txReady = false;
    g.ampMs = 0u;
    g.hangElapsedMs = 0u;
    g.armWait = false;
    g.armMs = 0u;
    configureCwRx(false);
    if (g.speaker)
        speakerOn(base);
    else
        setAudioGain(base, AUDIO_DAC_GAIN_MAX);
    g.statusRedraw = true;
}

/* A receive retune (pitch, crystal, offset) with the same muting. */
static void retuneRx(void)
{
    if (g.txReady) {
        returnToRx();
        return;
    }
    if (!g.speaker) {
        configureCwRx(true);
        return;
    }
    const uint16_t base = gainBase();
    speakerOff(base);
    configureCwRx(false);
    speakerOn(base);
}

static uint16_t dotMs(void)
{
    return (uint16_t)(uint32_t)A->uidivmod(1200u, g.wpm);
}

static bool isSide(uint8_t key)
{
    return key == KEY_SIDE1 || key == KEY_SIDE2;
}

/* A capsule of the two-column layouts, from x to the end of its column: the
 * main screen's right column starts at TAG_RIGHT_X, the help page's wider
 * one at HELP_RIGHT_X. */
static void drawConfigTag(const char *s, uint8_t x, uint8_t line)
{
    A->print_inverse(s, x, line, false, true, x < 64u ? 42u : 127u);
}

static void drawStatus(uint16_t title, uint8_t titleChars)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    a->status_clear();
    char t[TEXT_MAX];
    a->print_inverse(text(t, title), 2u, 0u, true, true,
                     (uint8_t)(2u + titleChars * 4u));
    a->print_tiny(text(t, T_VERSION), 92u, 1u, true, false);
    a->draw_battery();
}

/* The main screen's big digits through the resident draw_frequency (API
 * level 3), centred: with d digits, the big part, its dot and the small
 * last two digits are (d - 2) x 13 + 17 = 13 d - 9 pixels wide. The
 * digits are counted by formatting the frequency into s. At 1 GHz and
 * above the service prints the whole frequency in the big text font. */
static void drawBigFrequency(char *s)
{
    const uint32_t frequency = tunedFrequency(A->tx_freq());
    const uint32_t digits = (uint32_t)(putu(s, frequency) - s);
    A->draw_frequency(frequency, (uint8_t)((137u - digits * 13u) >> 1), 0u);
}

static void drawMainStatus(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    char t[TEXT_MAX];
    drawStatus(T_TITLE, T_TITLE_CHARS);
    /* The label slot shows HELP on the help page (MENU disarms, so the
     * transmitter is never ready there), TX OFF after a refusal, else RX. */
    if (g.denied || !g.txReady) {
        const bool help = g.help, denied = g.denied;
        text(t, help ? T_HELP : denied ? T_DENIED : T_READY);
        a->print_inverse(t, 43u, 0u, true, true,
                         (uint8_t)(43u + (help ? T_HELP_LEN - 1u
                                          : denied ? T_DENIED_LEN - 1u
                                                   : T_READY_LEN - 1u) * 4u));
    }
    /* F is never armed on the help page; it shows no speaker icon. */
    if (g.fArm)
        a->asset_read(BMP_F, a->status_line + F_X, BMP_F_LEN);
    else if (g.speaker && !g.help)
        a->asset_read(BMP_SPEAKER, a->status_line + F_X, BMP_SPEAKER_LEN);
}

/* S-meter scale of the main screen: S9 at -93 dBm, 6 dB per S-unit below
 * it, one more bar per 10 dB above it. The reading is capped at
 * SMETER_DBM_MAX, so the level never exceeds SMETER_MAX_LEVEL. The
 * divisions are multiply-shifts, exact on the ranges they see: x / 10 is
 * (x * 13) >> 7 for x <= 40 (S9 to S9+40), x / 6 is (x * 43) >> 8 for
 * x <= 53 (below S9). */
static uint8_t smeterLevel(int dbm)
{
    if (dbm >= -93)
        return (uint8_t)(9 + (((dbm + 93) * 13) >> 7));
    const int x = dbm + 147;
    return x > 0 ? (uint8_t)((x * 43) >> 8) : 0u;
}

/* The main-screen RSSI line: "% 4d dBm" in the 3x5 font, then S0-S9 or
 * +10 to +40, then 13 bars, hollow above S9. Line 2 is cleared and drawn,
 * then its rows are split across lines 2 and 3 (4 rows down). */
static void drawSmeter(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    uint8_t *p = a->fb[SMETER_LINE];
    uint8_t *q = a->fb[SMETER_LINE + 1u];
    for (uint8_t i = 0u; i < 128u; i++)
        p[i] = 0u;

    const int dbm = g.smDbm;
    char s[TEXT_MAX + 5u];
    char *out = s;
    if (dbm > -100)
        *out++ = ' ';
    *out++ = '-';
    text(putu(out, (uint32_t)-dbm), T_DBM);
    a->print_tiny(s, 2u, SMETER_LINE * 8u + 1u, false, true);

    const uint8_t level = smeterLevel(dbm);
    out = s;
    if (level <= 9u) {
        *out++ = 'S';
        *out++ = (char)('0' + level);
    } else {
        *out++ = '+';
        out = putu(out, (uint32_t)(dbm + 93));
    }
    *out = '\0';
    a->print_normal(s, SMETER_TEXT_X, 0u, SMETER_LINE);

    for (uint8_t i = 0u; i < level; i++) {
        uint8_t *bar = p + SMETER_BAR_X + i * 5u;
        bar[0] = bar[3] = 0x3Eu;
        bar[1] = bar[2] = i < 9u ? 0x3Eu : 0x22u;
    }

    for (uint8_t i = 0u; i < 128u; i++) {
        q[i] = (uint8_t)(p[i] >> 4);
        p[i] = (uint8_t)(p[i] << 4);
    }
}

/* Every SMETER_EVERY_MS while receiving. A CW signal comes and goes with each
 * element, so the reading rises at once and falls by SMETER_FALL_DB per
 * update. Only the two S-meter lines go to the display. */
static void serviceSmeter(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    int dbm = a->rssi_dbm();
    if (dbm > SMETER_DBM_MAX)
        dbm = SMETER_DBM_MAX;
    const int fall = g.smDbm - SMETER_FALL_DB;
    if (dbm < fall)
        dbm = fall;
    if (dbm != g.smDbm) {
        g.smDbm = dbm;
        drawSmeter();
        a->blit_line(SMETER_LINE);
        a->blit_line(SMETER_LINE + 1u);
    }
}

static void drawMain(void)
{
    /* Room for a port label followed by a TEXT_MAX asset read. */
    char s[TEXT_MAX + 8u], right[TEXT_MAX + 8u];
    drawMainStatus();

    drawBigFrequency(s);

    putText(s, T_MODE + g.mode * T_MODE_STRIDE);
    char *out = putText(right, T_SPEED);
    out = putu(out, g.wpm);
    *out = '\0';
    drawConfigTag(s, TAG_LEFT_X, 4u);
    drawConfigTag(right, TAG_RIGHT_X, 4u);

    out = putText(s, T_RX_PITCH);
    out = putu(out, g.rxPitchHz);
    *out = '\0';
    out = putText(right, T_BREAKIN);
    out = putu(out, g.breakInMs);
    *out = '\0';
    drawConfigTag(s, TAG_LEFT_X, 5u);
    drawConfigTag(right, TAG_RIGHT_X, 5u);

    out = putText(s, T_PORT + g.port * T_PORT_STRIDE);
    putText(out, (uint16_t)(T_ORDER + g.reverse * T_ORDER_STRIDE));
    out = putText(right, T_XTAL);
    out = putCalibration(out);
    *out = '\0';
    drawConfigTag(s, TAG_LEFT_X, 6u);
    drawSmeter();
    drawConfigTag(right, TAG_RIGHT_X, 6u);
}

/* MENU: the keys in two columns of capsules, as on CW Decode. Keying is
 * ignored while it is shown. */
static void drawHelp(void)
{
    char t[TEXT_MAX];
    drawMainStatus();
    /* T_HELP_KEYS: the left column (lines 1 to 6), then the right one. */
    for (uint8_t i = 0u; i < 12u; i++) {
        if (text(t, T_HELP_KEYS + i * T_HELP_KEYS_STRIDE)[0])
            drawConfigTag(t, i < 6u ? TAG_LEFT_X : HELP_RIGHT_X,
                          (uint8_t)(i < 6u ? i + 1u : i - 5u));
    }
    A->print_tiny(text(t, T_HELP_STEP), 56u, 49u, false, true);
}

static void draw(void)
{
    A->display_clear();
    if (g.help)
        drawHelp();
    else
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

/* PA bias in four 1 ms steps, 1/4, 1/2, 3/4 and full drive going up, the
 * same levels the other way down to 0. */
static void rampBias(bool up)
{
    const uint8_t quarter = (uint8_t)(g.paBias >> 2);
    const uint8_t level[5] = { 0u, quarter, (uint8_t)(g.paBias >> 1),
                               (uint8_t)(g.paBias - quarter), g.paBias };
    for (uint8_t i = 1u; i <= 4u; i++) {
        writeBias(level[up ? i : 4u - i]);
        A->delay_ms(1u);
    }
}

static void rampUp(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    a->bk_write(REG_TX_LINK, TX_LINK_NO_MIC);
    a->tx_carrier(true);
    rampBias(true);
}

static void rampDown(void)
{
    rampBias(false);
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

/* Called by keyOn or the arming press once tx_state has allowed the
 * transmission. */
static void prepareTx(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    a->audio_path(false);
    /* tx_prepare (API level 3, table of 368 B) leaves PA_ENABLE off and skips
     * the FM waits; tx_set_params returns with RF enabled after a 10 ms
     * unshaped burst. Either way, remove PA drive, apply the calibrated
     * frequency, then start every mark with the same controlled ramp.
     * REG_38/39 use 10 Hz synthesizer units. */
    if (a->api_size >= 368u)
        a->tx_prepare();
    else
        a->tx_set_params();
    const uint16_t pa = a->bk_read(REG_PA_BIAS);
    g.paBias = (uint8_t)(pa >> 8);
    g.paControl = (uint8_t)pa;

    a->tx_carrier(false);
    writeBias(0u);

    /* REG_50 mutes modulation, not the RF carrier. Keep it muted for the whole
     * keying session, remove every modulation source, and disable the mic ADC.
     * RF is keyed with internal PA gain, external PA bias and PA_ENABLE. */
    a->tx_mute(true);
    a->bk_write(REG_SUBAUDIO, 0u);
    a->bk_write(REG_TONE, 0u);
    const uint32_t frequency = tunedFrequency(a->tx_freq());
    a->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    a->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));

    /* REG_38/39 are written after tx_set_params, so restart the TX link to
     * recalibrate the VCO and latch the corrected synthesizer frequency. Keep
     * internal PA gain disabled when the link comes back up. */
    a->bk_write(REG_TX_LINK, TX_LINK_OFF);
    a->bk_write(REG_TX_LINK, TX_LINK_NO_PA);
    a->delay_ms(PLL_LOCK_MS);

    /* AF_ALAM is the BK4829's local alarm/tone output. Unlike AF_BEEP, it
     * remains audible while REG_50 keeps the TX modulation path muted. Keep
     * Tone1 enabled and key it through REG_71; rewriting REG_70 on every Morse
     * edge produces an audible pop. */
    a->bk_write(REG_TONE, TONE1_CONFIG);
    sidetone(false);
    a->set_af(AF_ALAM);
    /* The TX setup leaves the AF output moving: the amplifier comes on
     * AMP_SETTLE_MS later, from serviceOneMs, while the mark goes on. */
    g.ampMs = AMP_SETTLE_MS;
    g.txReady = true;
    g.denied = false;
    g.statusRedraw = true;
}

/* tx_state is asked before every mark: a refusal ends a prepared session,
 * or leaves the receiver as it is when none was prepared. */
static bool keyOn(void)
{
    if (A->tx_state() != 0u) {
        if (g.txReady)
            returnToRx();
        else
            g.redraw = true;
        g.denied = true;
        return false;
    }
    if (!g.txReady)
        prepareTx();
    sidetone(true);
    rampUp();
    g.armWait = false;
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
    const app_api_t *const a = A;   /* one load: calls may change g */
    a->delay_ms(1u);
    if (g.ampMs && --g.ampMs == 0u)
        a->audio_path(g.speaker);
    if (g.armMs)
        g.armMs--;
    /* hangElapsedMs needs no reset here: keyOn() zeroes it and it only counts
     * while the key is up. */
    if (g.keyDown && ++g.markMs >= MARK_MAX_MS) {
        keyOff();
        if (g.txReady)
            returnToRx();
        g.denied = true;
        g.statusRedraw = true;
    }
    if (g.txReady && !g.keyDown &&
        ++g.hangElapsedMs >= (g.armWait ? ARM_WAIT_MS : g.breakInMs)) {
        returnToRx();
    }
    if (++g.serviceMs >= SERVICE_MS) {
        g.serviceMs = 0u;
        a->backlight_update();
        /* Receiving only: no RX while the carrier is up or during break-in.
         * The help page covers the S-meter lines. */
        if (!g.txReady && !g.help &&
            (g.smMs = (uint8_t)(g.smMs + SERVICE_MS)) >= SMETER_EVERY_MS) {
            g.smMs = 0u;
            serviceSmeter();
        }
        g.batteryMs = (uint16_t)(g.batteryMs + SERVICE_MS);
        if (g.batteryMs >= BATTERY_MS) {
            g.batteryMs = 0u;
            if (!g.txReady) {
                a->battery_sample();
                g.statusRedraw = true;
            }
            /* A sleeping overlay consumes the first SIDE press as a wake key.
             * Keep the keyer awake so a dit or dah is never discarded. */
            a->backlight_on();
        }
    }
}

/* Element wait. The paddles are only looked at once the space is over (no
 * paddle memory). EXIT aborts once it has been read for UI_DEBOUNCE_MS of
 * waiting: while a SIDE contact bounces, the keypad scan can miss the zero
 * column and report another key of the same row (SIDE1: MENU, UP, DOWN or
 * EXIT; SIDE2: 1, 2, 3 or STAR), and a single such reading must not end
 * the app. */
static bool keyerWait(uint16_t ms)
{
    uint32_t left = ms * g.ticksPerMs;
    ticksSince();
    for (;;) {
        const uint8_t key = A->get_key();
        const uint32_t spent = ticksSince();
        if (key != APP_KEY_EXIT) {
            g.exitTicks = 0u;
        } else if ((g.exitTicks += spent) >= UI_DEBOUNCE_MS * g.ticksPerMs) {
            g.running = false;
            return false;
        }
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
    g.exitTicks = 0u;
    while (p) {
        const uint8_t el = p != (P_DIT | P_DAH) ? p : last == P_DIT ? P_DAH : P_DIT;
        if (!keyOn())
            return;
        const uint16_t dot = dotMs();
        /* The PA bias ramps take RAMP_MS at each edge and cross half drive
         * about one millisecond off centre, so the waits are trimmed to give a
         * 1:1 mark/space at the half-drive points and an exact 2-dot period. */
        const bool done = keyerWait((uint16_t)((el == P_DAH ? 3u * dot : dot) - RAMP_MS - 1u));
        keyOff();
        if (!done || !keyerWait((uint16_t)(dot - RAMP_MS + 1u)))
            return;
        last = el;
        p = paddles(A->get_key());
    }
}

/* One step up or down, refused when it would leave [lo, hi]: the bounded
 * settings and the frequency offset all follow this rule. */
static bool stepWithin(int16_t *value, int8_t direction, int16_t step,
                       int16_t lo, int16_t hi)
{
    const int16_t next = (int16_t)(*value + direction * step);
    if (next < lo || next > hi)
        return false;
    *value = next;
    return true;
}

static bool adjustFrequency(int8_t direction)
{
    if (!stepWithin(&g.tuneOffset10Hz, direction, TUNE_STEP_10HZ,
                    -TUNE_MAX_10HZ, TUNE_MAX_10HZ))
        return false;

    retuneRx();
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
        case APP_KEY_7:
            portSet(false);
            g.port = (uint8_t)(g.port ^ 1u);
            portSet(true);
            break;
        case APP_KEY_2: {
            int16_t wpm = g.wpm;
            if (!stepWithin(&wpm, direction, 1, WPM_MIN, WPM_MAX))
                return;
            g.wpm = (uint8_t)wpm;
            break; }
        case APP_KEY_3:
            if (!stepWithin((int16_t *)&g.rxPitchHz, direction, RX_PITCH_STEP,
                            RX_PITCH_MIN, RX_PITCH_MAX))
                return;
            retune = true;
            break;
        case APP_KEY_4:
            if (!stepWithin((int16_t *)&g.breakInMs, direction, BREAKIN_STEP,
                            BREAKIN_MIN, BREAKIN_MAX))
                return;
            break;
        case APP_KEY_5:
            g.reverse = !g.reverse;
            break;
        case APP_KEY_6:
            if (!stepWithin(&g.calSteps, direction, 1, CAL_MIN, CAL_MAX))
                return;
            A->bk_write(REG_XTAL, (uint16_t)(g.xtalBase + g.calSteps));
            retune = true;
            break;
        default:
            return;
    }
    if (retune)
        retuneRx();
    g.redraw = true;
}

static void handleUiKey(uint8_t key)
{
    if (g.help) {
        if (key == APP_KEY_EXIT || key == APP_KEY_MENU) {
            g.help = false;
            g.redraw = true;
        }
        return;
    }

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
        case APP_KEY_MENU:
            disarm();   /* ends a break-in hang: no keying behind the help */
            g.fArm = false;
            g.help = true;
            g.redraw = true;
            break;
        case APP_KEY_F:
            g.fArm = !g.fArm;
            g.statusRedraw = true;
            break;
        case APP_KEY_0: case APP_KEY_1: case APP_KEY_2: case APP_KEY_3:
        case APP_KEY_4: case APP_KEY_5: case APP_KEY_6: case APP_KEY_7:
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
    /* Any other magic, including a 0.2 config (0xCA), starts from the
     * defaults. */
    if (cfg.magic == CFG_MAGIC &&
        cfg.wpm >= WPM_MIN && cfg.wpm <= WPM_MAX &&
        cfg.mode <= MODE_PADDLE && cfg.port <= PORT_USB && cfg.reverse <= 1u) {
        g.wpm = cfg.wpm;
        g.mode = cfg.mode;
        g.reverse = cfg.reverse;
        g.port = cfg.port;
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
        g.rxPitchHz, g.breakInMs, (uint8_t)g.speaker, g.port
    };
    A->cfg_save((const uint8_t *)&cfg, sizeof(cfg));
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    g.running = true;
    g.redraw = true;
    g.statusRedraw = false;
    g.smDbm = SMETER_DBM_START;
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

        /* Keying is ignored while the help page is shown: PTT and the
         * paddle contacts then only reach the help key handler. */
        const uint8_t raw = paddles(key);
        uint8_t pad = debounced(raw);
        /* Paddle start on the first closed reading, confirmed 200 us later
         * (a spike is not a contact). Bounce no longer matters once an
         * element runs: the paddles are read again after the space. SIDE
         * keys come from the debounced keypad scan. */
        if (g.mode != MODE_STRAIGHT && raw && !pad) {
            if (g.port == PORT_USB) {
                const uint32_t t = A->cycles_48mhz();
                while (A->cycles_48mhz() - t < CONFIRM_CYCLES)
                    ;
            }
            if (paddles(key) == raw)
                pad = raw;
        }
        const bool keying = !g.help && !g.armMs;
        const bool straight = keying &&
            (key == APP_KEY_PTT || (g.mode == MODE_STRAIGHT && pad));
        if (straight) {
            resetUiInput();
            if (!g.keyDown)
                keyOn();
        } else if (g.keyDown) {
            keyOff();
            resetUiInput();
        } else if (keying && g.mode != MODE_STRAIGHT && pad) {
            resetUiInput();
            A->backlight_on();
            if (g.port == PORT_USB && !g.txReady) {
                /* Arming press. After a pause, the USB-C port reads a
                 * held contact as open for about 200 ms (radio board, not
                 * the paddle): this press only prepares TX, sends nothing
                 * and takes that window; the keying starts after it. */
                if (A->tx_state() != 0u) {
                    g.denied = true;
                    g.statusRedraw = true;
                } else {
                    prepareTx();
                    g.armWait = true;
                    g.armMs = ARM_HOLDOFF_MS;
                }
            } else {
                runPaddle(pad);
            }
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
