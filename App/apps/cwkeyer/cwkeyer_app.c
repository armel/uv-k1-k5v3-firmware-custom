/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
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
 * PTT is always a straight key. SIDE1/SIDE2 are either straight keys or a
 * single-paddle-at-a-time automatic keyer. The existing overlay ABI reports
 * only one key, so true iambic squeeze detection is intentionally not claimed.
 *
 * The BK4829 VRAMP PA-bias byte in REG_36 is stepped across four milliseconds
 * at each edge. The board PA GPIO is enabled before the rise and disabled after
 * the fall. Audio, tone and sub-audio paths remain muted throughout a session.
 */

#include <stdbool.h>
#include <stdint.h>
#include "../app_api.h"
#include "cwkeyer_assets.h"

#define CFG_MAGIC_V1       0xC7u
#define CFG_MAGIC_V2       0xC8u
#define CFG_MAGIC_V3       0xC9u
#define CFG_MAGIC          0xCAu
#define WPM_DEFAULT        18u
#define WPM_MIN             5u
#define WPM_MAX            40u
#define CAL_DEFAULT       277
#define CAL_MIN          -999
#define CAL_MAX           999
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
#define REG_RX_LEVEL       0x42u
#define AF_USB               5u
#define TX_LINK_OFF      0x0000u
#define TX_LINK_NO_MIC   0xC3FAu
#define TX_LINK_NO_PA    0xC3F2u

/* These values are already returned by get_key(), but API level 2 only names
 * codes through PTT. They mirror KEY_SIDE2=17 and KEY_SIDE1=18. */
#define KEY_SIDE2          17u
#define KEY_SIDE1          18u

enum { MODE_STRAIGHT, MODE_PADDLE };

typedef struct {
    uint8_t magic, wpm, mode, reverse;
    int16_t calCentiPpm;
    uint16_t rxPitchHz, breakInMs;
    uint8_t speaker, reserved;
} config_t;

_Static_assert(sizeof(config_t) <= 16u, "CW Keyer config exceeds overlay slot");

static struct {
    const app_api_t *A;
    bool running, txReady, keyDown, denied, fArm, redraw, statusRedraw;
    bool help, uiHandled;
    bool reverse, speaker;
    uint8_t wpm, mode, uiCandidate, paBias, paControl;
    int16_t calCentiPpm, tuneOffset10Hz;
    uint16_t rxPitchHz, breakInMs;
    uint16_t serviceMs, batteryMs, markMs, hangElapsedMs;
    uint32_t uiCandidateAt, navRepeatAt;
} g;

#define A (g.A)

static const uint32_t places[] = {
    1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
    10000u, 1000u, 100u, 10u, 1u
};

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
    bool started = false;
    for (uint8_t i = 0u; i < sizeof(places) / sizeof(places[0]); i++) {
        uint8_t digit = 0u;
        while (value >= places[i]) {
            value -= places[i];
            digit++;
        }
        if (digit || started || places[i] == 1u) {
            *out++ = (char)('0' + digit);
            started = true;
        }
    }
    return out;
}

