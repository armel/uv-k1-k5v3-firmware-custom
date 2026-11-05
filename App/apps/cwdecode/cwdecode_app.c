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
 * CW Decode - keyed-carrier Morse decoder for the Fusion firmware.
 *
 * The BK4829 cannot demodulate SSB/CW into an audio sidetone. This app follows
 * the corrected RSSI envelope instead, so it decodes a nearby A1A transmitter
 * without relying on the noisy FM audio path.
 */

#include <stdbool.h>
#include <stdint.h>
#include "../app_api.h"
#include "cwdecode_assets.h"

#define SAMPLE_MS        10u
#define KEY_MS           50u
#define DRAW_MS         250u
#define BATTERY_MS      500u
#define CALIBRATE_MS   1200u
#define DEBOUNCE_MS      10u
#define MIN_MARK_MS      20u
#define MAX_MARK_MS    1200u
#define DEFAULT_DOT_MS   80u
#define MIN_DOT_MS       30u
#define MAX_DOT_MS      300u
#define SPEED_STEP_MS     5u
#define DEFAULT_MARGIN    4u
#define MIN_MARGIN        4u
#define MAX_MARGIN       30u
#define HYSTERESIS_DB     3
#define WORD_GAP_DOTS     6u
#define LINE_GAP_DOTS    15u
#define TEXT_CAP        256u
/* The decoded text uses frame-buffer rows 0 to 30; row 31 stays blank above
 * the three capsule lines (4: THR / WPM, 5: RSSI / XTAL, 6: MORSE / RX
 * frequency). */
#define BODY_BOTTOM      31u
#define BODY_LAST_PAGE   (BODY_BOTTOM >> 3)
#define BODY_LAST_MASK   ((1u << (BODY_BOTTOM & 7u)) - 1u)
#define CAPS_LINE         4u
#define CFG_MAGIC_V1    0xC9u   /* no speaker byte */
#define CFG_MAGIC_V2    0xCAu   /* no crystal correction */
#define CFG_MAGIC       0xD0u
#define RX_PITCH_10HZ    70u
#define CAL_DEFAULT      14   /* REG_3B steps, as CW Keyer: 14 x 0.1923 = +2.69 ppm */
#define CAL_MIN         -50
#define CAL_MAX          50
#define AGC_X            44u
#define SCROLL_X         63u
#define F_X              72u
#define LEFT_CAPS_X       2u
#define LEFT_CAPS_END    42u
#define RIGHT_CAPS_X     87u
#define RIGHT_CAPS_END  127u
#define MORSE_CAPS_END   54u

_Static_assert(BODY_LAST_PAGE + 1u == CAPS_LINE,
               "CW Decode text body must end right above the capsules");
_Static_assert((CAL_MAX * 1923 + 50) / 100 < 1000 &&
               (-(CAL_MIN) * 1923 + 50) / 100 < 1000,
               "XTAL correction must stay below 10 ppm");

#define REG_TX_LINK      0x30u
#define REG_AM_CTRL      0x31u
#define REG_FREQ_LOW     0x38u
#define REG_FREQ_HIGH    0x39u
#define REG_XTAL         0x3Bu
#define REG_AUDIO_FILTER 0x3Du
#define REG_AUDIO_GAIN   0x48u
#define REG_AFC          0x73u
#define AF_USB              5u
#define RX_LINK_OFF      0x0000u

typedef struct {
    uint8_t magic, compact, margin, agc, speaker;
    int8_t calSteps;
} config_t;

_Static_assert(sizeof(config_t) <= 16u, "CW Decode config exceeds overlay slot");

_Static_assert(BMP_CAL_SCREEN_LEN == 4u * 128u,
               "CW Decode calibration screen must cover LCD pages 1 through 4");

