/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#include <string.h>

#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "font.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "misc.h"
#include "settings.h"


void UI_GenerateChannelStringEx(char *pString, const bool bShowPrefix, const uint16_t ChannelNumber)
{
    if (gInputBoxIndex > 0) {
        for (unsigned int i = 0; i < 4; i++) {
            pString[i] = (gInputBox[i] == 10) ? '-' : gInputBox[i] + '0';
        }

        pString[4] = 0;
        return;
    }

    if (bShowPrefix) {
        // BUG here? Prefixed NULLs are allowed
        sprintf(pString, "CH-%04u", ChannelNumber + 1);
    } else if (ChannelNumber == MR_CHANNEL_LAST + 1) {
        strcpy(pString, "None");
    } else if (ChannelNumber == 0xFFFF) {
        strcpy(pString, "NULL");
    } else {
        sprintf(pString, "%04u", ChannelNumber + 1);
    }
}

void UI_PrintStringBuffer(const char *pString, uint8_t *buffer, const uint8_t *font)
{
    const size_t Length = strlen(pString);
    const unsigned int char_spacing = FONT_SMALL_WIDTH + 1u;
    for (size_t i = 0; i < Length; i++) {
        const unsigned int index = pString[i] - ' ' - 1;
        if (pString[i] > ' ' && pString[i] < 127) {
            const uint32_t offset = i * char_spacing + 1;
            uint16_t bit = index * FONT_SMALL_WIDTH * 7u;

            for (uint8_t column = 0; column < FONT_SMALL_WIDTH; column++) {
                const uint16_t byte = bit >> 3;
                const uint16_t packed = font[byte] | ((uint16_t)font[byte + 1u] << 8);
                buffer[offset + column] = (packed >> (bit & 7u)) & 0x7Fu;
                bit += 7u;
            }
        }
    }
}

// CW text carries each prosign as its two letters in lowercase (see CW_PROSIGN_SK),
// drawn as capitals with an underline. The usual mark is a bar over the letters, but
// the small font's capitals start on the top row of their line, so it goes under.
static bool IsProsignLetter(char c)
{
#ifdef ENABLE_CW_MODULATOR
    return c >= 'a' && c <= 'z';
#else
    (void)c;
    return false;  // no CW text to mark, so the underline code drops out
#endif
}

// Underline width px of a prosign letter's cell at the given row bit, running on
// across the gap to the next cell (pitch px on) when that's a prosign letter too
static void UnderlineProsignLetter(uint8_t *row, unsigned int width, unsigned int pitch, uint8_t bit, bool joinNext)
{
    const unsigned int end = joinNext ? pitch : width;
    for (unsigned int x = 0; x < end; x++)
        row[x] |= bit;
}

// cw: CW text, with a dotted zero and underlined prosign letters
static void PrintStringBig(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, uint8_t Width, bool cw)
{
    size_t i;
    size_t Length = strlen(pString);

    if (End > Start)
        Start += (((End - Start) - (Length * Width)) + 1) / 2;

    for (i = 0; i < Length; i++)
    {
        const unsigned int ofs     = (unsigned int)Start + (i * Width);
        const bool         prosign = cw && IsProsignLetter(pString[i]);
        const char         c       = prosign ? pString[i] - ('a' - 'A') : pString[i];
        if (c > ' ' && c < 127)
        {
            if (cw && c == '0') {
                memcpy(gFrameBuffer[Line + 0] + ofs, &gFontBigZeroDotted[0], 7);
                memcpy(gFrameBuffer[Line + 1] + ofs, &gFontBigZeroDotted[7], 7);
            } else {
                FONT_DrawBigGlyph(c - ' ' - 1,
                                  gFrameBuffer[Line + 0] + ofs,
                                  gFrameBuffer[Line + 1] + ofs);
            }
            if (prosign)  // capitals end on row 12 of 16, so row 14 leaves a gap
                UnderlineProsignLetter(gFrameBuffer[Line + 1] + ofs, 7, Width, 0x40, IsProsignLetter(pString[i + 1]));
        }
    }
}

