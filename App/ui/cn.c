/*
 * Chinese (GB2312) text rendering.
 *
 * Draws mixed ASCII / GB2312 strings into the frame buffer. Chinese
 * characters come from the external SPI flash shared font (16x16 px,
 * 白头佬-compatible, see app/cnfont.h) and are downsampled at draw time:
 * UI_PrintStringCN / UI_PrintStringCNSmall render 12x12, the tight
 * UI_PrintStringCNTight renders 8x8 on a single line.
 *
 * When no font is programmed (CN_FONT_Present() == false) the Chinese bytes
 * are skipped and the rest is drawn in ASCII, so a wrongly-set language
 * option can never render garbage.
 */

#include "cn.h"

#include <string.h>

#include "app/cnfont.h"
#include "driver/st7565.h"
#include "font.h"

#ifdef ENABLE_FEAT_F4HWN_CN_FONT

// Pixel width of pStr: 8 px per ASCII char (gFontBig pitch), 12 px per
// GB2312 char. Stray bytes that are not valid GB2312 pairs count as ASCII.
uint8_t UI_PrintStringCNWidth(const char *pStr)
{
    uint8_t Width = 0;

    for (const uint8_t *p = (const uint8_t *)pStr; *p; p++)
    {
        if (*p >= 0xA1u && p[1] >= 0xA1u)   // GB2312: two bytes in A1..FE
        {
            p++;
            Width += 12;
        }
        else
        {
            Width += 8;
        }
    }
    return Width;
}

// Fetches a 16x16 glyph from the shared font and downsamples it to 12x12.
// Uses a box filter: each output pixel covers a 4x4 sub-pixel grid on a
// 48x48 virtual image (source pixel = 3x3 sub-pixels). Threshold is half of
// the 16 sub-pixels so thin strokes are preserved without turning dots into
// blocks.
// pOut: 12 bytes upper page (rows 0..7) + 12 bytes lower (rows 8..11 in the
// low nibble). Returns false when the glyph is not in the font.
static bool CN_GetGlyph12(uint8_t Zone, uint8_t Pos, uint8_t *pOut)
{
    uint8_t Glyph[32];

    if (!CN_FONT_GetGlyph(Zone, Pos, Glyph))
        return false;

    memset(pOut, 0, 24);

    for (unsigned int dst_col = 0; dst_col < 12; dst_col++)
    {
        const unsigned int sub_x0 = dst_col * 4u;

        for (unsigned int dst_row = 0; dst_row < 12; dst_row++)
        {
            const unsigned int sub_y0 = dst_row * 4u;
            unsigned int       sum    = 0;

            for (unsigned int dy = 0; dy < 4; dy++)
            {
                const unsigned int src_row = (sub_y0 + dy) / 3u;
                const uint8_t      mask    = (uint8_t)(1u << (src_row & 7u));

                for (unsigned int dx = 0; dx < 4; dx++)
                {
                    const unsigned int src_col = (sub_x0 + dx) / 3u;
                    if (Glyph[(src_row >= 8u ? 16u : 0u) + src_col] & mask)
                        sum++;
                }
            }

            if (sum >= 8u)
                pOut[(dst_row >= 8u ? 12u : 0u) + dst_col] |= (1u << (dst_row & 7u));
        }
    }
    return true;
}

// Downsampling to 8x8 for the tight row. Each output pixel covers a 2x2
// source pixel block; keep it when at least half (2 of 4) are set.
// pOut: 8 bytes, one ST7565 page.
static bool CN_GetGlyph8(uint8_t Zone, uint8_t Pos, uint8_t *pOut)
{
    uint8_t Glyph[32];

    if (!CN_FONT_GetGlyph(Zone, Pos, Glyph))
        return false;

    memset(pOut, 0, 8);

    for (unsigned int dst_col = 0; dst_col < 8; dst_col++)
    {
        const unsigned int src_col0 = dst_col * 2u;

        for (unsigned int dst_row = 0; dst_row < 8; dst_row++)
        {
            const unsigned int src_row0 = dst_row * 2u;
            unsigned int       sum      = 0;

            for (unsigned int dy = 0; dy < 2; dy++)
            {
                const unsigned int src_row = src_row0 + dy;
                const uint8_t      mask    = (uint8_t)(1u << (src_row & 7u));

                for (unsigned int dx = 0; dx < 2; dx++)
                {
                    const unsigned int src_col = src_col0 + dx;
                    if (Glyph[(src_row >= 8u ? 16u : 0u) + src_col] & mask)
                        sum++;
                }
            }

            if (sum >= 2u)
                pOut[dst_col] |= (1u << dst_row);
        }
    }
    return true;
}

