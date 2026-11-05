// SPDX-License-Identifier: Apache-2.0
#include "ui/ui_plus.h"

#ifdef ENABLE_FEAT_F4HWN

#include <string.h>

#include "driver/st7565.h"
#include "font.h"
#include "ui/gui_style.h"
#include "ui/helper.h"

static void DrawText(const char *text, uint8_t x, uint8_t y, bool bold,
                     uint8_t width, uint8_t height)
{
    const uint8_t *font = gFontSmallPacked;
#ifdef ENABLE_SMALL_BOLD
    if (bold)
        font = gFontSmallBoldPacked;
#else
    (void)bold;
#endif

    // Names expand the existing glyphs to 7x10; frequencies retain their native
    // 6x7 size. Both can start between LCD pages without losing source pixels.
    if (y > FRAME_LINES * 8u - height)
        return;

    for (unsigned int cursor = x; *text && cursor <= LCD_WIDTH - width;
         text++, cursor += width + 1u)
    {
        const uint8_t c = (uint8_t)*text;
        if (c <= ' ' || c >= 127u)
            continue;

        for (uint8_t column = 0; column < width; column++)
        {
            // Exact enlargement at 2x and above keeps both stems even and
            // never samples the next glyph. Preserve existing smaller layouts.
            const uint8_t sourceColumn = width >= 12u ? column * 6u / width :
                                         (column * 6u + width / 2u) / width;
            const uint16_t bit = ((c - '!') * FONT_SMALL_WIDTH + sourceColumn) * 7u;
            const uint16_t packed = font[bit >> 3] |
                                   ((uint16_t)font[(bit >> 3) + 1u] << 8);
            const uint8_t pixels = (packed >> (bit & 7u)) & 0x7Fu;
            uint32_t expanded = 0;
            for (uint8_t row = 0; row < height; row++)
            {
                const uint8_t sourceRow = (row * 7u + 3u) / height;
                if (pixels & (1u << sourceRow))
                    expanded |= 1u << row;
            }
            const uint32_t shifted = (uint32_t)expanded << (y & 7u);

            const uint8_t pages = ((y & 7u) + height + 7u) / 8u;
            for (uint8_t page = 0; page < pages; page++)
                gFrameBuffer[y / 8u + page][cursor + column] |= shifted >> (page * 8u);
        }
    }
}

void UI_PLUS_DrawName(const char *text, uint8_t x, uint8_t y, bool bold)
{
    DrawText(text, x, y, bold, 7, 10);
}

void UI_PLUS_DrawFrequency(const char *text, uint8_t x, uint8_t y)
{
    DrawText(text, x, y, false, 6, 7);
}

void UI_PLUS_DrawChannelNumber(const char *text, uint8_t line)
{
    // Three digits normally; retain all four for 1000..1024 and channel entry.
    // The inverted badge is 15 or 19 pixels wide, ending before the frequency.
    GUI_DisplaySmallestInverse(text, 2, line + 1u, false, true,
                               2u + strlen(text) * 4u);
}

void UI_PLUS_PrintStatus(const char *text, uint8_t x, uint8_t line, uint8_t layout)
{
#if UI_PLUS_ENABLED
    if (layout == UI_PLUS_SINGLE_LAYOUT)
        GUI_DisplaySmallest(text, x, UI_PLUS_SINGLE_STATUS_Y, false, true);
    else if (layout)
        GUI_DisplaySmallest(text, x, line * 8u + UI_PLUS_STATUS_Y, false, true);
    else
#else
    (void)layout;
#endif
        UI_PrintStringSmallNormal(text, LCD_WIDTH + x, 0, line + 1u);
}

void UI_PLUS_DrawSingleName(const char *text)
{
    // Stacked CH/number leaves room for ten wider glyphs and the TX-lock icon.
    DrawText(text, 21, 1, true, 8, 18);
}

void UI_PLUS_DrawSingleFrequency(const char *text)
{
    // Four MHz digits need two more columns of clearance from the list badge.
    DrawText(text, strlen(text) > 9u ? 33 : 35, 21, false, 7, 10);
}