static struct {
    const app_api_t *A;
    const char *morse;
    bool running, down, candidate, fArm, redraw, follow, keyRedraw, morseRedraw;
    bool help;
    bool agc, speaker;
    bool linePending;
    uint8_t prevKey, margin, gapStage, sampleIndex;
    uint8_t code, depth, compact;
    int8_t calSteps;
    uint16_t dotMs, textLen, top, limit, xtalBase;
    int16_t rssi, threshold, samples[3];
    int32_t noiseQ8;
    uint32_t edgeAt, candidateAt, lastDraw, lastBattery, lastKey;
    char pattern[7];
    char *text;
} g;

#define A (g.A)

static int32_t divPow2(int32_t value, uint8_t shift)
{
    return value < 0 ? -(int32_t)((uint32_t)(-value) >> shift)
                     :  (int32_t)((uint32_t)value >> shift);
}

static uint32_t udiv(uint32_t n, uint32_t d)
{
    return (uint32_t)A->uidivmod(n, d);
}

static char *put(char *out, const char *text)
{
    while (*text)
        *out++ = *text++;
    return out;
}

/* Local stubs keep every call to these services a plain bl: they lie past
 * the ldr immediate range of the API table, so a direct call costs an extra
 * instruction at each site. */
__attribute__((noinline))
static char *puti(char *out, int32_t value)
{
    return A->append_i32(out, value);
}
#define putu puti

__attribute__((noinline))
static void asset(uint16_t offset, void *buf, uint16_t len)
{
    A->asset_read(offset, buf, len);
}

__attribute__((noinline))
static uint32_t ticks(void)
{
    return A->ticks_ms();
}

/* An inverse 3x5 capsule on a frame-buffer line, from x to end. */
__attribute__((noinline))
static void tag(const char *s, uint8_t x, uint8_t line, uint8_t end)
{
    A->print_inverse(s, x, line, false, true, end);
}

/* The crystal correction in ppm with two decimals, as CW Keyer shows it:
 * |steps| x 0.1923, rounded half away from zero. It stays below 10 ppm, so
 * magnitude + 1000 prints as four digits, "1" then the units and the two
 * decimals: the units digit moves over the "1" and the dot takes its place. */
static char *putCalibration(char *out)
{
    const int steps = g.calSteps;
    *out++ = steps < 0 ? '-' : '+';
    const uint32_t magnitude =
        udiv((uint32_t)(steps < 0 ? -steps : steps) * 1923u + 50u, 100u);
    char *end = putu(out, (int32_t)(magnitude + 1000u));
    out[0] = out[1];
    out[1] = '.';
    return end;
}

/* The crystal correction is a signed count of BK4829 REG_3B steps (0.1923
 * ppm each) added to the value found on entry, as in CW Keyer: positive
 * means the radio runs high, and lowers every frequency in proportion. The
 * REG_30 restart below latches it, so the receiver sits RX_PITCH_10HZ below
 * the true carrier and the tone comes out at 700 Hz. */
static void configureCwRx(void)
{
    const app_api_t *const a = A;   /* one load for the whole sequence */
    a->bk_write(REG_XTAL, (uint16_t)(g.xtalBase + g.calSteps));
    uint32_t frequency = a->rx_freq();
    if (frequency > RX_PITCH_10HZ)
        frequency -= RX_PITCH_10HZ;

    a->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    a->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));
    const uint16_t rxLink = a->bk_read(REG_TX_LINK);
    a->bk_write(REG_TX_LINK, RX_LINK_OFF);
    a->bk_write(REG_TX_LINK, rxLink);

    /* Match CW Keyer's proven USB product-detector path. The displayed VFO
     * remains the carrier frequency while the receiver runs 700 Hz below it.
     * RX_PROFILE (gen_assets.py) holds the RX level, filter and audio
     * register / value pairs, written in their original order. */
    a->bk_write(REG_AM_CTRL, a->bk_read(REG_AM_CTRL) & 0xFFFEu);
    uint16_t profile[RX_PROFILE_LEN / 4u][2];
    asset(RX_PROFILE, profile, sizeof(profile));
    for (uint8_t i = 0u; i < RX_PROFILE_LEN / 4u; i++)
        a->bk_write((uint8_t)profile[i][0], profile[i][1]);
    a->bk_write(REG_AUDIO_GAIN,
                (uint16_t)((a->bk_read(REG_AUDIO_GAIN) & 0xFFF0u) | 0x000Fu));
    a->bk_write(REG_AUDIO_FILTER, 0u);
    a->bk_write(REG_AFC, a->bk_read(REG_AFC) | 0x0010u);
    a->set_agc(g.agc);
    a->set_af(AF_USB);
    a->audio_path(g.speaker);
}