// Draws pStr at frame-buffer line Line (and Line+1), starting at column X.
// Line must be <= 6 so that Line+1 stays inside gFrameBuffer.
// ASCII uses gFontBig (16 px tall), Chinese glyphs are 12x12 spanning Line
// and the upper half of Line+1.
uint8_t UI_PrintStringCN(const char *pStr, uint8_t Line, uint8_t X)
{
    const bool bFont = CN_FONT_Present();
    uint8_t    Glyph[24];
    uint8_t    x = X;

    for (const uint8_t *p = (const uint8_t *)pStr; *p; p++)
    {
        if (bFont && *p >= 0xA1u && p[1] >= 0xA1u)
        {
            if (CN_GetGlyph12(*p, p[1], Glyph))
            {
                if (x + 12 <= LCD_WIDTH)
                {
                    memcpy(gFrameBuffer[Line + 0] + x, Glyph + 0, 12);
                    memcpy(gFrameBuffer[Line + 1] + x, Glyph + 12, 12);
                }
            }
            p++;
            x += 12;
        }
        else
        {   // ASCII (or a stray byte): the column advances either way,
            // like UI_PrintString does for spaces
            if (*p > ' ' && *p < 127)
            {
                const unsigned int index = *p - ' ' - 1;
                if (x + 8 <= LCD_WIDTH)
                {
                    memcpy(gFrameBuffer[Line + 0] + x, &gFontBig[index][0], 7);
                    memcpy(gFrameBuffer[Line + 1] + x, &gFontBig[index][7], 7);
                }
            }
            x += 8;
        }
    }
    return (uint8_t)(x - X);
}

// Pixel width of pStr in the small style: 7 px per ASCII char (gFontSmall
// pitch), 12 px per GB2312 char.
uint8_t UI_PrintStringCNSmallWidth(const char *pStr)
{
    uint8_t Width = 0;

    for (const uint8_t *p = (const uint8_t *)pStr; *p; p++)
    {
        if (*p >= 0xA1u && p[1] >= 0xA1u)   // GB2312: two bytes in A1..FE
        {
            p++;
            Width += 12;
        }
        else
        {
            Width += 7;
        }
    }
    return Width;
}

// Small variant of UI_PrintStringCN for the channel-name line: ASCII uses
// gFontSmall (6 px, one frame-buffer line, shifted 2 px down), Chinese
// glyphs are 12x12 and span Line plus the upper half of Line+1.
uint8_t UI_PrintStringCNSmall(const char *pStr, uint8_t Line, uint8_t X)
{
    const bool bFont = CN_FONT_Present();
    uint8_t    Glyph[24];
    uint8_t    x = X;

    for (const uint8_t *p = (const uint8_t *)pStr; *p; p++)
    {
        if (bFont && *p >= 0xA1u && p[1] >= 0xA1u)
        {
            if (CN_GetGlyph12(*p, p[1], Glyph))
            {
                if (x + 12 <= LCD_WIDTH)
                {
                    memcpy(gFrameBuffer[Line + 0] + x, Glyph + 0, 12);
                    memcpy(gFrameBuffer[Line + 1] + x, Glyph + 12, 12);
                }
            }
            p++;
            x += 12;
        }
        else
        {   // ASCII (or a stray byte), dropped 2 px to line up its center
            // with the taller Chinese glyphs
            if (*p > ' ' && *p < 127)
            {
                const unsigned int index = *p - ' ' - 1;
                if (x + 7 <= LCD_WIDTH)
                {
                    for (unsigned int i = 0; i < 6; i++)
                    {
                        const uint8_t b = gFontSmall[index][i];
                        gFrameBuffer[Line + 0][x + i] = (uint8_t)(b >> 2);
                        gFrameBuffer[Line + 1][x + i] = (uint8_t)(b << 6);
                    }
                }
            }
            x += 7;
        }
    }
    return (uint8_t)(x - X);
}

// Tight single-line variant for the dual-VFO name+frequency row: Chinese
// glyphs are downsampled to 8x8 and share one frame-buffer line with the
// small ASCII.
uint8_t UI_PrintStringCNTight(const char *pStr, uint8_t Line, uint8_t X)
{
    const bool bFont = CN_FONT_Present();
    uint8_t    Glyph[8];
    uint8_t    x = X;

    for (const uint8_t *p = (const uint8_t *)pStr; *p; p++)
    {
        if (bFont && *p >= 0xA1u && p[1] >= 0xA1u)
        {
            if (CN_GetGlyph8(*p, p[1], Glyph))
            {
                if (x + 8 <= LCD_WIDTH)
                    memcpy(gFrameBuffer[Line] + x, Glyph, 8);
            }
            p++;
            x += 8;
        }
        else
        {   // ASCII (or a stray byte), same convention as UI_PrintStringSmall
            if (*p > ' ' && *p < 127)
            {
                const unsigned int index = *p - ' ' - 1;
                if (x + 7 <= LCD_WIDTH)
                    memcpy(gFrameBuffer[Line] + x, &gFontSmall[index][0], 6);
            }
            x += 7;
        }
    }
    return (uint8_t)(x - X);
}

#endif // ENABLE_FEAT_F4HWN_CN_FONT
