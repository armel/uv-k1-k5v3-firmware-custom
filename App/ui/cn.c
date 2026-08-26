/*
 * Chinese (GB2312) text rendering.
 *
 * Draws mixed ASCII / GB2312 strings into the frame buffer: ASCII uses the
 * built-in gFontBig (7 px wide, pitch 8), Chinese characters are read from
 * the external SPI flash font (16x16 px, pitch 16, see app/cnfont.h). Like
 * UI_PrintString, a 16 px character occupies two frame-buffer lines.
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

// Pixel width of pStr: 8 px per ASCII char (gFontBig pitch), 16 px per
// GB2312 char. Stray bytes that are not valid GB2312 pairs count as ASCII.
uint8_t UI_PrintStringCNWidth(const char *pStr)
{
    uint8_t Width = 0;

    for (const uint8_t *p = (const uint8_t *)pStr; *p; p++)
    {
        if (*p >= 0xA1u && p[1] >= 0xA1u)   // GB2312: two bytes in A1..FE
        {
            p++;
            Width += 16;
        }
        else
        {
            Width += 8;
        }
    }
    return Width;
}

// Draws pStr at frame-buffer line Line (and Line+1), starting at column X.
// Line must be <= 6 so that Line+1 stays inside gFrameBuffer.
uint8_t UI_PrintStringCN(const char *pStr, uint8_t Line, uint8_t X)
{
    const bool bFont = CN_FONT_Present();
    uint8_t    Glyph[32];
    uint8_t    x = X;

    for (const uint8_t *p = (const uint8_t *)pStr; *p; p++)
    {
        if (bFont && *p >= 0xA1u && p[1] >= 0xA1u)
        {
            if (CN_FONT_GetGlyph(*p, p[1], Glyph))
            {
                if (x + 16 <= LCD_WIDTH)
                {
                    memcpy(gFrameBuffer[Line + 0] + x, Glyph + 0, 16);
                    memcpy(gFrameBuffer[Line + 1] + x, Glyph + 16, 16);
                }
            }
            p++;
            x += 16;
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

#endif // ENABLE_FEAT_F4HWN_CN_FONT