static void updateLimit(void)
{
    const uint16_t width = g.compact ? 32u : 18u;
    const uint16_t step = g.compact ? 6u : 8u;
    const uint16_t visibleRows = g.compact ? 5u : 4u;
    uint16_t rows = g.textLen ? 1u : 0u;
    uint16_t column = 0u;
    for (uint16_t i = 0u; i < g.textLen; i++) {
        if (g.text[i] == '\n') {
            rows++;
            column = 0u;
        } else {
            if (column == width) {
                rows++;
                column = 0u;
            }
            column++;
        }
    }
    g.limit = rows > visibleRows ? (uint16_t)((rows - visibleRows) * step) : 0u;
    if (g.top > g.limit)
        g.top = g.limit;
}

static void append(char c)
{
    if (c == ' ' && (!g.textLen || g.text[g.textLen - 1u] == ' ' ||
                     g.text[g.textLen - 1u] == '\n'))
        return;
    if (c == '\n' && (!g.textLen || g.text[g.textLen - 1u] == '\n'))
        return;
    const bool keepFollowing = g.follow || g.top == g.limit;
    if (g.textLen >= TEXT_CAP) {
        const uint16_t width = g.compact ? 32u : 18u;
        const uint16_t step = g.compact ? 6u : 8u;
        uint16_t drop = 0u;
        while (drop < width && g.text[drop] != '\n')
            drop++;
        if (g.text[drop] == '\n')
            drop++;
        for (uint16_t i = drop; i < g.textLen; i++)
            g.text[i - drop] = g.text[i];
        g.textLen = (uint16_t)(g.textLen - drop);
        if (!keepFollowing)
            g.top = g.top > step ? (uint16_t)(g.top - step) : 0u;
    }
    g.text[g.textLen++] = c;
    g.text[g.textLen] = '\0';
    g.follow = keepFollowing;
    updateLimit();
    if (g.follow)
        g.top = g.limit;
    g.redraw = true;
}

static void armLineBreak(void)
{
    if (!g.textLen || g.text[g.textLen - 1u] == '\n')
        return;
    g.linePending = true;
}

static void clearSymbol(void)
{
    g.code = 1u;
    g.depth = 0u;
    g.pattern[0] = '\0';
    g.morseRedraw = true;
}

static void finishCharacter(void)
{
    if (!g.depth)
        return;
    append(g.depth <= 5u && g.code < 64u && g.morse[g.code]
           ? g.morse[g.code] : '?');
    clearSymbol();
}

static void addMark(uint32_t duration)
{
    if (duration < MIN_MARK_MS)
        return;
    if (duration > MAX_MARK_MS) {
        append('?');
        clearSymbol();
        return;
    }

    const bool dash = duration > (uint32_t)g.dotMs * 2u;
    uint32_t estimate = dash ? udiv(duration, 3u) : duration;
    if (estimate < MIN_DOT_MS)
        estimate = MIN_DOT_MS;
    if (estimate > MAX_DOT_MS)
        estimate = MAX_DOT_MS;
    const uint32_t low = (uint32_t)g.dotMs * 7u >> 3;
    const uint32_t high = (uint32_t)g.dotMs * 9u >> 3;
    if (estimate < low)
        estimate = low;
    if (estimate > high)
        estimate = high;
    g.dotMs = (uint16_t)(((uint32_t)g.dotMs * 3u + estimate + 2u) >> 2);

    if (g.depth < 6u) {
        g.pattern[g.depth++] = dash ? '-' : '.';
        g.pattern[g.depth] = '\0';
        if (g.depth <= 5u)
            g.code = (uint8_t)((g.code << 1) | (dash ? 1u : 0u));
    }
    g.morseRedraw = true;
    g.redraw = true;
}