void UI_PrintString(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, uint8_t Width)
{
    PrintStringBig(pString, Start, End, Line, Width, false);
}

void UI_PrintStringCW(const char *pString, uint8_t Start, uint8_t End, uint8_t Line)
{
    PrintStringBig(pString, Start, End, Line, 8, true);
}

void UI_PrintStringSmall(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, const uint8_t *font)
{
    const size_t Length = strlen(pString);
    const unsigned int char_spacing = FONT_SMALL_WIDTH + 1u;

    if (End > Start) {
        Start += (((End - Start) - Length * char_spacing) + 1) / 2;
    }

    UI_PrintStringBuffer(pString, gFrameBuffer[Line] + Start, font);
}


void UI_PrintStringSmallNormal(const char *pString, uint8_t Start, uint8_t End, uint8_t Line)
{
    UI_PrintStringSmall(pString, Start, End, Line, gFontSmallPacked);
}

// Frequency-font glyph for c, or NULL for the few CW characters it lacks
static const uint8_t *CWLargeGlyph(char c)
{
    if (c == '0')
        return gFontBigDigitZeroDotted;
    if (c == 'O')
        return gFontBigDigits[0];
    if (c > '0' && c <= '9')
        return gFontBigDigits[c - '0'];
    if (c == '-')
        return gFontBigDigits[10];

    const char *p = (c != '\0') ? strchr(gFontBigTextChars, c) : NULL;
    return p ? gFontBigText[p - gFontBigTextChars] : NULL;
}

void UI_PrintStringCWLarge(const char *pString, uint8_t Start, uint8_t Line)
{
    for (size_t i = 0; pString[i] != '\0'; i++) {
        const bool         prosign = IsProsignLetter(pString[i]);
        const char         c       = prosign ? pString[i] - ('a' - 'A') : pString[i];
        const unsigned int ofs     = Start + i * UI_CW_LARGE_PITCH;
        const uint8_t     *glyph   = CWLargeGlyph(c);

        if (glyph) {
            memcpy(gFrameBuffer[Line + 0] + ofs, glyph, 10);
            memcpy(gFrameBuffer[Line + 1] + ofs, glyph + 10, 10);
        } else if (c > ' ' && c < 127) {
            // e.g. '&': the regular big font, centered in the cell
            FONT_DrawBigGlyph(c - ' ' - 1,
                              gFrameBuffer[Line + 0] + ofs + 1,
                              gFrameBuffer[Line + 1] + ofs + 1);
        }
        if (prosign)  // capitals end on row 14 of 16
            UnderlineProsignLetter(gFrameBuffer[Line + 1] + ofs, 10, UI_CW_LARGE_PITCH, 0x80, IsProsignLetter(pString[i + 1]));
    }
}

void UI_PrintStringSmallCW(const char *pString, uint8_t Start, uint8_t Line)
{
    const unsigned int char_spacing = sizeof(gFontSmallZeroDotted) + 1;

    UI_PrintStringSmallNormal(pString, Start, 0, Line);
    // Redraw each zero and prosign letter in the slot UI_PrintStringBuffer gave it
    for (size_t i = 0; pString[i] != '\0'; i++) {
        uint8_t *cell = gFrameBuffer[Line] + Start + 1 + i * char_spacing;
        if (pString[i] == '0') {
            memcpy(cell, gFontSmallZeroDotted, sizeof(gFontSmallZeroDotted));
        } else if (IsProsignLetter(pString[i])) {
            // capitals fill rows 0-6, so the underline on row 7 touches them
            const char capital[2] = {pString[i] - ('a' - 'A'), '\0'};
            UI_PrintStringBuffer(capital, cell - 1, gFontSmallPacked);  // draws from cell + 0
            UnderlineProsignLetter(cell, FONT_SMALL_WIDTH, char_spacing, 0x80, IsProsignLetter(pString[i + 1]));
        }
    }
}