static char *putCalibration(char *out)
{
    int16_t value = g.calCentiPpm;
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

/* Calibration is stored in hundredths of one ppm. Positive means the radio's
 * measured carrier is high, so the programmed synthesizer word is reduced. */
static uint32_t calibratedFrequency(uint32_t frequency)
{
    const int16_t signedCal = g.calCentiPpm;
    const uint16_t cal = (uint16_t)(signedCal < 0 ? -signedCal : signedCal);
    if (cal == 0u)
        return frequency;

    uint64_t qr = A->uidivmod(frequency, 100000u);
    const uint32_t wholeMHz = (uint32_t)qr;
    const uint32_t fraction = (uint32_t)(qr >> 32);

    qr = A->uidivmod(wholeMHz * cal, 1000u);
    uint32_t correction = (uint32_t)qr;
    const uint32_t tail = (uint32_t)(qr >> 32) * 100000u + fraction * cal;
    correction += (uint32_t)A->uidivmod(tail + 50000000u, 100000000u);

    if (signedCal > 0)
        return correction < frequency ? frequency - correction : 0u;
    return frequency + correction;
}

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

static void configureCwRx(void)
{
    uint32_t frequency = calibratedFrequency(tunedFrequency(A->rx_freq()));
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
    A->bk_write(REG_RX_LEVEL, 0x6B5Au);
    A->bk_write(REG_RX_FILTER_1, 0x7400u);
    A->bk_write(REG_RX_FILTER_2, 0x0000u);
    A->bk_write(REG_RX_FILTER_3, 0x9890u);
    A->bk_write(REG_AUDIO_1, 0x9009u);
    A->bk_write(REG_AUDIO_2, 0x31A9u);
    A->bk_write(REG_AUDIO_GAIN,
                (uint16_t)((A->bk_read(REG_AUDIO_GAIN) & 0xFFF0u) | 0x000Fu));
    A->bk_write(REG_AUDIO_FILTER, 0u);
    A->bk_write(REG_AFC, A->bk_read(REG_AFC) | 0x0010u);
    A->set_agc(true);
    A->set_af(AF_USB);
    /* CW must not use the FM squelch gate: its opening delay clips dits and
     * repeatedly chops the recovered tone at every carrier transition. */
    A->audio_path(g.speaker);
}

static void returnToRx(void)
{
    A->tx_end();
    g.txReady = false;
    g.hangElapsedMs = 0u;
    configureCwRx();
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

static uint8_t ditKey(void)
{
    return g.reverse ? KEY_SIDE2 : KEY_SIDE1;
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

    char *out = put(s, text(t, g.mode == MODE_PADDLE ? T_PADDLE : T_STRAIGHT));
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

    out = put(s, g.reverse ? "DAH-DIT" : "DIT-DAH");
    *out = '\0';
    out = put(right, text(t, T_XTAL));
    out = putCalibration(out);
    *out = '\0';
    drawConfigTag(s, false, 6u);
    drawConfigTag(right, true, 6u);
}

static void drawHelp(void)
{
    char t[TEXT_MAX];
    drawStatus(T_TITLE, T_TITLE_CHARS);
    A->print_inverse(text(t, T_HELP), 43u, 0u, true, true,
                     (uint8_t)(43u + slen(t) * 4u));
    for (uint8_t i = 0u; i < 6u; i++) {
        A->print_inverse(text(t, T_HELP_LEFT + i * T_HELP_LEFT_STRIDE),
                         2u, (uint8_t)(i + 1u), false, true, 42u);
        text(t, T_HELP_RIGHT + i * T_HELP_RIGHT_STRIDE);
        if (t[0])
            A->print_inverse(t, 79u, (uint8_t)(i + 1u), false, true, 127u);
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

static void rampUp(void)
{
    const uint8_t quarter = (uint8_t)(g.paBias >> 2);
    const uint8_t half = (uint8_t)(g.paBias >> 1);
    A->bk_write(REG_TX_LINK, TX_LINK_NO_MIC);
    A->tx_carrier(true);
    writeBias(quarter);                         A->delay_ms(1u);
    writeBias(half);                            A->delay_ms(1u);
    writeBias((uint8_t)(g.paBias - quarter));  A->delay_ms(1u);
    writeBias(g.paBias);                        A->delay_ms(1u);
}

static void rampDown(void)
{
    const uint8_t quarter = (uint8_t)(g.paBias >> 2);
    const uint8_t half = (uint8_t)(g.paBias >> 1);
    writeBias((uint8_t)(g.paBias - quarter));  A->delay_ms(1u);
    writeBias(half);                            A->delay_ms(1u);
    writeBias(quarter);                         A->delay_ms(1u);
    writeBias(0u);                              A->delay_ms(1u);
    A->tx_carrier(false);
    A->bk_write(REG_TX_LINK, TX_LINK_NO_PA);
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
    const uint32_t frequency = calibratedFrequency(tunedFrequency(A->tx_freq()));
    A->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    A->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));

    /* REG_38/39 are written after tx_set_params, so restart the TX link to
     * recalibrate the VCO and latch the corrected synthesizer frequency. Keep
     * internal PA gain disabled when the link comes back up. */
    A->bk_write(REG_TX_LINK, TX_LINK_OFF);
    A->bk_write(REG_TX_LINK, TX_LINK_NO_PA);
    A->delay_ms(PLL_LOCK_MS);
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
    rampUp();
    g.keyDown = true;
    g.markMs = 0u;
    g.hangElapsedMs = 0u;
    return true;
}

static void keyOff(void)
{
    rampDown();
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

/* Wait without touching the display. Opposite-paddle taps are remembered, and
 * EXIT remains an immediate RF-safe abort even during a dah. */
static bool keyerWait(uint16_t ms, uint8_t current, uint8_t *memory)
{
    while (ms-- && g.running) {
        serviceOneMs();
        const uint8_t key = A->get_key();
        if (key == APP_KEY_EXIT) {
            g.running = false;
            return false;
        }
        if (isSide(key) && key != current)
            *memory = key;
    }
    return g.running;
}

static bool sendElement(uint8_t key, uint8_t *memory)
{
    if (!keyOn())
        return false;

    const uint16_t dot = dotMs();
    uint16_t mark = dot;
    if (key != ditKey())
        mark = (uint16_t)(mark * 3u);
    const uint16_t holdMs = (uint16_t)(mark - 2u * RAMP_MS);

    const bool complete = keyerWait(holdMs, key, memory);
    keyOff();
    if (!complete)
        return false;
    return keyerWait(dot, key, memory);
}

static void runKeyer(uint8_t first)
{
    uint8_t current = first;
    while (g.running && isSide(current)) {
        uint8_t memory = APP_KEY_INVALID;
        if (!sendElement(current, &memory))
            break;
        if (isSide(memory)) {
            current = memory;
            continue;
        }
        current = A->get_key();
    }
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

    if (g.txReady) {
        A->tx_end();
        g.txReady = false;
        g.hangElapsedMs = 0u;
    }
    configureCwRx();
    g.denied = false;
    return true;
}

static void adjustSetting(uint8_t key, int8_t direction)
{
    bool retune = false;
    switch (key) {
        case APP_KEY_0:
            g.speaker = !g.speaker;
            A->audio_path(g.speaker && !g.txReady);
            break;
        case APP_KEY_1:
            g.mode ^= 1u;
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
                if (g.calCentiPpm >= CAL_MAX)
                    return;
                g.calCentiPpm++;
            } else {
                if (g.calCentiPpm <= CAL_MIN)
                    return;
                g.calCentiPpm--;
            }
            retune = true;
            break;
        default:
            return;
    }
    if (retune) {
        if (g.txReady)
            returnToRx();
        else
            configureCwRx();
    }
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
            disarm();
            g.fArm = false;
            g.help = true;
            g.redraw = true;
            break;
        case APP_KEY_F:
            g.fArm = !g.fArm;
            g.statusRedraw = true;
            break;
        case APP_KEY_0: case APP_KEY_1: case APP_KEY_2: case APP_KEY_3:
        case APP_KEY_4: case APP_KEY_5: case APP_KEY_6:
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
    if ((cfg.magic == CFG_MAGIC || cfg.magic == CFG_MAGIC_V3 ||
         cfg.magic == CFG_MAGIC_V2 || cfg.magic == CFG_MAGIC_V1) &&
        cfg.wpm >= WPM_MIN && cfg.wpm <= WPM_MAX &&
        cfg.mode <= MODE_PADDLE && cfg.reverse <= 1u) {
        g.wpm = cfg.wpm;
        g.mode = cfg.mode;
        g.reverse = cfg.reverse;
        g.calCentiPpm = (cfg.magic == CFG_MAGIC || cfg.magic == CFG_MAGIC_V3 ||
            cfg.magic == CFG_MAGIC_V2) &&
            cfg.calCentiPpm >= CAL_MIN && cfg.calCentiPpm <= CAL_MAX
            ? cfg.calCentiPpm : CAL_DEFAULT;
    } else {
        g.wpm = WPM_DEFAULT;
        g.mode = MODE_STRAIGHT;
        g.reverse = false;
        g.calCentiPpm = CAL_DEFAULT;
    }
    g.rxPitchHz = (cfg.magic == CFG_MAGIC || cfg.magic == CFG_MAGIC_V3) &&
        cfg.rxPitchHz >= RX_PITCH_MIN && cfg.rxPitchHz <= RX_PITCH_MAX
        ? cfg.rxPitchHz : RX_PITCH_DEFAULT;
    g.breakInMs = (cfg.magic == CFG_MAGIC || cfg.magic == CFG_MAGIC_V3) &&
        cfg.breakInMs >= BREAKIN_MIN && cfg.breakInMs <= BREAKIN_MAX
        ? cfg.breakInMs : BREAKIN_DEFAULT;
    g.speaker = cfg.magic == CFG_MAGIC && cfg.speaker <= 1u
        ? cfg.speaker : true;
}

static void saveConfig(void)
{
    const config_t cfg = {
        CFG_MAGIC, g.wpm, g.mode, (uint8_t)g.reverse, g.calCentiPpm,
        g.rxPitchHz, g.breakInMs, (uint8_t)g.speaker, 0u
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
    g.help = false;
    resetUiInput();
    loadConfig();
    A->backlight_on();
    configureCwRx();
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
            if (key == APP_KEY_PTT || isSide(key)) {
                resetUiInput();
                serviceOneMs();
                continue;
            }
            g.denied = false;
            g.statusRedraw = true;
        }

        const bool straight = !g.help && (key == APP_KEY_PTT ||
            (g.mode == MODE_STRAIGHT && isSide(key)));
        if (straight) {
            resetUiInput();
            if (!g.keyDown)
                keyOn();
        } else if (g.keyDown) {
            keyOff();
            resetUiInput();
        } else if (!g.help && g.mode == MODE_PADDLE && isSide(key)) {
            resetUiInput();
            A->backlight_on();
            runKeyer(key);
        } else if (key != APP_KEY_INVALID) {
            handleUiInput(key);
        } else {
            resetUiInput();
        }

        if (g.redraw && !g.keyDown)
            draw();
        else if (g.statusRedraw && !g.help && !g.keyDown)
            redrawMainStatus();
        serviceOneMs();
    }

    disarm();
    saveConfig();
    A->set_af(APP_AF_MUTE);
    A->audio_path(false);
}
