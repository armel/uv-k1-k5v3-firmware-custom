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
#define BODY_BOTTOM      39u
#define CFG_MAGIC_V1    0xC9u
#define CFG_MAGIC       0xCAu
#define RX_PITCH_10HZ    70u
#define AGC_X            44u
#define SCROLL_X         63u
#define F_X              72u
#define THR_CAPS_X        2u
#define THR_CAPS_END     34u
#define WPM_CAPS_X       52u
#define WPM_CAPS_END     76u
#define RSSI_CAPS_X      91u
#define RSSI_CAPS_END   127u
#define MORSE_CAPS_END   54u

#define REG_TX_LINK      0x30u
#define REG_AM_CTRL      0x31u
#define REG_FREQ_LOW     0x38u
#define REG_FREQ_HIGH    0x39u
#define REG_AUDIO_FILTER 0x3Du
#define REG_RX_LEVEL     0x42u
#define REG_AUDIO_GAIN   0x48u
#define REG_AUDIO_1      0x54u
#define REG_AUDIO_2      0x55u
#define REG_AFC          0x73u
#define REG_RX_FILTER_1  0x2Au
#define REG_RX_FILTER_2  0x2Bu
#define REG_RX_FILTER_3  0x2Fu
#define AF_USB              5u
#define RX_LINK_OFF      0x0000u

typedef struct {
    uint8_t magic, compact, margin, agc, speaker;
} config_t;

_Static_assert(sizeof(config_t) <= 16u, "CW Decode config exceeds overlay slot");

typedef struct {
    char morse[MORSE_TREE_LEN];
    uint32_t decimalPlaces[DECIMAL_PLACES_LEN / 4u];
} asset_tables_t;

_Static_assert(DECIMAL_PLACES == MORSE_TREE + MORSE_TREE_LEN,
               "CW Decode lookup assets must remain contiguous");
_Static_assert(sizeof(asset_tables_t) == MORSE_TREE_LEN + DECIMAL_PLACES_LEN,
               "CW Decode lookup cache must not contain padding");
_Static_assert(BMP_CAL_SCREEN_LEN == 4u * 128u,
               "CW Decode calibration screen must cover LCD pages 1 through 4");