void UI_PLUS_DrawIcon(const uint8_t *glyph, uint8_t width, uint8_t x, uint8_t y)
{
    for (uint8_t column = 0; column < width; column++)
        for (uint8_t row = 0; row < 7; row++)
            PutPixel(x + column, y + row, (glyph[column] & (1u << row)) != 0);
}

void UI_PLUS_DrawSingleList(const char *text, uint8_t x)
{
    GUI_DisplaySmallest(text, x, 24, false, true);
    for (uint8_t column = x - 2; column <= 127; column++) {
        const uint8_t mask = (column == x - 2 || column == 127) ? 0x3e : 0x7f;
        for (uint8_t row = 0; row < 7; row++)
            if (mask & (1u << row))
                gFrameBuffer[(23u + row) / 8u][column] ^= 1u << ((23u + row) % 8u);
    }
}

void UI_PLUS_DrawSinglePowerBars(unsigned int level)
{
    if (level > 6u)
        level = 6u;
    for (uint8_t i = 0; i <= level; i++)
        for (uint8_t x = 0; x < 2; x++)
            for (uint8_t y = 0; y < 5; y++)
                PutPixel(2u + i * 3u + x, UI_PLUS_SINGLE_STATUS_Y + y, true);
}

void UI_PLUS_FinishSingleActivity(const uint8_t *savedPage, bool compress)
{
    for (unsigned x = 0; x < LCD_WIDTH; x++) {
        const uint8_t source = gFrameBuffer[5][x];
        // Merge the middle two rows instead of dropping thin features such
        // as a minus sign. Scan progress already fits in six rows.
        const uint8_t pixels = compress ?
            (source & 0x03u) | ((source & 0x0cu) ? 0x04u : 0u) | ((source & 0x70u) >> 1) :
            source & 0x3fu;
        gFrameBuffer[4][x] = (gFrameBuffer[4][x] & 0x07u) | (pixels << 3);
        gFrameBuffer[5][x] = (savedPage[x] & 0xfeu) | (pixels >> 5);
    }
}

void UI_PLUS_DrawPowerBars(unsigned int level, uint8_t line)
{
    if (level > 6u)
        level = 6u;

    // Keep the seven power steps, confined to the five-pixel status strip.
    // The normal bars reach into the enlarged frequency's last pixel row.
    for (uint8_t i = 0; i <= level; i++)
    {
        const uint8_t height = 1u + i * 4u / 6u;
        const uint8_t bar = ((1u << height) - 1u) << (8u - height);
        memset(gFrameBuffer[line + 2u] + 2u + i * 3u, bar, 2u);
    }
}

bool UI_PLUS_LowerChannelA(void)
{
    // RX/TX meters are drawn after placement. The caller excludes scanning
    // and reserved center-line uses. Also
    // preserve overlays that draw here without setting center_line (key lock,
    // full-watch labels). No part of B or the top status bar is touched.
    for (unsigned int x = 0; x < LCD_WIDTH; x++)
        if (gFrameBuffer[3][x] != 0)
            return false;

    for (unsigned int x = 0; x < LCD_WIDTH; x++)
    {
        const uint32_t column = ((uint32_t)gFrameBuffer[0][x] |
                               ((uint32_t)gFrameBuffer[1][x] << 8) |
                               ((uint32_t)gFrameBuffer[2][x] << 16)) << UI_PLUS_A_SHIFT;
        for (uint8_t page = 0; page < 4; page++)
            gFrameBuffer[page][x] = column >> (page * 8u);
    }
    return true;
}

// Reuse the radio's 10x14 numeric glyphs at a common 22-pixel height.
static void DrawLargeDigit(char c, uint8_t cursor, uint8_t width)
{
    const uint8_t *digit = gFontBigDigits[c - '0'];
    for (uint8_t x = 0; x < width; ++x) {
        const uint8_t sourceX = x * 10u / width;
        const uint16_t bits = digit[sourceX] | ((uint16_t)digit[sourceX + 10] << 8);
        for (uint8_t y = 0; y < 22; ++y)
            if (bits & (1u << (1u + y * 14u / 22u)))
                PutPixel(cursor + x, 11 + y, true);
    }
}