void UI_PrintStringSmallNormalInverse(const char *pString, uint8_t Start, uint8_t End, uint8_t Line)
{
    // First draw the string normally
    UI_PrintStringSmallNormal(pString, Start, End, Line);

    // Now invert the framebuffer bits for the rendered area
    uint8_t len = strlen(pString);
    uint8_t char_width = 7; // small font is typically 6px wide

    uint8_t x_start = Start;
    uint8_t x_end   = Start + (len * char_width) + 1;

    if (End != 0 && x_end > End)
        x_end = End;

    //gFrameBuffer[Line][x_start - 2] ^= 0x3E;
    gFrameBuffer[Line][x_start - 1] ^= 0x7F;
    //gFrameBuffer[Line][x_start - 1] ^= 0xFF;
    for (uint8_t x = x_start; x < x_end; x++)
    {
        gFrameBuffer[Line][x] ^= 0xFF;
        gFrameBuffer[Line - 1][x] ^= 0x80;
    }
    //gFrameBuffer[Line][x_end + 0] ^= 0xFF;
    gFrameBuffer[Line][x_end + 0] ^= 0x7F;
    //gFrameBuffer[Line][x_end + 1] ^= 0x3E;
}


void UI_PrintStringSmallBold(const char *pString, uint8_t Start, uint8_t End, uint8_t Line)
{
#ifdef ENABLE_SMALL_BOLD
    const uint8_t *font = gFontSmallBoldPacked;
#else
    const uint8_t *font = gFontSmallPacked;
#endif

    UI_PrintStringSmall(pString, Start, End, Line, font);
}

void UI_PrintStringSmallBufferNormal(const char *pString, uint8_t * buffer)
{
    UI_PrintStringBuffer(pString, buffer, gFontSmallPacked);
}

void UI_PrintStringSmallBufferBold(const char *pString, uint8_t * buffer)
{
#ifdef ENABLE_SMALL_BOLD
    const uint8_t *font = gFontSmallBoldPacked;
#else
    const uint8_t *font = gFontSmallPacked;
#endif
    UI_PrintStringBuffer(pString, buffer, font);
}

void UI_DisplayFrequency(const char *string, uint8_t X, uint8_t Y, bool center)
{
    const unsigned int char_width  = 13;
    uint8_t           *pFb0        = gFrameBuffer[Y] + X;
    uint8_t           *pFb1        = pFb0 + 128;
    bool               bCanDisplay = false;

    uint8_t len = strlen(string);
    for(int i = 0; i < len; i++) {
        char c = string[i];
        if(c=='-') c = '9' + 1;
        if (bCanDisplay || c != ' ')
        {
            bCanDisplay = true;
            if(c>='0' && c<='9' + 1) {
                memcpy(pFb0 + 2, gFontBigDigits[c-'0'],                  char_width - 3);
                memcpy(pFb1 + 2, gFontBigDigits[c-'0'] + char_width - 3, char_width - 3);
            }
            else if(c=='.') {
                *pFb1 = 0x60; pFb0++; pFb1++;
                *pFb1 = 0x60; pFb0++; pFb1++;
                *pFb1 = 0x60; pFb0++; pFb1++;
                continue;
            }

        }
        else if (center) {
            pFb0 -= 6;
            pFb1 -= 6;
        }
        pFb0 += char_width;
        pFb1 += char_width;
    }
}

