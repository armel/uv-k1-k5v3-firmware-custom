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

#include "app/app.h"
#include "app/chFrScanner.h"
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
#include "app/rxtx_log.h"
#endif
#ifdef ENABLE_FMRADIO_EMBEDDED
    #include "app/fm.h"
#endif
#include "app/scanner.h"
#include "bitmaps.h"
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT_HOT_CFG
#include "driver/mb_flash.h"
#endif
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "fw_assets.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/helper.h"
#ifdef ENABLE_FEAT_F4HWN_MENU_CAT
#include "ui/menu.h"
#endif
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT_HOT_CFG
#include "ui/multiboot.h"
#endif
#include "ui/ui.h"
#include "ui/status.h"

#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
#ifndef ENABLE_FEAT_F4HWN_DEBUG
static void convertTime(uint8_t *line, uint8_t type) 
{
    uint16_t t = (type == 0) ? (gTxTimerCountdown_500ms / 2) : (3600 - gRxTimerCountdown_500ms / 2);

    uint8_t m = t / 60;
    uint8_t s = t - (m * 60); // Replace modulo with subtraction for efficiency

    char str[6];
    sprintf(str, "%02u:%02u", m, s);
    UI_PrintStringSmallBufferNormal(str, line);
}
#endif
#endif

#if defined(ENABLE_FEAT_F4HWN_FOXHUNT) || defined(ENABLE_FEAT_F4HWN_BEACON) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
void UI_DrawStatusBattery(uint8_t *line, char *str)
#else
static __attribute__((always_inline)) inline void UI_DrawStatusBattery(uint8_t *line, char *str)
#endif
{
    unsigned int x = LCD_WIDTH - sizeof(BITMAP_BatteryLevel1);

    UI_DrawBattery(line + x, gBatteryDisplayLevel, gLowBatteryBlink);

    if (gSetting_battery_text == 1) {
        const uint16_t voltage = MIN(gBatteryVoltageAverage, 999);
        sprintf(str, "%u.%02u", voltage / 100, voltage % 100);
    } else if (gSetting_battery_text == 2) {
        sprintf(str, "%02u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
    } else {
        return;
    }

    x -= 7 * strlen(str);
    UI_PrintStringSmallBufferNormal(str, line + x);
}

void UI_DisplayStatus()
{
    gUpdateStatus = false;

    if (APP_IsScreenSaverDisplayed())
        return;

    UI_StatusClear();

    uint8_t     *line = gStatusLine;
    unsigned int x    = 0;
    unsigned int x1   = 0;

    char str[8] = "";

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    // The filter label reuses the leftmost slot (pixels 2..16) normally
    // reserved by the config-bank and scan indicators; those two are skipped
    // on the log screen so the rest of the bar keeps its usual layout.
    const bool isRxTxLogScreen = gScreenToDisplay == DISPLAY_RXTX_LOG;
    if (isRxTxLogScreen) {
        const char *filter = RXTX_LOG_GetFilterName();
        const uint8_t end = (filter[2] == 0) ? 10 : 14;

        GUI_DisplaySmallestInverse(filter, 2, 0, true, true, end);
    }
#endif

#if defined(ENABLE_FEAT_F4HWN_RX_TX_TIMER) && !defined(ENABLE_FEAT_F4HWN_DEBUG)
    bool isTransmit = gCurrentFunction == FUNCTION_TRANSMIT;
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    if (!isRxTxLogScreen && gSetting_set_tmr && (isTransmit || FUNCTION_IsRx())) {
#else
    if (gSetting_set_tmr && (isTransmit || FUNCTION_IsRx())) {
#endif
        convertTime(line, !isTransmit);
        x += 35u;
    } else {
#endif

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    if (!isRxTxLogScreen)
#endif
    {
#ifdef ENABLE_NOAA
        // NOAA indicator
        if (!(gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) && gIsNoaaMode) { // NOASS SCAN indicator
            FW_ASSETS_COPY(line + x, BITMAP_NOAA);
        }
        else
#endif
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT_HOT_CFG
        if (!(gScanStateDir != SCAN_OFF || SCANNER_IsScanning())) {
            const uint8_t bank = MB_GetActiveBank();

            if (bank < MB_BANK_COUNT) {
                FW_ASSETS_COPY(line + x, gFontConfigBank[0]);
                if (bank == 0u) {
                    FW_ASSETS_COPY(line + x + sizeof(gFontConfigBank[0]),
                                   gFontConfigBank[1]);
                } else {
                    str[0] = (char)('0' + bank);
                    str[1] = '\0';
                    UI_PrintStringSmallBufferNormal(str, line + x + 6u);
                }
            }
        }
#else
        // Power Save indicator
        if (gCurrentFunction == FUNCTION_POWER_SAVE) {
            FW_ASSETS_COPY(line + x, gFontPowerSave);
        }
#endif
    }
    x += 8u;

    x1 = x;

#ifdef ENABLE_DTMF_CALLING
    if (gSetting_KILLED) {
        memset(line + x, 0xFF, 10);
        x1 = x + 10;
    }
    else
#endif
    { // SCAN indicator
        if ((gScanStateDir != SCAN_OFF || SCANNER_IsScanning())
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
            && !isRxTxLogScreen
#endif
        ) {
            if (IS_MR_CHANNEL(gNextMrChannel) && !SCANNER_IsScanning()) { // channel mode
                uint8_t end = 0;

                if (gEeprom.SCAN_LIST_DEFAULT == SCAN_LIST_MODE_MIX)
                {
                    strcpy(str, "MIX");
                    end = 14;
                }
                else if(gEeprom.SCAN_LIST_DEFAULT == SCAN_LIST_MODE_ALL)
                {
                    strcpy(str, "ALL");
                    end = 14;
                }
                else
                {
                    const char *name = gListName[gEeprom.SCAN_LIST_DEFAULT - 1];

                    // Check if name is valid
                    if (!IsEmptyName(name, sizeof(gListName[0]))) {
                        sprintf(str, "%.3s", name);
                        end = 14;
                    } else {
                        sprintf(str, "%02d", gEeprom.SCAN_LIST_DEFAULT);
                        end = 10;
                    }
                }

                if (gEeprom.SCAN_LIST_ENABLED) {
                    strcat(str, "+");
                    end += 4;
                }

#ifdef ENABLE_FEAT_F4HWN
                GUI_DisplaySmallestInverse(str, 2, 0, true, true, end);
#else
                GUI_DisplaySmallest(str, 2, 1, true, true);

                gStatusLine[0] ^= 0x3E;
                for (uint8_t x = 1; x < end; x++)
                {
                    gStatusLine[x] ^= 0x7F;
                }
                gStatusLine[end] ^= 0x3E;
#endif
            }
            else {  // frequency mode
                FW_ASSETS_COPY(line + x + 1, gFontS);
                //UI_PrintStringSmallBufferNormal("S", line + x + 1);
            }
            x1 = x + 10;
        }
    }
    x += 10u;

    #ifdef ENABLE_FEAT_F4HWN_DEBUG
        // Only for debug
        // Only for debug
        // Only for debug

        sprintf(str, "%d", gDebug);
        UI_PrintStringSmallBufferNormal(str, line + x + 1);
        x += 16;
    #else
        #ifdef ENABLE_VOICE
        // VOICE indicator
        if (gEeprom.VOICE_PROMPT != VOICE_PROMPT_OFF){
            memcpy(line + x, BITMAP_VoicePrompt, sizeof(BITMAP_VoicePrompt));
            x1 = x + sizeof(BITMAP_VoicePrompt);
        }
        x += sizeof(BITMAP_VoicePrompt);
        #endif

        if (gScanStateDir == SCAN_OFF && !SCANNER_IsScanning()) {
            if(!gAirCopyBootMode) {
                uint8_t *const modeLine = line + x - 1u;
                const void *src = NULL;
                size_t sSize = 0;
                uint8_t sOff = 0;

                #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
                    if (gEeprom.MENU_LOCK) {
                        src = gFontRO;
                        sSize = sizeof(gFontRO);
                    } else 
                #endif
                {
                    const uint8_t xb = gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF;

                    if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
                        if (gDualWatchActive) {
                            const uint8_t (*font)[6] = gFontDWR;
#ifdef ENABLE_FEAT_F4HWN_FULL_WATCH
                            if (gEeprom.DUAL_WATCH == DUAL_WATCH_FULL)
                                font = gFontFWR;
#endif
                            FW_ASSETS_COPY(modeLine + sOff, font[0]);
                            FW_ASSETS_COPY(modeLine + sOff + sizeof(font[0]),
                                           font[xb ? 1u : 2u]);
                        } else {
                            src = gFontHold;
                            sOff = 1u;
                            sSize = sizeof(gFontHold);
                        }
                    } else {
                        src = xb ? gFontXB : gFontMO;
                        sSize = xb ? sizeof(gFontXB) : sizeof(gFontMO);
                    }
                }

                if (src != NULL)
                    FW_ASSETS_Read(src, modeLine + sOff, sSize);
            }
        }
        x += 17u;
    #endif

#if defined(ENABLE_FEAT_F4HWN_RX_TX_TIMER) && !defined(ENABLE_FEAT_F4HWN_DEBUG)
    }
#endif

#ifdef ENABLE_VOX
    // VOX indicator
    if (gEeprom.VOX_SWITCH) {
        FW_ASSETS_COPY(line + x, gFontVox);
        x1 = x + sizeof(gFontVox) + 1u;
    }
    x += sizeof(gFontVox) + 5u;
#endif

#ifdef ENABLE_FEAT_F4HWN
    // PTT indicator
    if(!gAirCopyBootMode) {
        const void *src = gSetting_set_ptt_session
                        ? (const void *)gFontPttOnePush
                        : (const void *)gFontPttClassic;

        FW_ASSETS_Read(src, line + x, sizeof(gFontPttClassic));
        x1 = x + sizeof(gFontPttClassic) + 1u;
    }
    x += sizeof(gFontPttClassic) + 5u;
#endif

    x = MAX(x1, 69u);

    const void *src = NULL;   // Pointer to the font/bitmap to copy
    size_t size = 0;          // Size of the font/bitmap

    // Determine the source and size based on conditions
    if (gEeprom.KEY_LOCK) {
        src = gFontKeyLock;
        size = sizeof(gFontKeyLock);
    }
    else if (gWasFKeyPressed) {
        src = gFontF;
        size = sizeof(gFontF);
    }
    #ifdef ENABLE_FEAT_F4HWN
        else if (gMute) {
            src = gFontMute;
            size = sizeof(gFontMute);
        }
    #endif
    else if (gBackLight) {
        // Distinguish the manual backlight sub-state: BACKLIGHT_TIME == 0
        // means the light is currently off (hollow bulb), otherwise it is
        // forced on (bulb with filament).

        if (gEeprom.BACKLIGHT_TIME == 0) {
            src = gFontLightOff;
            size = sizeof(gFontLightOff);
        }
        else
        {
            src = gFontLight;
            size = sizeof(gFontLight);     
        }
    }
    #ifdef ENABLE_FEAT_F4HWN_CHARGING_C
    else if (gChargingWithTypeC) {
        src = BITMAP_USB_C;
        size = sizeof(BITMAP_USB_C);
    }
    #endif

    // Perform the memcpy if a source was selected
    if (src) {
        FW_ASSETS_Read(src, line + x + 1, size);
    }

    UI_DrawStatusBattery(line, str);

#ifdef ENABLE_FEAT_F4HWN_MENU_CAT
    if (gScreenToDisplay == DISPLAY_MENU) {
        // "MENU" at category level, otherwise the uppercased category name.
        char text[sizeof(CategoryNames[0])];   // longest label ("Channels") + NUL
        uint8_t n = 0;

        if (gMenuLevel == MENU_LEVEL_CAT)
            strcpy(text, "MENU");
        else
            FW_ASSETS_STRCPY(text, CategoryNames, gMenuCategory);

        // & 0xDF uppercases letters (labels are letters only).
        for (; text[n] != '\0'; n++)
            text[n] = (char)(text[n] & 0xDFu);

        // Clear through the last pixel of the three-letter receiver-mode bitmap.
        memset(line, 0, 36u);
        GUI_DisplaySmallestInverse(text, 2, 0, true, true, (uint8_t)(2u + n * 4u));
    }
#endif

    // **************

    ST7565_BlitStatusLine();
}
