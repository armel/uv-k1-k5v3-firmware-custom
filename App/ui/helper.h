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

#ifndef UI_UI_H
#define UI_UI_H

#include <stdbool.h>
#include <stdint.h>

#define UI_TEXT_SIZE 17u

typedef enum {
    UI_TEXT_VFO_NORMAL = 0,
    UI_TEXT_VFO_BUSY,
    UI_TEXT_VFO_BAT_LOW,
    UI_TEXT_VFO_TX_DISABLE,
    UI_TEXT_VFO_TIMEOUT,
    UI_TEXT_VFO_VOLT_HIGH,
    UI_TEXT_LOW_BATTERY,
    UI_TEXT_SCAN_LIST,
    UI_TEXT_TRIPLE_WATCH,
    UI_TEXT_QUAD_WATCH,
    UI_TEXT_UNLOCK_KEYBOARD,
    UI_TEXT_SCANNER_SAVE_PROMPT,
    UI_TEXT_SCANNER_SAVE_PREFIX,
    UI_TEXT_SCANNER_SEARCH_FREQ,
    UI_TEXT_SCANNER_SEARCH_TONE,
    UI_TEXT_SCANNER_COMPLETE,
    UI_TEXT_SCANNER_FAILED,
    UI_TEXT_SCANNER_FREQ_UNKNOWN,
    UI_TEXT_SCANNER_TONE_UNKNOWN,
    UI_TEXT_SCANNER_TONE_NONE,
    UI_TEXT_APP_TITLE,
    UI_TEXT_APP_ERROR,
    UI_TEXT_PRESS_ANY_KEY,
    UI_TEXT_NO_APPS,
    UI_TEXT_BAD_SLOT,
    UI_TEXT_NO_APP,
    UI_TEXT_UPDATE_APP,
    UI_TEXT_UPDATE_FIRMWARE,
    UI_TEXT_REINSTALL_APP,
    UI_TEXT_AUTH,
    UI_TEXT_NOT_SUPPORTED,
    UI_TEXT_ERROR,
    UI_TEXT_COUNT
} UI_Text_t;

void UI_LoadText(char *buffer, uint32_t size, UI_Text_t text);

void UI_GenerateChannelStringEx(char *pString, const bool bShowPrefix, const uint16_t ChannelNumber);
void UI_PrintString(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, uint8_t Width);
#ifdef ENABLE_FEAT_F4HWN_SET_FNT
    void UI_PrintStringWithFont(const char *pString, uint8_t Start, uint8_t End,
                                uint8_t Line, uint8_t Width, uint8_t font);
#endif
void UI_PrintStringSmallNormal(const char *pString, uint8_t Start, uint8_t End, uint8_t Line);
void UI_PrintStringSmallNormalInverse(const char *pString, uint8_t Start, uint8_t End, uint8_t Line);
void UI_PrintStringSmallBold(const char *pString, uint8_t Start, uint8_t End, uint8_t Line);
void UI_PrintStringSmallBufferNormal(const char *pString, uint8_t *buffer);
void UI_PrintStringSmallBufferBold(const char *pString, uint8_t * buffer);
void UI_PrintStringSmall(const char *pString, uint8_t Start, uint8_t End, uint8_t Line, const uint8_t *font);
void UI_DisplayFrequency(const char *string, uint8_t X, uint8_t Y, bool center);
#ifdef ENABLE_FEAT_F4HWN_SET_FNT
    void UI_DisplayFrequencyWithFont(const char *string, uint8_t X, uint8_t Y,
                                     bool center, uint8_t font);
#endif

// SetFnt value applied to the UI, or the default family when it is disabled.
#ifdef ENABLE_FEAT_F4HWN_SET_FNT
    #define UI_FONT_SETTING gSetting_set_fnt
#else
    #define UI_FONT_SETTING 0u
#endif

void UI_DisplayPopup(const char *string);

void UI_DrawPixelBuffer(uint8_t (*buffer)[128], uint8_t x, uint8_t y, bool black);
#ifdef ENABLE_FEAT_F4HWN
    //void UI_DrawLineDottedBuffer(uint8_t (*buffer)[128], int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool black);
    void PutPixel(uint8_t x, uint8_t y, bool fill);
    void PutPixelStatus(uint8_t x, uint8_t y, bool fill);
    void GUI_DisplaySmallest(const char *pString, uint8_t x, uint8_t y, bool statusbar, bool fill);
    void GUI_DisplaySmallestInverse(const char *pString, uint8_t x, uint8_t Line, bool statusbar, bool fill, uint8_t endX);
    void UI_DisplayUnlockKeyboard(uint8_t shift);
    bool IsEmptyName(const char *name, uint8_t len);
#endif
void UI_DrawLineBuffer(uint8_t (*buffer)[128], int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool black);
void UI_DrawRectangleBuffer(uint8_t (*buffer)[128], int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool black);

void UI_DisplayClear(void);
void UI_StatusClear(void);

#if defined(ENABLE_FEAT_F4HWN_MULTIBOOT) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
void UI_DrawMenuKeyHints(const char *act_menu, const char *act_exit);
#endif

#endif