void UI_PLUS_DrawLargeName(const char *text)
{
    const size_t length = strlen(text);
    if (length == 0u || length > 10u)
        return;

    // Match the channel-number readout height. Ten characters fit at 11px;
    // shorter names use 12px glyphs, without stretching short labels to fill.
    const uint8_t glyphWidth = length == 10u ? 11u : 12u;
    const uint8_t width = length * (glyphWidth + 1u) - 1u;
    DrawText(text, (LCD_WIDTH - width) / 2u, 11, true, glyphWidth, 22);
}

void UI_PLUS_DrawLargeChannel(const char *text)
{
    const size_t length = strlen(text);
    if (length != 3u && length != 4u)
        return;
    for (size_t i = 0; i < length; ++i)
        if (text[i] < '0' || text[i] > '9')
            return;

    // Natural-width digits, with a fixed-size CH prefix and a clear gap.
    // Three/four-digit identifiers occupy 84/102 pixels without stretching.
    const uint8_t width = 25u + 7u + length * 18u - 2u;
    uint8_t cursor = (128u - width) / 2u;
    DrawText("CH", cursor, 11, true, 12, 22);
    cursor += 32;
    for (; *text; ++text, cursor += 18)
        DrawLargeDigit(*text, cursor, 16);
}

void UI_PLUS_DrawLargeFrequency(const char *text)
{
    const size_t length = strlen(text);
    // UI_FormatFrequency supplies ddd.ddddd or dddd.ddddd. Keep unexpected
    // inputs outside the renderer rather than wrapping its pixel coordinates.
    if ((length != 9u && length != 10u) || text[length - 6u] != '.')
        return;
    const uint8_t count = length - 1u;
    const uint8_t digitWidth = count > 8u ? 11u : 12u;
    const uint8_t totalWidth = count * (digitWidth + 2u) + 2u;
    uint8_t cursor = (128u - totalWidth) / 2u;
    for (; *text; ++text) {
        if (*text == '.') {
            for (uint8_t x = 0; x < 3; ++x)
                for (uint8_t y = 19; y < 22; ++y)
                    PutPixel(cursor + x, 11 + y, true);
            cursor += 4;
            continue;
        }
        if (*text < '0' || *text > '9')
            return;
        DrawLargeDigit(*text, cursor, digitWidth);
        cursor += digitWidth + 2;
    }
}
// Reuse the NAME+FREQ block, badges and status strip with one 14px readout.
void UI_PLUS_DrawDualFrequency(const char *text, uint8_t line)
{
    const size_t length = strlen(text);
    if ((length != 9u && length != 10u) || text[length - 6u] != '.')
        return;
    const uint8_t width = length == 10u ? 7u : 8u;
    uint8_t cursor = 34;
    const uint8_t top = line * 8u + 2u;
    for (; *text; ++text) {
        if (*text == '.') {
            for (uint8_t x = 0; x < 2; ++x)
                for (uint8_t y = 12; y < 14; ++y)
                    PutPixel(cursor + x, top + y, true);
            cursor += 3;
            continue;
        }
        if (*text < '0' || *text > '9')
            return;
        const uint8_t *digit = gFontBigDigits[*text - '0'];
        for (uint8_t x = 0; x < width; ++x) {
            const uint8_t sourceX = x * 10u / width;
            const uint16_t bits = digit[sourceX] | ((uint16_t)digit[sourceX + 10] << 8);
            for (uint8_t y = 0; y < 14; ++y)
                if (bits & (1u << (y + 1u)))
                    PutPixel(cursor + x, top + y, true);
        }
        cursor += width + 1u;
    }
}

void UI_PLUS_DrawDualText(const char *text, uint8_t line)
{
    const size_t length = strlen(text);
    if (length == 0u || length > 10u)
        return;
    const uint8_t glyphWidth = length == 10u ? 7u : 8u;
    const uint8_t width = length * (glyphWidth + 1u) - 1u;
    // The scan history ends at x30; the scan-list badge begins at x113.
    DrawText(text, 32u + (80u - width) / 2u, line * 8u + 2u, true, glyphWidth, 14);
}
#endif