static struct {
    const app_api_t *A;
    const char *morse;
    const uint32_t *decimalPlaces;
    bool running, down, candidate, fArm, redraw, follow, keyRedraw, morseRedraw;
    bool help;
    bool agc, speaker;
    bool linePending;
    uint8_t prevKey, margin, gapStage, sampleIndex;
    uint8_t code, depth, compact;
    uint16_t dotMs, textLen, top, limit;
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

static char *putu(char *out, uint32_t value)
{
    bool started = false;
    for (uint8_t i = 0u; i < DECIMAL_PLACES_LEN / 4u; i++) {
        const uint32_t place = g.decimalPlaces[i];
        uint8_t digit = 0u;
        while (value >= place) {
            value -= place;
            digit++;
        }
        if (digit || started || place == 1u) {
            *out++ = (char)('0' + digit);
            started = true;
        }
    }
    return out;
}

static char *puti(char *out, int32_t value)
{
    if (value < 0) {
        *out++ = '-';
        value = -value;
    }
    return putu(out, (uint32_t)value);
}

static void configureCwRx(void)
{
    uint32_t frequency = A->rx_freq();
    if (frequency > RX_PITCH_10HZ)
        frequency -= RX_PITCH_10HZ;

    A->bk_write(REG_FREQ_LOW, (uint16_t)frequency);
    A->bk_write(REG_FREQ_HIGH, (uint16_t)(frequency >> 16));
    const uint16_t rxLink = A->bk_read(REG_TX_LINK);
    A->bk_write(REG_TX_LINK, RX_LINK_OFF);
    A->bk_write(REG_TX_LINK, rxLink);

    /* Match CW Keyer's proven USB product-detector path. The displayed VFO
     * remains the carrier frequency while the receiver runs 700 Hz below it. */
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
    A->set_agc(g.agc);
    A->set_af(AF_USB);
    A->audio_path(g.speaker);
}

static void updateLimit(void)
{
    const uint16_t width = g.compact ? 32u : 18u;
    const uint16_t step = g.compact ? 6u : 8u;
    const uint16_t visibleRows = g.compact ? 6u : 5u;
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
    A->status_clear();
    A->print_inverse(ui + T_TITLE, 2u, 0u, true, true,
                     (uint8_t)(2u + T_TITLE_CHARS * 4u));
    if (g.agc)
        A->asset_read(BMP_AGC, A->status_line + AGC_X, BMP_AGC_LEN);
    unsigned marks = 0u;
    if (g.top)
        marks |= 1u;
    if (g.top < g.limit)
        marks |= 2u;
    A->asset_read((uint16_t)(BMP_SCROLL + marks * BMP_SCROLL_W),
                  A->status_line + SCROLL_X, BMP_SCROLL_W);
    if (g.fArm)
        A->asset_read(BMP_F, A->status_line + F_X, BMP_F_LEN);
    else if (g.speaker)
        A->asset_read(BMP_SPEAKER, A->status_line + F_X, BMP_SPEAKER_LEN);
    A->draw_battery();
}

static void renderDecoded(void)
{
    char row[33];
    uint16_t pos = 0u, vrow = g.compact ? 2u : 0u;
    const uint8_t width = g.compact ? 32u : 18u;
    const uint8_t step = g.compact ? 6u : 8u;
    const int32_t bodyTop = g.compact ? 2 : 0;
    const int32_t bodyBottom = g.compact ? (int32_t)BODY_BOTTOM - 1
                                         : (int32_t)BODY_BOTTOM;
    const int32_t glyphHeight = g.compact ? 5 : 7;
    const uint16_t bottomMask = g.compact ? 0x3Fu : 0x7Fu;

    while (pos < g.textLen) {
        uint8_t n = 0u;
        while (n < width && pos < g.textLen && g.text[pos] != '\n')
            row[n++] = g.text[pos++];
        row[n] = '\0';

        const int32_t y = (int32_t)vrow - (int32_t)g.top;
        if (n && y + glyphHeight > bodyTop && y < bodyBottom) {
            uint8_t *scratch = A->fb[6];
            if (g.compact)
                A->print_tiny(row, 0u, 48u, false, true);
            else
                A->print_normal(row, 0u, 0u, 6u);

            for (unsigned x = 0u; x < 128u; x++) {
                if (y < bodyTop) {
                    const uint8_t crop = (uint8_t)(bodyTop - y);
                    A->fb[0][x] |= (uint8_t)((scratch[x] >> crop) << bodyTop);
                } else {
                    const unsigned page = (unsigned)y >> 3;
                    const unsigned shift = (unsigned)y & 7u;
                    uint16_t bits = (uint16_t)scratch[x] << shift;
                    uint8_t *out = A->fb[page] + x;
                    if (page == 4u)
                        bits &= bottomMask;
                    *out |= (uint8_t)bits;
                    if (page < 4u)
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

static void drawCapsules(const char *labels, bool blit)
{
    char line[34], *out;
    if (blit) {
        for (unsigned x = 0u; x < 128u; x++) {
            A->fb[5][x] = 0u;
            A->fb[6][x] = 0u;
        }
    }

    out = put(line, labels + T_THR - T_RSSI);
    out = puti(out, g.threshold);
    *out = '\0';
    A->print_inverse(line, THR_CAPS_X, 5u, false, true, THR_CAPS_END);

    out = put(line, labels);
    out = puti(out, g.rssi);
    *out = '\0';
    A->print_inverse(line, RSSI_CAPS_X, 5u, false, true, RSSI_CAPS_END);

    out = put(line, labels + T_WPM - T_RSSI);
    out = putu(out, udiv(1200u, g.dotMs));
    *out = '\0';
    A->print_inverse(line, WPM_CAPS_X, 5u, false, true, WPM_CAPS_END);

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
    A->print_inverse(line, 2u, 6u, false, true, MORSE_CAPS_END);

    /* Match Beacon: RX frequency in 10 Hz units, shown as MHz with five
     * decimals and right-aligned in the normal font. */
    char *const end = putu(line, A->rx_freq());
    char *point = end;
    point[1] = '\0';
    for (uint8_t i = 0u; i < 5u; i++, point--)
        point[0] = point[-1];
    point[0] = '.';
    const unsigned len = (unsigned)(end - line) + 1u;
    A->print_normal(line, (uint8_t)(126u - len * 7u), 0u, 6u);
    if (blit) {
        A->blit_line(5u);
        A->blit_line(6u);
    }
}

static void drawHelp(const char *ui)
{
    char text[TEXT_MAX];
    A->status_clear();
    A->print_inverse(ui + T_TITLE, 2u, 0u, true, true,
                     (uint8_t)(2u + T_TITLE_CHARS * 4u));
    A->asset_read(T_HELP, text, TEXT_MAX);
    A->print_inverse(text, AGC_X, 0u, true, true,
                     (uint8_t)(AGC_X + 4u * 4u));
    A->draw_battery();
    for (uint8_t i = 0u; i < 6u; i++) {
        A->asset_read((uint16_t)(T_HELP_LEFT + i * T_HELP_LEFT_STRIDE),
                      text, TEXT_MAX);
        A->print_inverse(text, 2u, (uint8_t)(i + 1u), false, true, 46u);
        A->asset_read((uint16_t)(T_HELP_RIGHT + i * T_HELP_RIGHT_STRIDE),
                      text, TEXT_MAX);
        if (text[0])
            A->print_inverse(text, 79u, (uint8_t)(i + 1u), false, true, 127u);
    }
    A->asset_read(T_HELP_UNIT_1, text, TEXT_MAX);
    A->print_tiny(text, 56u, 41u, false, true);
    A->asset_read(T_HELP_UNIT_2, text, TEXT_MAX);
    A->print_tiny(text, 56u, 49u, false, true);
}

static void refreshCapsules(void)
{
    char labels[UI_SIZE - T_RSSI];
    A->asset_read(T_RSSI, labels, sizeof(labels));
    drawCapsules(labels, true);
}

static void draw(bool calibrating)
{
    char ui[UI_SIZE] __attribute__((aligned(4)));
    A->asset_read(0u, ui, UI_SIZE);
    A->display_clear();
    if (g.help) {
        drawHelp(ui);
    } else {
        drawStatus(ui);
        if (calibrating) {
            A->asset_read(BMP_CAL_SCREEN, A->fb[1], BMP_CAL_SCREEN_LEN);
        } else {
            if (g.textLen)
                renderDecoded();

            drawCapsules(ui + T_RSSI, false);
        }
    }

    A->blit_status();
    A->blit_full();
    g.redraw = false;
    g.morseRedraw = false;
}

static void calibrate(void)
{
    g.down = false;
    g.candidate = false;
    g.candidateAt = A->ticks_ms();
    clearSymbol();
    g.gapStage = 2u;
    g.rssi = A->rssi_dbm();
    g.noiseQ8 = (int32_t)g.rssi * 256;

    const uint32_t start = A->ticks_ms();
    uint32_t last = start;
    draw(true);
    while (A->ticks_ms() - start < CALIBRATE_MS) {
        A->delay_ms(SAMPLE_MS);
        const int32_t target = (int32_t)A->rssi_dbm() * 256;
        g.noiseQ8 += divPow2(target - g.noiseQ8, 3u);
        const uint32_t now = A->ticks_ms();
        if (now - last >= DRAW_MS) {
            last = now;
            A->backlight_update();
        }
    }

    g.rssi = (int16_t)divPow2(g.noiseQ8, 8u);
    g.threshold = (int16_t)(g.rssi + g.margin);
    g.sampleIndex = 0u;
    for (uint8_t i = 0u; i < 3u; i++)
        g.samples[i] = g.rssi;
    g.edgeAt = A->ticks_ms();
    g.lastDraw = 0u;
    g.redraw = true;
}

static void loadConfig(void)
{
    config_t cfg;
    A->cfg_load((uint8_t *)&cfg, sizeof(cfg));
    if (cfg.magic == CFG_MAGIC || cfg.magic == CFG_MAGIC_V1) {
        g.compact = (uint8_t)(cfg.compact == 1u);
        g.margin = cfg.margin >= MIN_MARGIN && cfg.margin <= MAX_MARGIN
            ? cfg.margin : DEFAULT_MARGIN;
        g.agc = cfg.agc <= 1u ? cfg.agc : true;
        g.speaker = cfg.magic == CFG_MAGIC && cfg.speaker <= 1u
            ? cfg.speaker : true;
    } else {
        g.margin = DEFAULT_MARGIN;
        g.agc = true;
        g.speaker = true;
    }
}

static void saveConfig(void)
{
    const config_t cfg = {
        CFG_MAGIC, g.compact, g.margin, g.agc, g.speaker
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
    asset_tables_t tables;
    /* History lives for the whole modal call but stays outside the 4 KiB
     * overlay image, following the larger-buffer overlay apps. */
    char text[TEXT_CAP + 1u];
    A = api;
    g.morse = tables.morse;
    g.decimalPlaces = tables.decimalPlaces;
    g.text = text;
    A->asset_read(MORSE_TREE, &tables, sizeof(tables));
    g.running = true;
    g.follow = true;
    g.help = false;
    g.prevKey = APP_KEY_INVALID;
    g.dotMs = DEFAULT_DOT_MS;
    loadConfig();
    updateLimit();
    A->backlight_on();
    configureCwRx();
    calibrate();

    while (g.running) {
        const uint32_t now = A->ticks_ms();
        sample(now);
        serviceGap(now);

        if (now - g.lastKey >= KEY_MS) {
            g.lastKey = now;
            handleKey(A->get_key());
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
            A->battery_sample();
        }
        A->backlight_update();
        A->delay_ms(SAMPLE_MS);
    }

    saveConfig();
    /* The loader's retune may keep its cached AGC setting, so restore the
     * receiver's normal automatic gain explicitly before returning. */
    A->set_agc(true);
    A->set_af(APP_AF_MUTE);
    A->audio_path(false);
}
