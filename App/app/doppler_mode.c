/* Copyright 2026
 * https://github.com/armel
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

#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "app/doppler.h"
#include "app/doppler_mode.h"
#include "audio.h"
#include "dcs.h"
#include "driver/bk4819.h"
#include "driver/rtc.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "ui/helper.h"
#include "ui/ui.h"

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

static DOPPLER_State_t gDopplerState = DOPPLER_STATE_OFF;

static bool     gDopplerInit = false;
static bool     gDopplerPassed = false;
static bool     gDopplerEntryValid = false;
static bool     gDopplerTxOverride = false;

static uint32_t gDopplerSavedRxFreq = 0;
static uint32_t gDopplerSavedTxFreq = 0;
static uint8_t  gDopplerSavedTxCodeType = 0;
static uint8_t  gDopplerSavedTxCode = 0;

static uint8_t  gDopplerTime[6];          // year(2000-based)..second
static char     gDopplerInputStr[13];     // 12-digit entry (YYMMDDHHMMSS + '\0')
static uint8_t  gDopplerInputIndex = 0;

static DOPPLER_Entry_t gDopplerEntry;

bool DOPPLER_IsActive(void)
{
    return gDopplerState != DOPPLER_STATE_OFF;
}

static void DOPPLER_SetInputIndex(uint8_t Index)
{
    gDopplerInputIndex = Index;
    gDopplerInputStr[Index] = 0;
}

static bool DOPPLER_IsLeapYear(uint8_t Year2000)
{
    const uint16_t year = (uint16_t)2000 + Year2000;
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

// Validates the 12 entered digits (YYMMDDHHMMSS) before they reach the RTC.
static bool DOPPLER_InputValid(void)
{
    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    const uint8_t year  = (uint8_t)((gDopplerInputStr[0] - '0') * 10 + (gDopplerInputStr[1] - '0'));
    const uint8_t month = (uint8_t)((gDopplerInputStr[2] - '0') * 10 + (gDopplerInputStr[3] - '0'));
    const uint8_t day   = (uint8_t)((gDopplerInputStr[4] - '0') * 10 + (gDopplerInputStr[5] - '0'));
    const uint8_t hour  = (uint8_t)((gDopplerInputStr[6] - '0') * 10 + (gDopplerInputStr[7] - '0'));
    const uint8_t min   = (uint8_t)((gDopplerInputStr[8] - '0') * 10 + (gDopplerInputStr[9] - '0'));
    const uint8_t sec   = (uint8_t)((gDopplerInputStr[10] - '0') * 10 + (gDopplerInputStr[11] - '0'));

    if (month < 1 || month > 12 || hour > 23 || min > 59 || sec > 59)
    {
        return false;
    }

    uint8_t maxDay = days_in_month[month - 1];
    if (month == 2 && DOPPLER_IsLeapYear(year))
    {
        maxDay = 29;
    }
    return day >= 1 && day <= maxDay;
}

static void DOPPLER_EnterTracking(void)
{
    if (!DOPPLER_InputValid())
    {
        // Reject nonsense dates/times instead of writing garbage to the RTC
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        DOPPLER_SetInputIndex(0);
        gDopplerState = DOPPLER_STATE_INPUT_DATE;
        gUpdateDisplay = true;
        return;
    }

    const uint8_t date[6] = {
        (uint8_t)((gDopplerInputStr[0] - '0') * 10 + (gDopplerInputStr[1] - '0')),
        (uint8_t)((gDopplerInputStr[2] - '0') * 10 + (gDopplerInputStr[3] - '0')),
        (uint8_t)((gDopplerInputStr[4] - '0') * 10 + (gDopplerInputStr[5] - '0')),
    };
    const uint8_t time[6] = {
        (uint8_t)((gDopplerInputStr[6] - '0') * 10 + (gDopplerInputStr[7] - '0')),
        (uint8_t)((gDopplerInputStr[8] - '0') * 10 + (gDopplerInputStr[9] - '0')),
        (uint8_t)((gDopplerInputStr[10] - '0') * 10 + (gDopplerInputStr[11] - '0')),
    };

    gDopplerTime[0] = date[0];
    gDopplerTime[1] = date[1];
    gDopplerTime[2] = date[2];
    gDopplerTime[3] = time[0];
    gDopplerTime[4] = time[1];
    gDopplerTime[5] = time[2];

    RTC_SetUnix32(DOPPLER_UnixTime(gDopplerTime));

    gDopplerPassed = false;
    gDopplerEntryValid = false;
    gDopplerState = DOPPLER_STATE_TRACKING;
    gUpdateDisplay = true;
}

void DOPPLER_EnterMode(void)
{
    if (DOPPLER_IsActive())
    {
        return;
    }

    if (!gDopplerInit)
    {
        DOPPLER_Init();
        RTC_Init();
        gDopplerInit = true;
    }

    gDopplerSavedRxFreq = gTxVfo->freq_config_RX.Frequency;
    gDopplerTxOverride = false;

    RTC_EnableSecondIT(true);

    DOPPLER_SetInputIndex(0);
    gDopplerState = DOPPLER_STATE_INPUT_DATE;
    gRequestDisplayScreen = DISPLAY_DOPPLER;
    gUpdateDisplay = true;
}

void DOPPLER_ExitMode(void)
{
    if (gDopplerState == DOPPLER_STATE_OFF)
    {
        return;
    }

    // Abort any ongoing Doppler transmission first
    if (gCurrentFunction == FUNCTION_TRANSMIT)
    {
        APP_HandleEndTransmission();
    }
    if (gDopplerTxOverride)
    {
        gCurrentVfo->pTX->Frequency = gDopplerSavedTxFreq;
        gCurrentVfo->pTX->CodeType = (DCS_CodeType_t)gDopplerSavedTxCodeType;
        gCurrentVfo->pTX->Code = gDopplerSavedTxCode;
        gDopplerTxOverride = false;
    }

    // Restore the receiving frequency (VFO struct + hardware, never persisted)
    gTxVfo->freq_config_RX.Frequency = gDopplerSavedRxFreq;
    BK4819_SetFrequency(gDopplerSavedRxFreq);
    BK4819_PickRXFilterPathBasedOnFrequency(gDopplerSavedRxFreq);

    RTC_EnableSecondIT(false);

    gDopplerState = DOPPLER_STATE_OFF;
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;
}

void DOPPLER_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed)
    {
        return;
    }

    switch (gDopplerState)
    {
        case DOPPLER_STATE_INPUT_DATE:
        case DOPPLER_STATE_INPUT_TIME:
            if (Key >= KEY_0 && Key <= KEY_9)
            {
                if (gDopplerInputIndex < 12)
                {
                    gDopplerInputStr[gDopplerInputIndex] = (char)('0' + (Key - KEY_0));
                    gDopplerInputIndex++;
                    gDopplerInputStr[gDopplerInputIndex] = 0;
                    gUpdateDisplay = true;

                    if (gDopplerInputIndex == 6)
                    {
                        // date complete, keep the digits, move on to the time
                        gDopplerState = DOPPLER_STATE_INPUT_TIME;
                    }
                    else if (gDopplerInputIndex == 12)
                    {
                        DOPPLER_EnterTracking();
                    }
                }
            }
            else if (Key == KEY_EXIT)
            {
                DOPPLER_ExitMode();
            }
            break;

        case DOPPLER_STATE_TRACKING:
            if (Key == KEY_EXIT)
            {
                DOPPLER_ExitMode();
            }
            else if (Key == KEY_PTT && !gDopplerPassed && gDopplerEntryValid)
            {
                if (gCurrentFunction == FUNCTION_TRANSMIT)
                {
                    break; // already transmitting
                }

                const DOPPLER_Satellite_t *pSat = DOPPLER_GetSatellite();

                gDopplerSavedTxFreq = gCurrentVfo->pTX->Frequency;
                gDopplerSavedTxCodeType = (uint8_t)gCurrentVfo->pTX->CodeType;
                gDopplerSavedTxCode = gCurrentVfo->pTX->Code;
                gDopplerTxOverride = true;

                gCurrentVfo->pTX->Frequency = gDopplerEntry.uplink;
                if (pSat->send_ctcss > 0)
                {
                    gCurrentVfo->pTX->CodeType = CODE_TYPE_CONTINUOUS_TONE;
                    gCurrentVfo->pTX->Code = DCS_GetCtcssCode(pSat->send_ctcss);
                }
                else
                {
                    gCurrentVfo->pTX->CodeType = CODE_TYPE_OFF;
                    gCurrentVfo->pTX->Code = 0;
                }

                gFlagPrepareTX = true;
            }
            break;

        default:
            break;
    }
}

void DOPPLER_TimeSlice(void)
{
    if (gDopplerState != DOPPLER_STATE_TRACKING)
    {
        return;
    }

    // Restore the TX frequency once a Doppler transmission has ended
    if (gDopplerTxOverride && gCurrentFunction != FUNCTION_TRANSMIT)
    {
        gCurrentVfo->pTX->Frequency = gDopplerSavedTxFreq;
        gCurrentVfo->pTX->CodeType = (DCS_CodeType_t)gDopplerSavedTxCodeType;
        gCurrentVfo->pTX->Code = gDopplerSavedTxCode;
        gDopplerTxOverride = false;
    }

    if (!gRtcSecondTick)
    {
        return;
    }
    gRtcSecondTick = false;

    const uint32_t now = RTC_GetUnix32();
    const DOPPLER_Satellite_t *pSat = DOPPLER_GetSatellite();

    if (now < pSat->start_unix)
    {
        return; // pass not started yet
    }

    DOPPLER_Entry_t Entry;
    if (DOPPLER_GetEntry(now, &Entry))
    {
        if (!gDopplerEntryValid || gDopplerEntry.downlink != Entry.downlink)
        {
            gDopplerEntry = Entry;
            gDopplerEntryValid = true;
            // Keep the VFO struct in sync so logs/UI show the tracked frequency
            gTxVfo->freq_config_RX.Frequency = Entry.downlink;
            BK4819_SetFrequency(Entry.downlink);
            BK4819_PickRXFilterPathBasedOnFrequency(Entry.downlink);
            gUpdateDisplay = true;
        }
        gDopplerPassed = false;
    }
    else
    {
        gDopplerPassed = true;
        gUpdateDisplay = true;
    }
}

static void DOPPLER_RenderFrequency(const uint32_t Freq10Hz, const uint8_t Line)
{
    char Buffer[12];
    snprintf(Buffer, sizeof(Buffer), "%lu.%03lu", (unsigned long)(Freq10Hz / 100000u),
             (unsigned long)((Freq10Hz % 100000u) / 100u));
    // UI_DisplayFrequency 的 Y 是帧缓冲行号(0-6), 大字占 Y 和 Y+1 两行
    UI_DisplayFrequency(Buffer, 0, Line, true);
}

void DOPPLER_Render(void)
{
    UI_DisplayClear();

    if (gDopplerState == DOPPLER_STATE_OFF)
    {
        return;
    }

    if (gDopplerState == DOPPLER_STATE_INPUT_DATE || gDopplerState == DOPPLER_STATE_INPUT_TIME)
    {
        // 大字占两行: 标题 line 0-1, 提示 line 2, 输入 line 3-4, 退出提示 line 6
        UI_PrintString(gDopplerState == DOPPLER_STATE_INPUT_DATE ? "DATE" : "TIME", 0, 127, 0, 8);
        UI_PrintStringSmallNormal(gDopplerState == DOPPLER_STATE_INPUT_DATE ? "YYMMDD" : "HHMMSS", 0, 127, 2);
        UI_PrintString(gDopplerInputStr, 0, 127, 3, 8);
        UI_PrintStringSmallNormal("EXIT QUIT", 0, 127, 6);
        ST7565_BlitFullScreen();   // 推送 LCD（渲染函数需自行 Blit）
        return;
    }

    // TRACKING
    const DOPPLER_Satellite_t *pSat = DOPPLER_GetSatellite();
    char Buffer[24];

    if (!DOPPLER_HasData())
    {
        UI_PrintString("NO DATA", 0, 127, 1, 8);
        UI_PrintStringSmallNormal("WRITE DATA FIRST", 0, 127, 3);
        ST7565_BlitFullScreen();
        return;
    }

    snprintf(Buffer, sizeof(Buffer), "%s", pSat->name);
    UI_PrintStringSmallBold(Buffer, 0, 127, 0);

    if (gDopplerPassed)
    {
        UI_PrintString("PASSED", 0, 127, 1, 8);
    }
    else if (gDopplerEntryValid)
    {
        DOPPLER_RenderFrequency(gDopplerEntry.downlink, 1);
        DOPPLER_RenderFrequency(gDopplerEntry.uplink, 3);

        if (pSat->send_ctcss > 0)
        {
            snprintf(Buffer, sizeof(Buffer), "CTCSS %u.%u", (unsigned)(pSat->send_ctcss / 10u),
                     (unsigned)(pSat->send_ctcss % 10u));
        }
        else
        {
            snprintf(Buffer, sizeof(Buffer), "NO TONE");
        }
        UI_PrintStringSmallNormal(Buffer, 0, 127, 5);

        // time to pass end
        const int32_t remain = (int32_t)(pSat->start_unix + pSat->sum_time) - (int32_t)RTC_GetUnix32();
        if (remain > 0)
        {
            snprintf(Buffer, sizeof(Buffer), "-%ld s", (long)remain);
        }
        else
        {
            snprintf(Buffer, sizeof(Buffer), "+%ld s", (long)(-remain));
        }
        UI_PrintStringSmallNormal(Buffer, 0, 127, 6);
    }
    else
    {
        UI_PrintString("WAIT", 0, 127, 1, 8);
    }

    ST7565_BlitFullScreen();   // 推送 LCD
}

#endif // ENABLE_FEAT_F4HWN_DOPPLER