static void serviceGap(uint32_t now)
{
    if (g.down)
        return;
    const uint32_t gap = now - g.edgeAt;
    if (g.gapStage == 0u && gap >= ((uint32_t)g.dotMs * 5u >> 1)) {
        finishCharacter();
        g.gapStage = 1u;
    }
    if (g.gapStage == 1u && gap >= (uint32_t)g.dotMs * WORD_GAP_DOTS) {
        g.gapStage = 2u;
    }
    if (g.gapStage == 2u && gap >= (uint32_t)g.dotMs * LINE_GAP_DOTS) {
        armLineBreak();
        g.gapStage = 3u;
    }
}

static void acceptEdge(bool down, uint32_t at)
{
    if (down) {
        if (!g.follow) {
            g.follow = true;
            g.top = g.limit;
            g.redraw = true;
            g.keyRedraw = true;
        }
        serviceGap(at);
        if (g.linePending) {
            g.linePending = false;
            append('\n');
        } else if (g.gapStage >= 2u)
            append(' ');
        g.down = true;
        g.edgeAt = at;
    } else {
        const uint32_t mark = at - g.edgeAt;
        g.down = false;
        g.edgeAt = at;
        g.gapStage = 0u;
        addMark(mark);
    }
    g.redraw = true;
}

static void sample(uint32_t now)
{
    const int16_t rawRssi = A->rssi_dbm();
    g.samples[g.sampleIndex] = rawRssi;
    if (++g.sampleIndex >= 3u)
        g.sampleIndex = 0u;
    const int16_t a = g.samples[0], b = g.samples[1], c = g.samples[2];
    g.rssi = a > b ? (b > c ? b : (a > c ? c : a))
                   : (a > c ? a : (b > c ? c : b));

    g.threshold = (int16_t)(divPow2(g.noiseQ8, 8u) + g.margin);
    const int16_t decision = g.down
        ? (int16_t)(g.threshold - HYSTERESIS_DB) : g.threshold;
    const bool raw = g.rssi >= decision;

    /* A valid Morse mark never exceeds MAX_MARK_MS. Rebase a continuously
     * asserted input so a raised noise floor cannot lock the decoder forever. */
    if (g.down && raw && g.candidate && now - g.edgeAt > MAX_MARK_MS) {
        append('?');
        clearSymbol();
        g.noiseQ8 = (int32_t)g.rssi * 256;
        g.threshold = (int16_t)(g.rssi + g.margin);
        g.down = g.candidate = false;
        g.candidateAt = g.edgeAt = now;
        g.gapStage = 0u;
        return;
    }

    if (!g.down && !raw) {
        const int32_t target = (int32_t)g.rssi * 256;
        g.noiseQ8 += divPow2(target - g.noiseQ8, 8u);
    }

    if (raw == g.down) {
        g.candidate = raw;
        g.candidateAt = now;
    } else if (raw != g.candidate) {
        g.candidate = raw;
        g.candidateAt = now;
    } else if (now - g.candidateAt >= DEBOUNCE_MS) {
        acceptEdge(raw, g.candidateAt);
        g.candidateAt = now;
    }
}

