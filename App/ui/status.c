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
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/scanner.h"
#include "bitmaps.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/helper.h"
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
    const bool isRxTxLogScreen = gScreenToDisplay == DISPLAY_RXTX_LOG;
    if (isRxTxLogScreen) {
        const char *filter = RXTX_LOG_GetFilterName();
        const uint8_t end = (filter[2] == 0) ? 10 : 14;

        GUI_DisplaySmallestInverse(filter, 2, 0, true, true, end);
        x = 18;
        x1 = x;
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
        x += 39;
    } else {
#endif

#ifdef ENABLE_NOAA
    // NOAA indicator
    if (!(gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) && gIsNoaaMode) { // NOASS SCAN indicator
        memcpy(line + x, BITMAP_NOAA, sizeof(BITMAP_NOAA));
    }
    // Power Save indicator
    else if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        memcpy(line + x, gFontPowerSave, sizeof(gFontPowerSave));
    }
    x += 8;
#else
    // Power Save indicator
    if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        memcpy(line + x, gFontPowerSave, sizeof(gFontPowerSave));
    }
    x += 8;
#endif

    x1 = x;

#ifdef ENABLE_DTMF_CALLING
    if (gSetting_KILLED) {
        memset(line + x, 0xFF, 10);
        x1 = x + 10;
    }
    else
#endif
    { // SCAN indicator
        if (gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) {
            if (IS_MR_CHANNEL(gNextMrChannel) && !SCANNER_IsScanning()) { // channel mode
                uint8_t end = 0;

                if(gEeprom.SCAN_LIST_DEFAULT == MR_CHANNELS_LIST + 1)
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
                memcpy(line + x + 1, gFontS, sizeof(gFontS));
                //UI_PrintStringSmallBufferNormal("S", line + x + 1);
            }
            x1 = x + 10;
        }
    }
    x += 10;  // font character width

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

        if(!SCANNER_IsScanning()) {
            if(!gAirCopyBootMode) {
                const void *src = NULL;    // Pointer to the font/bitmap to copy
                size_t sSize = 0;          // Size of the font/bitmap
                uint8_t sOff = 2;          // Offset relative to the reference position

                #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
                    if (gEeprom.MENU_LOCK) {
                        src = gFontRO;
                        sSize = sizeof(gFontRO);
                    } else 
                #endif
                {
                    uint8_t xb = (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF);

                    if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
                        if (gDualWatchActive) { // DWR - dual watch + respond
                            src = gFontDWR;
                            sOff = xb ? 2 : 0;
                            sSize = sizeof(gFontDWR) - (xb ? 5 : 0);
                        } else {
                            src = gFontHold;
                            sOff = 3;
                            sSize = sizeof(gFontHold);
                        }
                    } else {
                        src   = xb ? gFontXB         : gFontMO;          // XB - crossband
                        sSize = xb ? sizeof(gFontXB) : sizeof(gFontMO);  // MO - main only
                    }
                }

                // Perform the memcpy if a source was selected
                if (src) {
                    memcpy(line + x + sOff, src, sSize);
                }
            }
        }
        x += sizeof(gFontDWR) + 3;
    #endif

#if defined(ENABLE_FEAT_F4HWN_RX_TX_TIMER) && !defined(ENABLE_FEAT_F4HWN_DEBUG)
    }
#endif

#ifdef ENABLE_VOX
    // VOX indicator
    if (gEeprom.VOX_SWITCH) {
        memcpy(line + x, gFontVox, sizeof(gFontVox));
        x1 = x + sizeof(gFontVox) + 1;
    }
    x += sizeof(gFontVox) + 3;
#endif

#ifdef ENABLE_FEAT_F4HWN
    // PTT indicator
    if(!gAirCopyBootMode) {
        if (gSetting_set_ptt_session) {
            memcpy(line + x, gFontPttOnePush, sizeof(gFontPttOnePush));
            x1 = x + sizeof(gFontPttOnePush) + 1;
        }
        else
        {
            memcpy(line + x, gFontPttClassic, sizeof(gFontPttClassic));
            x1 = x + sizeof(gFontPttClassic) + 1;       
        }
    }
    x += sizeof(gFontPttClassic) + 3;
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
        memcpy(line + x + 1, src, size);
    }

    // Battery
    unsigned int x2 = LCD_WIDTH - sizeof(BITMAP_BatteryLevel1) - 0;

    UI_DrawBattery(line + x2, gBatteryDisplayLevel, gLowBatteryBlink);

    switch (gSetting_battery_text) {
        default:
        case 0:
            break;

        case 1:     // voltage
        case 2:     // percentage
            if (gSetting_battery_text == 1) {
                const uint16_t voltage = MIN(gBatteryVoltageAverage, 999); // limit to 9.99V
                sprintf(str, "%u.%02u", voltage / 100, voltage % 100);
            } else {
                //gBatteryVoltageAverage = 999;
                sprintf(str, "%02u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
            }

            x2 -= (7 * strlen(str));
            UI_PrintStringSmallBufferNormal(str, line + x2);
            /*
            uint8_t shift = (strlen(str) < 5) ? 92 : 88;
            GUI_DisplaySmallest(str, shift, 1, true, true);

            for (uint8_t i = shift - 2; i < 110; i++) {
                gStatusLine[i] ^= 0x7F; // invert
            }
            */
            
            break;
    }

    // **************

    ST7565_BlitStatusLine();
}
