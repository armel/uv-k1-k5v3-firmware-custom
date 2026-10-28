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
#include "fw_assets.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "misc.h"
#include "settings.h"

static const char gUiText[UI_TEXT_COUNT][UI_TEXT_SIZE] FW_ASSET = {
    [UI_TEXT_VFO_NORMAL]           = "",
    [UI_TEXT_VFO_BUSY]             = "BUSY",
    [UI_TEXT_VFO_BAT_LOW]          = "BAT LOW",
    [UI_TEXT_VFO_TX_DISABLE]       = "TX DISABLE",
    [UI_TEXT_VFO_TIMEOUT]          = "TIMEOUT",
    [UI_TEXT_VFO_VOLT_HIGH]        = "VOLT HIGH",
    [UI_TEXT_LOW_BATTERY]          = "LOW BATTERY",
    [UI_TEXT_SCAN_LIST]            = "SCAN LIST ",
    [UI_TEXT_TRIPLE_WATCH]         = "TRIPLE WATCH",
    [UI_TEXT_QUAD_WATCH]           = "QUAD WATCH",
    [UI_TEXT_UNLOCK_KEYBOARD]      = "UNLOCK KEYBOARD",
    [UI_TEXT_SCANNER_SAVE_PROMPT]  = "Save?",
    [UI_TEXT_SCANNER_SAVE_PREFIX]  = "Save:",
    [UI_TEXT_SCANNER_SEARCH_FREQ]  = "Search Freq",
    [UI_TEXT_SCANNER_SEARCH_TONE]  = "Search Tone",
    [UI_TEXT_SCANNER_COMPLETE]     = "Scan Complete",
    [UI_TEXT_SCANNER_FAILED]       = "Scan Failed",
    [UI_TEXT_SCANNER_FREQ_UNKNOWN] = "Freq:---.-----",
    [UI_TEXT_SCANNER_TONE_UNKNOWN] = "Tone:---",
    [UI_TEXT_SCANNER_TONE_NONE]    = "Tone:None",
    [UI_TEXT_APP_TITLE]            = "F4HWN APPS",
    [UI_TEXT_APP_ERROR]            = "APP ERROR",
    [UI_TEXT_PRESS_ANY_KEY]        = "Press any key",
    [UI_TEXT_NO_APPS]              = "NO APPS",
    [UI_TEXT_BAD_SLOT]             = "BAD SLOT",
    [UI_TEXT_NO_APP]               = "NO APP",
    [UI_TEXT_UPDATE_APP]           = "UPDATE APP",
    [UI_TEXT_UPDATE_FIRMWARE]      = "UPDATE FIRMWARE",
    [UI_TEXT_REINSTALL_APP]        = "REINSTALL APP",
    [UI_TEXT_AUTH]                 = "AUTH",
    [UI_TEXT_NOT_SUPPORTED]        = "NOT SUPPORTED",
    [UI_TEXT_ERROR]                = "ERROR",
};

void UI_LoadText(char *buffer, uint32_t size, UI_Text_t text)
{
    if (size == 0u)
        return;

    if ((unsigned)text >= UI_TEXT_COUNT)
    {
        buffer[0] = '\0';
        return;
    }

    FW_ASSETS_ReadStr(buffer, gUiText[text], MIN(size, sizeof(gUiText[0])));
}


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
#ifdef ENABLE_FEAT_F4HWN_FW_ASSETS
            FW_ASSETS_Read(font + index * FONT_SMALL_WIDTH,
                           buffer + offset, FONT_SMALL_WIDTH);
#else
            uint16_t bit = index * FONT_SMALL_WIDTH * 7u;

            for (uint8_t column = 0; column < FONT_SMALL_WIDTH; column++) {
                const uint16_t byte = bit >> 3;
                const uint16_t packed = font[byte] | ((uint16_t)font[byte + 1u] << 8);
                buffer[offset + column] = (packed >> (bit & 7u)) & 0x7Fu;
                bit += 7u;
            }
#endif
        }
    }
}

#ifdef ENABLE_FEAT_F4HWN_SET_FNT
void UI_PrintStringWithFont(const char *pString, uint8_t Start, uint8_t End,
                            uint8_t Line, uint8_t Width, uint8_t font)
#else
void UI_PrintString(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, uint8_t Width)
#endif
{
    size_t i;
    size_t Length = strlen(pString);

    if (End > Start)
    {
        const size_t available = (size_t)End - Start + 1u;
        const size_t required = Length * Width;

        if (available > required)
            Start += (available - required) / 2u;
    }

    for (i = 0; i < Length; i++)
    {
        const unsigned int ofs   = (unsigned int)Start + (i * Width);
        if (pString[i] > ' ' && pString[i] < 127)
        {
            const unsigned int index = pString[i] - ' ' - 1;
            #ifdef ENABLE_FEAT_F4HWN_SET_FNT
                FONT_DrawBigGlyphWithVariant(font, index,
                                             gFrameBuffer[Line + 0] + ofs,
                                             gFrameBuffer[Line + 1] + ofs);
            #else
                FONT_DrawBigGlyph(index,
                                  gFrameBuffer[Line + 0] + ofs,
                                  gFrameBuffer[Line + 1] + ofs);
            #endif
        }
    }
}

#ifdef ENABLE_FEAT_F4HWN_SET_FNT
void UI_PrintString(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, uint8_t Width)
{
    UI_PrintStringWithFont(pString, Start, End, Line, Width, UI_FONT_SETTING);
}
#endif

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
    UI_PrintStringSmall(pString, Start, End, Line, FONT_GetSmallFont(UI_FONT_SETTING, false));
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
    UI_PrintStringSmall(pString, Start, End, Line, FONT_GetSmallFont(UI_FONT_SETTING, true));
}

void UI_PrintStringSmallBufferNormal(const char *pString, uint8_t * buffer)
{
    UI_PrintStringBuffer(pString, buffer, FONT_GetSmallFont(UI_FONT_SETTING, false));
}

void UI_PrintStringSmallBufferBold(const char *pString, uint8_t * buffer)
{
    UI_PrintStringBuffer(pString, buffer, FONT_GetSmallFont(UI_FONT_SETTING, true));
}

#ifdef ENABLE_FEAT_F4HWN_SET_FNT
void UI_DisplayFrequencyWithFont(const char *string, uint8_t X, uint8_t Y,
                                 bool center, uint8_t font)
#else
void UI_DisplayFrequency(const char *string, uint8_t X, uint8_t Y, bool center)
#endif
{
#ifndef ENABLE_FEAT_F4HWN_SET_FNT
    const uint8_t font = FONT_BIG_DIGITS_DEFAULT;
#endif
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
                FONT_DrawBigDigit(font, c - '0', pFb0, pFb1);
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

#ifdef ENABLE_FEAT_F4HWN_SET_FNT
// Big digits in the SetFnt font, for every screen. Kept as a function because
// overlay apps call it through the app API and must follow the setting too.
void UI_DisplayFrequency(const char *string, uint8_t X, uint8_t Y, bool center)
{
    UI_DisplayFrequencyWithFont(string, X, Y, center, UI_FONT_SETTING);
}
#endif

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
        uint8_t glyph[3];

        c -= 0x20;
        FW_ASSETS_Read(gFont3x5[c], glyph, sizeof(glyph));
        for (int i = 0; i < 3; ++i) {
          pixels = glyph[i];
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
            char text[UI_TEXT_SIZE];

            UI_LoadText(text, sizeof(text), UI_TEXT_UNLOCK_KEYBOARD);
            UI_PrintStringSmallBold(text, 12, 0, shift);

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