static void drawStatus(const char *ui)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    uint8_t *const status = a->status_line;
    a->status_clear();
    a->print_inverse(ui + T_TITLE, 2u, 0u, true, true,
                     (uint8_t)(2u + T_TITLE_CHARS * 4u));
    if (g.agc)
        asset(BMP_AGC, status + AGC_X, BMP_AGC_LEN);
    unsigned marks = 0u;
    if (g.top)
        marks |= 1u;
    if (g.top < g.limit)
        marks |= 2u;
    asset((uint16_t)(BMP_SCROLL + marks * BMP_SCROLL_W),
          status + SCROLL_X, BMP_SCROLL_W);
    if (g.fArm)
        asset(BMP_F, status + F_X, BMP_F_LEN);
    else if (g.speaker)
        asset(BMP_SPEAKER, status + F_X, BMP_SPEAKER_LEN);
    a->draw_battery();
}

static void renderDecoded(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    const app_fb_t fb = a->fb;
    char row[33];
    uint16_t pos = 0u, vrow = g.compact ? 2u : 0u;
    const uint8_t width = g.compact ? 32u : 18u;
    const uint8_t step = g.compact ? 6u : 8u;
    const int32_t bodyTop = g.compact ? 2 : 0;
    const int32_t glyphHeight = g.compact ? 5 : 7;

    while (pos < g.textLen) {
        uint8_t n = 0u;
        while (n < width && pos < g.textLen && g.text[pos] != '\n')
            row[n++] = g.text[pos++];
        row[n] = '\0';

        const int32_t y = (int32_t)vrow - (int32_t)g.top;
        if (n && y + glyphHeight > bodyTop && y < (int32_t)BODY_BOTTOM) {
            uint8_t *scratch = fb[6];
            if (g.compact)
                a->print_tiny(row, 0u, 48u, false, true);
            else
                a->print_normal(row, 0u, 0u, 6u);

            for (unsigned x = 0u; x < 128u; x++) {
                if (y < bodyTop) {
                    const uint8_t crop = (uint8_t)(bodyTop - y);
                    fb[0][x] |= (uint8_t)((scratch[x] >> crop) << bodyTop);
                } else {
                    const unsigned page = (unsigned)y >> 3;
                    const unsigned shift = (unsigned)y & 7u;
                    uint16_t bits = (uint16_t)scratch[x] << shift;
                    uint8_t *out = fb[page] + x;
                    if (page == BODY_LAST_PAGE)
                        bits &= BODY_LAST_MASK;
                    *out |= (uint8_t)bits;
                    if (page < BODY_LAST_PAGE)
                        out[128] |= (uint8_t)(bits >> 8);
                }
                scratch[x] = 0u;
            }
        }
        if (pos < g.textLen && g.text[pos] == '\n')
            pos++;
        vrow = (uint16_t)(vrow + step);
    }
}

/* Two columns of equal capsules as in CW Keyer: THR / WPM on line 4,
 * RSSI / XTAL on line 5, then the MORSE capsule and the RX frequency on
 * line 6. The frames reserve the widest values, so they never resize. */