/*
void UI_DisplayFrequency(const char *string, uint8_t X, uint8_t Y, bool center)
{
    const unsigned int char_width  = 13;
    uint8_t           *pFb0        = gFrameBuffer[Y] + X;
    uint8_t           *pFb1        = pFb0 + 128;
    bool               bCanDisplay = false;

    if (center) {
        uint8_t len = 0;
        for (const char *ptr = string; *ptr; ptr++)
            if (*ptr != ' ') len++; // Ignores spaces for centering

        X -= (len * char_width) / 2; // Centering adjustment
        pFb0 = gFrameBuffer[Y] + X;
        pFb1 = pFb0 + 128;
    }

    for (; *string; string++) {
        char c = *string;
        if (c == '-') c = '9' + 1; // Remap of '-' symbol

        if (bCanDisplay || c != ' ') {
            bCanDisplay = true;
            if (c >= '0' && c <= '9' + 1) {
                memcpy(pFb0 + 2, gFontBigDigits[c - '0'], char_width - 3);
                memcpy(pFb1 + 2, gFontBigDigits[c - '0'] + char_width - 3, char_width - 3);
            } else if (c == '.') {
                memset(pFb1, 0x60, 3); // Replaces the three assignments
                pFb0 += 3;
                pFb1 += 3;
                continue;
            }
        }
        pFb0 += char_width;
        pFb1 += char_width;
    }
}
*/

void UI_DrawPixelBuffer(uint8_t (*buffer)[128], uint8_t x, uint8_t y, bool black)
{
    const uint8_t pattern = 1 << (y % 8);
    if(black)
        buffer[y/8][x] |= pattern;
    else
        buffer[y/8][x] &= ~pattern;
}

static void sort(int16_t *a, int16_t *b)
{
    if(*a > *b) {
        int16_t t = *a;
        *a = *b;
        *b = t;
    }
}

#ifdef ENABLE_FEAT_F4HWN
    /*
    void UI_DrawLineDottedBuffer(uint8_t (*buffer)[128], int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool black)
    {
        if(x2==x1) {
            sort(&y1, &y2);
            for(int16_t i = y1; i <= y2; i+=2) {
                UI_DrawPixelBuffer(buffer, x1, i, black);
            }
        } else {
            const int multipl = 1000;
            int a = (y2-y1)*multipl / (x2-x1);
            int b = y1 - a * x1 / multipl;

            sort(&x1, &x2);
            for(int i = x1; i<= x2; i+=2)
            {
                UI_DrawPixelBuffer(buffer, i, i*a/multipl +b, black);
            }
        }
    }
    */

    void PutPixel(uint8_t x, uint8_t y, bool fill) {
      UI_DrawPixelBuffer(gFrameBuffer, x, y, fill);
    }

    void PutPixelStatus(uint8_t x, uint8_t y, bool fill) {
      UI_DrawPixelBuffer(&gStatusLine, x, y, fill);
    }

    void GUI_DisplaySmallest(const char *pString, uint8_t x, uint8_t y,
                                    bool statusbar, bool fill) {
      uint8_t c;
      uint8_t pixels;
      const uint8_t *p = (const uint8_t *)pString;

      while ((c = *p++) && c != '\0') {
        c -= 0x20;
        for (int i = 0; i < 3; ++i) {
          pixels = gFont3x5[c][i];
          for (int j = 0; j < 6; ++j) {
            if (pixels & 1) {
              if (statusbar)
                PutPixelStatus(x + i, y + j, fill);
              else
                PutPixel(x + i, y + j, fill);
            }
            pixels >>= 1;
          }
        }
        x += 4;
      }
    }

    void GUI_DisplaySmallestInverse(const char *pString, uint8_t x, uint8_t Line,
                                bool statusbar, bool fill, uint8_t end)
    {
        // First draw the string normally
        GUI_DisplaySmallest(pString, x, (Line * 8) + 1, statusbar, fill);

        // Now invert the framebuffer/statusline bits for the rendered area
        uint8_t start = (x - 2);
        uint8_t *buffer = statusbar ? gStatusLine : gFrameBuffer[Line];

        buffer[start] ^= 0x3E;
        for (uint8_t i = start + 1; i < end; i++) {
            buffer[i] ^= 0x7F;
        }
        buffer[end] ^= 0x3E;
    }

    void UI_DisplayUnlockKeyboard(uint8_t shift) {
        if (gEeprom.KEY_LOCK && gKeypadLocked > 0)
        {   // tell user how to unlock the keyboard
            
            //memcpy(gFrameBuffer[shift] + 2, gFontKeyLock, sizeof(gFontKeyLock));
            UI_PrintStringSmallBold("UNLOCK KEYBOARD", 12, 0, shift);
            //memcpy(gFrameBuffer[shift] + 120, gFontKeyLock, sizeof(gFontKeyLock));

            /*
            for (uint8_t i = 12; i < 116; i++)
            {
                gFrameBuffer[shift][i] ^= 0xFF;
            }
            */
        }
    }

    bool IsEmptyName(const char *name, uint8_t len) {
        if (name[0] == '\0' || name[0] == '\xff')
            return true;
        for (uint8_t i = 0; i < len; i++) {
            if (name[i] != ' ' && name[i] != '\xff' && name[i] != '\0')
                return false;
        }
        return true;
    }