static void drawCapsules(const char *labels, bool blit)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    char line[34], *out;
    if (blit) {
        uint8_t *const caps = a->fb[CAPS_LINE];   /* lines 4 to 6 in a row */
        for (unsigned x = 0u; x < 3u * 128u; x++)
            caps[x] = 0u;
    }

    out = put(line, labels + T_THR - T_RSSI);
    out = puti(out, g.threshold);
    *out = '\0';
    tag(line, LEFT_CAPS_X, CAPS_LINE, LEFT_CAPS_END);

    out = put(line, labels + T_WPM - T_RSSI);
    out = putu(out, udiv(1200u, g.dotMs));
    *out = '\0';
    tag(line, RIGHT_CAPS_X, CAPS_LINE, RIGHT_CAPS_END);

    out = put(line, labels);
    out = puti(out, g.rssi);
    *out = '\0';
    tag(line, LEFT_CAPS_X, CAPS_LINE + 1u, LEFT_CAPS_END);

    out = putCalibration(put(line, labels + T_XTAL - T_RSSI));
    *out = '\0';
    tag(line, RIGHT_CAPS_X, CAPS_LINE + 1u, RIGHT_CAPS_END);

    out = put(line, labels + T_MORSE - T_RSSI);
    if (g.pattern[0]) {
        *out++ = ' ';
        out = put(out, g.pattern);
    } else if (g.gapStage == 3u) {
        *out++ = ' ';
        out = put(out, labels + T_WAIT - T_RSSI);
    }
    *out = '\0';
    /* Reserve the wider idle label so neither Morse marks nor WAITING resize
     * the capsule. */
    tag(line, LEFT_CAPS_X, CAPS_LINE + 2u, MORSE_CAPS_END);

    /* Match Beacon: RX frequency in 10 Hz units, shown as MHz with five
     * decimals and right-aligned in the normal font. */
    char *const end = putu(line, a->rx_freq());
    char *point = end;
    point[1] = '\0';
    for (uint8_t i = 0u; i < 5u; i++, point--)
        point[0] = point[-1];
    point[0] = '.';
    const unsigned len = (unsigned)(end - line) + 1u;
    a->print_normal(line, (uint8_t)(126u - len * 7u), 0u, CAPS_LINE + 2u);
    if (blit) {
        for (unsigned i = 0u; i < 3u; i++)
            a->blit_line(CAPS_LINE + i);
    }
}

static void drawHelp(const char *ui)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    char text[TEXT_MAX];
    a->status_clear();
    a->print_inverse(ui + T_TITLE, 2u, 0u, true, true,
                     (uint8_t)(2u + T_TITLE_CHARS * 4u));
    asset(T_HELP, text, TEXT_MAX);
    a->print_inverse(text, AGC_X, 0u, true, true,
                     (uint8_t)(AGC_X + 4u * 4u));
    a->draw_battery();
    /* T_HELP_KEYS: the left column (lines 1 to 6), then the right one. */
    for (uint8_t i = 0u; i < 12u; i++) {
        asset((uint16_t)(T_HELP_KEYS + i * T_HELP_KEYS_STRIDE), text, TEXT_MAX);
        if (text[0]) {
            const bool right = i >= 6u;
            tag(text, right ? 79u : 2u, (uint8_t)(right ? i - 5u : i + 1u),
                right ? 127u : 46u);
        }
    }
    asset(T_HELP_UNIT_1, text, TEXT_MAX);
    a->print_tiny(text, 56u, 41u, false, true);
    asset(T_HELP_UNIT_2, text, TEXT_MAX);
    a->print_tiny(text, 56u, 49u, false, true);
}

static void refreshCapsules(void)
{
    char labels[UI_SIZE - T_RSSI];
    asset(T_RSSI, labels, sizeof(labels));
    drawCapsules(labels, true);
}

static void draw(bool calibrating)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    char ui[UI_SIZE] __attribute__((aligned(4)));
    asset(0u, ui, UI_SIZE);
    a->display_clear();
    if (g.help) {
        drawHelp(ui);
    } else {
        drawStatus(ui);
        if (calibrating) {
            asset(BMP_CAL_SCREEN, a->fb[1], BMP_CAL_SCREEN_LEN);
        } else {
            if (g.textLen)
                renderDecoded();

            drawCapsules(ui + T_RSSI, false);
        }
    }

    a->blit_status();
    a->blit_full();
    g.redraw = false;
    g.morseRedraw = false;
}

static void calibrate(void)
{
    const app_api_t *const a = A;   /* one load: calls may change g */
    g.down = false;
    g.candidate = false;
    g.candidateAt = ticks();
    clearSymbol();
    g.gapStage = 2u;
    g.rssi = a->rssi_dbm();
    g.noiseQ8 = (int32_t)g.rssi * 256;

    const uint32_t start = ticks();
    uint32_t last = start;
    draw(true);
    while (ticks() - start < CALIBRATE_MS) {
        a->delay_ms(SAMPLE_MS);
        const int32_t target = (int32_t)a->rssi_dbm() * 256;
        g.noiseQ8 += divPow2(target - g.noiseQ8, 3u);
        const uint32_t now = ticks();
        if (now - last >= DRAW_MS) {
            last = now;
            a->backlight_update();
        }
    }

    g.rssi = (int16_t)divPow2(g.noiseQ8, 8u);
    g.threshold = (int16_t)(g.rssi + g.margin);
    g.sampleIndex = 0u;
    for (uint8_t i = 0u; i < 3u; i++)
        g.samples[i] = g.rssi;
    g.edgeAt = ticks();
    g.lastDraw = 0u;
    g.redraw = true;
}

/* Older configs keep their settings: V1 has no speaker byte, V1 and V2 no
 * crystal correction (CAL_DEFAULT, the CW Keyer default). */
static void loadConfig(void)
{
    config_t cfg;
    A->cfg_load((uint8_t *)&cfg, sizeof(cfg));
    if (cfg.magic == CFG_MAGIC || cfg.magic == CFG_MAGIC_V2 ||
        cfg.magic == CFG_MAGIC_V1) {
        g.compact = (uint8_t)(cfg.compact == 1u);
        g.margin = cfg.margin >= MIN_MARGIN && cfg.margin <= MAX_MARGIN
            ? cfg.margin : DEFAULT_MARGIN;
        g.agc = cfg.agc <= 1u ? cfg.agc : true;
        g.speaker = cfg.magic != CFG_MAGIC_V1 && cfg.speaker <= 1u
            ? cfg.speaker : true;
    } else {
        g.margin = DEFAULT_MARGIN;
        g.agc = true;
        g.speaker = true;
    }
    g.calSteps = cfg.magic == CFG_MAGIC &&
        cfg.calSteps >= CAL_MIN && cfg.calSteps <= CAL_MAX
        ? cfg.calSteps : CAL_DEFAULT;
}

static void saveConfig(void)
{
    const config_t cfg = {
        CFG_MAGIC, g.compact, g.margin, g.agc, g.speaker, g.calSteps
    };
    A->cfg_save((const uint8_t *)&cfg, sizeof(cfg));
}

static void clearText(void)
{
    g.textLen = g.top = g.limit = 0u;
    g.text[0] = '\0';
    g.follow = true;
    g.linePending = false;
    clearSymbol();
    g.gapStage = 2u;
}