#endif
    
void UI_DrawLineBuffer(uint8_t (*buffer)[128], int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool black)
{
    if(x2==x1) {
        sort(&y1, &y2);
        for(int16_t i = y1; i <= y2; i++) {
            UI_DrawPixelBuffer(buffer, x1, i, black);
        }
    } else {
        const int multipl = 1000;
        int a = (y2-y1)*multipl / (x2-x1);
        int b = y1 - a * x1 / multipl;

        sort(&x1, &x2);
        for(int i = x1; i<= x2; i++)
        {
            UI_DrawPixelBuffer(buffer, i, i*a/multipl +b, black);
        }
    }
}

void UI_DrawRectangleBuffer(uint8_t (*buffer)[128], int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool black)
{
    UI_DrawLineBuffer(buffer, x1,y1, x1,y2, black);
    UI_DrawLineBuffer(buffer, x1,y1, x2,y1, black);
    UI_DrawLineBuffer(buffer, x2,y1, x2,y2, black);
    UI_DrawLineBuffer(buffer, x1,y2, x2,y2, black);
}


void UI_DisplayPopup(const char *string)
{
    UI_DisplayClear();

    // for(uint8_t i = 1; i < 5; i++) {
    //  memset(gFrameBuffer[i]+8, 0x00, 111);
    // }

    // for(uint8_t x = 10; x < 118; x++) {
    //  UI_DrawPixelBuffer(x, 10, true);
    //  UI_DrawPixelBuffer(x, 46-9, true);
    // }

    // for(uint8_t y = 11; y < 37; y++) {
    //  UI_DrawPixelBuffer(10, y, true);
    //  UI_DrawPixelBuffer(117, y, true);
    // }
    // DrawRectangle(9,9, 118,38, true);
    UI_PrintString(string, 9, 118, 2, 8);
    UI_PrintStringSmallNormal("Press EXIT", 9, 118, 6);
}

void UI_DisplayClear(void)
{
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
}

void UI_StatusClear(void)
{
    memset(gStatusLine, 0, sizeof(gStatusLine));
}

#if defined(ENABLE_FEAT_F4HWN_MULTIBOOT) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
/* Shared bottom row for the Apps and Multiboot selectors. The EXIT action
 * ends at x=124; both key capsules and labels keep their original positions. */
void UI_DrawMenuKeyHints(const char *act_menu, const char *act_exit)
{
    const uint8_t sp = 6u;
    const uint8_t ae = (uint8_t)strlen(act_exit);
    const uint8_t xm = 4u;
    const uint8_t xe = (uint8_t)(124u - ae * 4u - sp - 16u);

    GUI_DisplaySmallestInverse("MENU", xm, 6, false, true, (uint8_t)(xm + 16u));
    GUI_DisplaySmallest(act_menu, (uint8_t)(xm + 16u + sp), 49, false, true);

    GUI_DisplaySmallestInverse("EXIT", xe, 6, false, true, (uint8_t)(xe + 16u));
    GUI_DisplaySmallest(act_exit, (uint8_t)(xe + 16u + sp), 49, false, true);
}
#endif