static void handleKey(uint8_t key)
{
    if (g.help) {
        if (key == APP_KEY_INVALID || key == APP_KEY_SAVER ||
            key == APP_KEY_WAKE || key == g.prevKey) {
            g.prevKey = key;
            return;
        }
        g.prevKey = key;
        A->backlight_on();
        if (key == APP_KEY_MENU || key == APP_KEY_EXIT) {
            g.help = false;
            g.redraw = true;
            g.keyRedraw = true;
        }
        return;
    }

    /* Scrolling is spatial: raw UP/DOWN is intentional and matches the
     * APRS RX/EPIRB fix for K5 UP/DOWN and K1 LEFT/RIGHT (issue #613).
     * Using nav_dir() here would reintroduce the model-dependent reversal. */
    const int direction = (key == APP_KEY_DOWN) - (key == APP_KEY_UP);
    const uint16_t top = (uint16_t)(g.top + direction);
    if (direction && top <= g.limit) {
        g.top = top;
        g.follow = g.top == g.limit;
        g.redraw = true;
    }

    if (key == APP_KEY_INVALID || key == APP_KEY_SAVER || key == APP_KEY_WAKE ||
        key == g.prevKey) {
        g.prevKey = key;
        return;
    }
    g.prevKey = key;
    A->backlight_on();
    /* A key action may redraw at the first stable carrier-up interval instead
     * of waiting for the much longer normal housekeeping window. */
    g.keyRedraw = true;

    if (key == APP_KEY_F) {
        g.fArm = !g.fArm;
        g.redraw = true;
        return;
    }

    const bool reverse = g.fArm;
    g.fArm = false;
    switch (key) {
        case APP_KEY_EXIT:
            g.running = false;
            break;
        case APP_KEY_0:
            g.speaker = !g.speaker;
            A->audio_path(g.speaker);
            break;
        case APP_KEY_1:
            if (!reverse && g.margin < MAX_MARGIN)
                g.margin++;
            else if (reverse && g.margin > MIN_MARGIN)
                g.margin--;
            break;
        case APP_KEY_2:
            if (!reverse && g.dotMs > MIN_DOT_MS + SPEED_STEP_MS - 1u)
                g.dotMs -= SPEED_STEP_MS;
            else if (reverse && g.dotMs < MAX_DOT_MS - SPEED_STEP_MS + 1u)
                g.dotMs += SPEED_STEP_MS;
            break;
        case APP_KEY_3:
            g.agc = !g.agc;
            A->set_agc(g.agc);
            calibrate();
            break;
        case APP_KEY_4:
            calibrate();
            break;
        case APP_KEY_5:
            clearText();
            break;
        case APP_KEY_6:
            if (!reverse && g.calSteps < CAL_MAX)
                g.calSteps++;
            else if (reverse && g.calSteps > CAL_MIN)
                g.calSteps--;
            configureCwRx();
            break;
        case APP_KEY_STAR:
            g.compact ^= 1u;
            updateLimit();
            g.top = g.limit;
            g.follow = true;
            break;
        case APP_KEY_MENU:
            g.help = true;
            break;
        default:
            break;
    }
    g.redraw = true;
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    char morse[MORSE_TREE_LEN];
    /* History lives for the whole modal call but stays outside the 4 KiB
     * overlay image, following the larger-buffer overlay apps. */
    char text[TEXT_CAP + 1u];
    const app_api_t *const a = api;   /* kept in a register for the loop */
    A = api;
    g.morse = morse;
    g.text = text;
    asset(MORSE_TREE, morse, sizeof(morse));
    g.running = true;
    g.follow = true;
    g.help = false;
    g.prevKey = APP_KEY_INVALID;
    g.dotMs = DEFAULT_DOT_MS;
    loadConfig();
    updateLimit();
    a->backlight_on();
    g.xtalBase = a->bk_read(REG_XTAL);
    configureCwRx();
    calibrate();

    while (g.running) {
        const uint32_t now = ticks();
        sample(now);
        serviceGap(now);

        if (now - g.lastKey >= KEY_MS) {
            g.lastKey = now;
            handleKey(a->get_key());
        }
        if (!g.running)
            break;

        const uint32_t gap = now - g.edgeAt;
        const bool stableGap = !g.down && g.candidate == g.down;
        const bool quietWindow = stableGap &&
            (g.gapStage == 2u || gap >= (uint32_t)g.dotMs * 4u);
        const bool drawWindow = quietWindow || (stableGap && g.keyRedraw);
        if (drawWindow && (g.redraw || !g.lastDraw || now - g.lastDraw >= DRAW_MS)) {
            g.lastDraw = now;
            draw(false);
            g.keyRedraw = false;
        } else if (!g.help && stableGap && g.morseRedraw) {
            refreshCapsules();
            g.morseRedraw = false;
        }
        if (quietWindow && now - g.lastBattery >= BATTERY_MS) {
            g.lastBattery = now;
            a->battery_sample();
        }
        a->backlight_update();
        a->delay_ms(SAMPLE_MS);
    }

    saveConfig();
    a->bk_write(REG_XTAL, g.xtalBase);
    /* The loader's retune may keep its cached AGC setting, so restore the
     * receiver's normal automatic gain explicitly before returning. */
    a->set_agc(true);
    a->set_af(APP_AF_MUTE);
    a->audio_path(false);
}
