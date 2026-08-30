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
#include "driver/rtc_save.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "ui/helper.h"
#include "ui/ui.h"

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

static DOPPLER_State_t gDopplerState = DOPPLER_STATE_OFF;

static bool     gDopplerInit = false;
static bool     gDopplerTimeSet = false;   // RTC was set this power session
static bool     gDopplerPassed = false;
static bool     gDopplerEntryValid = false;
static bool     gDopplerTxOverride = false;
static bool     gDopplerShowExtra = false; // key 2 toggles supplementary orbit info
static bool     gDopplerShowSlots = false; // key 3 shows all slot names
static bool     gDopplerFSlotArm = false;  // F key pressed, waiting for a slot digit (1..4)
static uint8_t  gDopplerLedTicks = 0;      // green RX LED flash countdown (10 ms ticks)
static uint8_t  gDopplerWarn60Mask = 0;    // per-slot bit: AOS-60 s reminder already played
static uint8_t  gDopplerWarn10Mask = 0;    // per-slot bit: AOS-10 s reminder already played

static uint32_t gDopplerSavedRxFreq = 0;
static uint32_t gDopplerSavedTxFreq = 0;
static uint8_t  gDopplerSavedTxCodeType = 0;
static uint8_t  gDopplerSavedTxCode = 0;

// Saved squelch thresholds so we can force SQL 0 during a pass and restore
// the user's global setting before/after the pass.
static uint8_t  gDopplerSavedSqlOpenRssi = 0;
static uint8_t  gDopplerSavedSqlCloseRssi = 0;
static uint8_t  gDopplerSavedSqlOpenNoise = 0;
static uint8_t  gDopplerSavedSqlCloseNoise = 0;
static uint8_t  gDopplerSavedSqlCloseGlitch = 0;
static uint8_t  gDopplerSavedSqlOpenGlitch = 0;
static bool     gDopplerSqlForcedOpen = false;

static uint8_t  gDopplerTime[6];          // year(2000-based)..second
static char     gDopplerInputStr[13];     // 12-digit entry (YYMMDDHHMMSS + '\0')
static uint8_t  gDopplerInputIndex = 0;

static DOPPLER_Entry_t gDopplerEntry;

static uint16_t gDopplerMs = 0;   // 0..999, sub-second phase synced to RTC second tick

// Time adjustment state
static uint8_t  gDopplerAdjustTime[6]; // year, month, day, hour, minute, second (Beijing)
static uint8_t  gDopplerAdjustField = 0; // 0=year..5=second, 0..2 digits per field

bool DOPPLER_IsActive(void)
{
    return gDopplerState != DOPPLER_STATE_OFF;
}

// Capture the current VFO squelch thresholds from the active RX VFO.
static void DOPPLER_SaveSquelch(void)
{
    gDopplerSavedSqlOpenRssi     = gRxVfo->SquelchOpenRSSIThresh;
    gDopplerSavedSqlCloseRssi    = gRxVfo->SquelchCloseRSSIThresh;
    gDopplerSavedSqlOpenNoise    = gRxVfo->SquelchOpenNoiseThresh;
    gDopplerSavedSqlCloseNoise   = gRxVfo->SquelchCloseNoiseThresh;
    gDopplerSavedSqlCloseGlitch  = gRxVfo->SquelchCloseGlitchThresh;
    gDopplerSavedSqlOpenGlitch   = gRxVfo->SquelchOpenGlitchThresh;
}

// Restore the previously saved VFO squelch thresholds to chip and VFO struct.
static void DOPPLER_RestoreSquelch(void)
{
    gRxVfo->SquelchOpenRSSIThresh    = gDopplerSavedSqlOpenRssi;
    gRxVfo->SquelchCloseRSSIThresh   = gDopplerSavedSqlCloseRssi;
    gRxVfo->SquelchOpenNoiseThresh   = gDopplerSavedSqlOpenNoise;
    gRxVfo->SquelchCloseNoiseThresh  = gDopplerSavedSqlCloseNoise;
    gRxVfo->SquelchCloseGlitchThresh = gDopplerSavedSqlCloseGlitch;
    gRxVfo->SquelchOpenGlitchThresh  = gDopplerSavedSqlOpenGlitch;

    BK4819_SetupSquelch(
        gRxVfo->SquelchOpenRSSIThresh,    gRxVfo->SquelchCloseRSSIThresh,
        gRxVfo->SquelchOpenNoiseThresh,   gRxVfo->SquelchCloseNoiseThresh,
        gRxVfo->SquelchCloseGlitchThresh, gRxVfo->SquelchOpenGlitchThresh);
}

// Force squelch fully open (SQL 0) for the duration of a pass.
static void DOPPLER_ForceSquelchOpen(void)
{
    if (gDopplerSqlForcedOpen)
    {
        return;
    }
    DOPPLER_SaveSquelch();
    gDopplerSqlForcedOpen = true;

    // These values match RADIO_ConfigureSquelchAndOutputPower() for
    // gEeprom.SQUELCH_LEVEL == 0 (squelch off / fully open).
    gRxVfo->SquelchOpenRSSIThresh    = 0;
    gRxVfo->SquelchCloseRSSIThresh   = 0;
    gRxVfo->SquelchOpenNoiseThresh   = 127;
    gRxVfo->SquelchCloseNoiseThresh  = 127;
    gRxVfo->SquelchCloseGlitchThresh = 255;
    gRxVfo->SquelchOpenGlitchThresh  = 255;

    BK4819_SetupSquelch(0, 0, 127, 127, 255, 255);
}

static void DOPPLER_SetInputIndex(uint8_t Index)
{
    gDopplerInputIndex = Index;
    gDopplerInputStr[Index] = 0;
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
    gDopplerTimeSet = true;
    RTC_SaveTimeToFlash();

    gDopplerPassed = false;
    gDopplerEntryValid = false;
    gDopplerMs = 0;
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

    // Passes that elapsed while the radio was off get cleaned up first.
    const uint32_t enterNow = RTC_GetUnix32();
    DOPPLER_EraseExpired(enterNow);

    // If the default slot (0) is empty, prefer a pass that is underway right
    // now, then the one starting next, then any valid slot.
    if (!DOPPLER_HasData())
    {
        int pick = DOPPLER_FindPassing(enterNow);
        if (pick < 0)
        {
            pick = DOPPLER_FindNext(enterNow);
        }
        if (pick < 0)
        {
            for (uint8_t s = 0; s < DOPPLER_SLOT_COUNT; s++)
            {
                if (DOPPLER_SlotHasData(s))
                {
                    pick = (int)s;
                    break;
                }
            }
        }
        if (pick >= 0)
        {
            DOPPLER_SelectSlot((uint8_t)pick);
        }
    }

    gDopplerSavedRxFreq = gTxVfo->freq_config_RX.Frequency;
    gDopplerTxOverride = false;
    gDopplerSqlForcedOpen = false;

    RTC_EnableSecondIT(true);

    gDopplerPassed = false;
    gDopplerEntryValid = false;

    // Squelch follows the global setting until a pass actually starts.
    DOPPLER_RestoreSquelch();

    // Never force the 12-digit entry: go straight to tracking. Time is set
    // either from Flash at boot or via the adjust screen (key 1).
    if (RTC_GetUnix32() >= 68000000u)
    {
        gDopplerTimeSet = true;
    }
    gDopplerState = DOPPLER_STATE_TRACKING;
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

    // Ensure the slot-switch LED flash does not stay stuck on: TimeSlice
    // (which would turn it off) stops running once we leave the mode.
    // The reminder blink finishes inside DOPPLER_NotifyPass() itself.
    if (gDopplerLedTicks > 0)
    {
        gDopplerLedTicks = 0;
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    }

    // Restore the user's global squelch setting before leaving Doppler mode.
    if (gDopplerSqlForcedOpen)
    {
        DOPPLER_RestoreSquelch();
        gDopplerSqlForcedOpen = false;
    }

    gDopplerState = DOPPLER_STATE_OFF;
    gDopplerShowExtra = false;
    gDopplerShowSlots = false;
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;
}

void DOPPLER_SetTimeFromUart(void)
{
    gDopplerTimeSet = true;
    gDopplerPassed = false;
    gDopplerEntryValid = false;
    RTC_SaveTimeToFlash();
}

static void DOPPLER_EnterAdjust(void)
{
    uint32_t unix = RTC_GetUnix32();
    if (unix < 68000000u)
    {
        // No usable time yet (fresh Flash, never set) - start from a sane
        // default so the user can adjust every field from here.
        const uint8_t t[6] = {26, 1, 1, 12, 0, 0}; // 2026-01-01 12:00:00
        unix = DOPPLER_UnixTime(t);
    }
    DOPPLER_UnixToDate(unix, gDopplerAdjustTime);
    gDopplerAdjustField = 0;
    gDopplerState = DOPPLER_STATE_ADJUST;
    gUpdateDisplay = true;
}

static void DOPPLER_SaveAdjust(void)
{
    RTC_SetUnix32(DOPPLER_UnixTime(gDopplerAdjustTime));
    gDopplerTimeSet = true;
    RTC_SaveTimeToFlash();
    gDopplerPassed = false;
    gDopplerEntryValid = false;
    gDopplerState = DOPPLER_STATE_TRACKING;
    gUpdateDisplay = true;
}

static void DOPPLER_AdjustStep(bool up)
{
    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int8_t dir = up ? 1 : -1;
    switch (gDopplerAdjustField)
    {
        case 0: // year
            {
                int16_t y = (int16_t)gDopplerAdjustTime[0] + dir;
                if (y < 0) y = 99;
                if (y > 99) y = 0;
                gDopplerAdjustTime[0] = (uint8_t)y;
                break;
            }
        case 1: // month
            {
                int16_t m = (int16_t)gDopplerAdjustTime[1] + dir;
                if (m < 1) m = 12;
                if (m > 12) m = 1;
                gDopplerAdjustTime[1] = (uint8_t)m;
                break;
            }
        case 2: // day
            {
                uint8_t maxDay = days_in_month[gDopplerAdjustTime[1] - 1];
                if (gDopplerAdjustTime[1] == 2 && DOPPLER_IsLeapYear(gDopplerAdjustTime[0]))
                    maxDay = 29;
                int16_t d = (int16_t)gDopplerAdjustTime[2] + dir;
                if (d < 1) d = maxDay;
                if (d > maxDay) d = 1;
                gDopplerAdjustTime[2] = (uint8_t)d;
                break;
            }
        case 3: // hour
            {
                int16_t h = (int16_t)gDopplerAdjustTime[3] + dir;
                if (h < 0) h = 23;
                if (h > 23) h = 0;
                gDopplerAdjustTime[3] = (uint8_t)h;
                break;
            }
        case 4: // minute
            {
                int16_t m = (int16_t)gDopplerAdjustTime[4] + dir;
                if (m < 0) m = 59;
                if (m > 59) m = 0;
                gDopplerAdjustTime[4] = (uint8_t)m;
                break;
            }
        case 5: // second
            {
                int16_t s = (int16_t)gDopplerAdjustTime[5] + dir;
                if (s < 0) s = 59;
                if (s > 59) s = 0;
                gDopplerAdjustTime[5] = (uint8_t)s;
                break;
            }
    }
    gUpdateDisplay = true;
}

void DOPPLER_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (!bKeyPressed)
    {
        return; // nothing to do on key release
    }

    // F + 1..4 switches the active satellite slot. The F press arms the
    // combination; the next digit key selects the slot. Runs before the
    // KEY_1 time-adjust check so F+1 always switches instead of adjusting.
    if (Key == KEY_F)
    {
        gDopplerFSlotArm = true;
        return;
    }
    if (gDopplerFSlotArm && Key >= KEY_1 && Key <= KEY_4)
    {
        gDopplerFSlotArm = false;
        gDopplerEntryValid = false;
        gDopplerPassed = false;
        if (DOPPLER_SelectSlot((uint8_t)(Key - KEY_1)))
        {
            gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        }
        else
        {
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        }
        // flash the green RX LED for ~150 ms as visual feedback
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
        gDopplerLedTicks = 15;
        gUpdateDisplay = true;
        return;
    }
    gDopplerFSlotArm = false;

    // Key 1 (short or held) opens the time-adjust screen from tracking -
    // this is the single place where the time is set manually.
    if (gDopplerState == DOPPLER_STATE_TRACKING && Key == KEY_1)
    {
        DOPPLER_EnterAdjust();
        return;
    }

    if (bKeyHeld)
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

        case DOPPLER_STATE_ADJUST:
            // EXIT and M both save the fine-tuned time and jump back to the
            // main tracking page.
            if (Key == KEY_EXIT || Key == KEY_MENU)
            {
                DOPPLER_SaveAdjust();
            }
            else if (Key == KEY_0)
            {
                // next field
                gDopplerAdjustField++;
                if (gDopplerAdjustField > 5)
                    gDopplerAdjustField = 0;
                gUpdateDisplay = true;
            }
            else if (Key == KEY_1)
            {
                DOPPLER_AdjustStep(true);
            }
            else if (Key == KEY_2)
            {
                DOPPLER_AdjustStep(false);
            }
            break;

        case DOPPLER_STATE_TRACKING:
            if (Key == KEY_EXIT)
            {
                if (gDopplerShowSlots)
                {
                    gDopplerShowSlots = false;
                    gUpdateDisplay = true;
                }
                else if (gDopplerShowExtra)
                {
                    gDopplerShowExtra = false;
                    gUpdateDisplay = true;
                }
                else
                {
                    DOPPLER_ExitMode();
                }
            }
            else if (Key == KEY_MENU)
            {
                // M returns to the main tracking page from any sub-page.
                if (gDopplerShowSlots)
                {
                    gDopplerShowSlots = false;
                    gUpdateDisplay = true;
                }
                else if (gDopplerShowExtra)
                {
                    gDopplerShowExtra = false;
                    gUpdateDisplay = true;
                }
            }
            else if (Key == KEY_2)
            {
                gDopplerShowExtra = !gDopplerShowExtra;
                gDopplerShowSlots = false;
                gUpdateDisplay = true;
            }
            else if (Key == KEY_3)
            {
                gDopplerShowSlots = !gDopplerShowSlots;
                gDopplerShowExtra = false;
                gUpdateDisplay = true;
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

// Pass reminder: 3 beeps with the green LED lit for exactly the duration
// of each tone, so the blink pattern matches the beeps one-to-one.
//   fast=false (AOS-60 s): 400 ms on / 200 ms off - calm "get ready" pace
//   fast=true  (AOS-10 s / pass underway): 200 ms on / 100 ms off - urgent
// Plays synchronously on purpose: gBeepToPlay is only consumed inside
// APP_ProcessKey() (i.e. when a key is pressed), so a reminder queued
// there would stay silent until the next key press. The sequence blocks
// ~0.9-2 s; the RTC hardware keeps ticking meanwhile, only the on-screen
// clock refresh pauses and jumps to the correct time right after.
// Same audio-path sequence as AUDIO_PlayBeep().
static void DOPPLER_NotifyPass(bool fast)
{
    const unsigned int onMs  = fast ? 200u : 400u;
    const unsigned int offMs = fast ? 100u : 200u;

    AUDIO_AudioPathOff();

    if (gCurrentFunction == FUNCTION_POWER_SAVE && gRxIdleMode)
    {
        BK4819_RX_TurnOn();
    }

    SYSTEM_DelayMs(20);

    const uint16_t toneCfg = BK4819_ReadRegister(BK4819_REG_71);
    BK4819_PrepareToPlayTone(true);
    SYSTEM_DelayMs(2);
    AUDIO_AudioPathOn();
    SYSTEM_DelayMs(60);

    for (uint8_t i = 0; i < 3; i++)
    {
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
        BK4819_PlayToneRaw(880, onMs);  // beep, LED lit the whole time
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        SYSTEM_DelayMs(offMs);
    }

    AUDIO_AudioPathOff();
    SYSTEM_DelayMs(5);
    BK4819_TurnsOffTones_TurnsOnRX();
    SYSTEM_DelayMs(5);
    BK4819_WriteRegister(BK4819_REG_71, toneCfg);

    if (gEnableSpeaker)
    {
        AUDIO_AudioPathOn();
    }
}

// A UART write/erase changed a slot's contents: re-arm its reminders so a
// freshly written pass beeps again even if the old one was already played.
void DOPPLER_ResetReminders(uint8_t Slot)
{
    if (Slot >= DOPPLER_SLOT_COUNT)
    {
        return;
    }
    const uint8_t bit = (uint8_t)(1u << Slot);
    gDopplerWarn60Mask &= (uint8_t)~bit;
    gDopplerWarn10Mask &= (uint8_t)~bit;
}

// Scans every slot once per second. At AOS-60 s and AOS-10 s the reminder
// (triple beep + green LED x3) plays once per slot per pass AND the radio
// automatically jumps to that slot. A pass that is already underway and was
// never notified (late mode entry) also notifies once. The masks are
// re-armed by DOPPLER_ResetReminders() on UART write/erase.
static void DOPPLER_CheckReminders(uint32_t now)
{
    for (uint8_t s = 0; s < DOPPLER_SLOT_COUNT; s++)
    {
        DOPPLER_Satellite_t sat;
        if (!DOPPLER_SlotGetInfo(s, &sat))
        {
            continue;
        }
        const uint8_t slotBit = (uint8_t)(1u << s);
        bool notify = false;
        bool fast   = false;

        if (now < sat.start_unix)
        {
            const uint32_t togo = sat.start_unix - now;
            if (togo <= 60u && !(gDopplerWarn60Mask & slotBit))
            {
                gDopplerWarn60Mask |= slotBit;
                // Already inside the 10 s window too: this beep doubles as
                // the final warning instead of beeping twice back to back,
                // and uses the urgent pace.
                if (togo <= 10u)
                {
                    gDopplerWarn10Mask |= slotBit;
                    fast = true;
                }
                notify = true;
            }
            else if (togo <= 10u && !(gDopplerWarn10Mask & slotBit))
            {
                gDopplerWarn10Mask |= slotBit;
                notify = true;
                fast   = true;
            }
        }
        else if (now <= sat.start_unix + (uint32_t)sat.sum_time &&
                 !(gDopplerWarn60Mask & slotBit))
        {
            // Pass already underway but never notified: late entry - urgent.
            gDopplerWarn60Mask |= slotBit;
            gDopplerWarn10Mask |= slotBit;
            notify = true;
            fast   = true;
        }

        if (notify)
        {
            // Jump to the slot whose pass is about to start (or underway).
            if (s != DOPPLER_GetSlot())
            {
                gDopplerEntryValid = false;
                gDopplerPassed = false;
                DOPPLER_SelectSlot(s);
                gUpdateDisplay = true;
            }
            DOPPLER_NotifyPass(fast);
            return; // one reminder per second is enough
        }
    }
}

void DOPPLER_TimeSlice(void)
{
    if (gDopplerState != DOPPLER_STATE_TRACKING)
    {
        return;
    }

    // Turn the green LED back off once the slot-switch flash expires.
    // (The pass reminder blinks the LED synchronously inside
    // DOPPLER_NotifyPass() and does not use this countdown.)
    if (gDopplerLedTicks > 0 && --gDopplerLedTicks == 0)
    {
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    }

    // Restore the TX frequency once a Doppler transmission has ended
    if (gDopplerTxOverride && gCurrentFunction != FUNCTION_TRANSMIT)
    {
        gCurrentVfo->pTX->Frequency = gDopplerSavedTxFreq;
        gCurrentVfo->pTX->CodeType = (DCS_CodeType_t)gDopplerSavedTxCodeType;
        gCurrentVfo->pTX->Code = gDopplerSavedTxCode;
        gDopplerTxOverride = false;
    }

    // Sync our sub-second counter to the RTC 1 Hz tick and keep it running
    // at 10 ms resolution (DOPPLER_TimeSlice is called every 10 ms).
    bool secondTick = false;
    if (gRtcSecondTick)
    {
        gRtcSecondTick = false;
        gDopplerMs = 0;
        secondTick = true;
    }
    else
    {
        gDopplerMs += 10;
        if (gDopplerMs >= 1000u)
        {
            gDopplerMs = 0; // safety wrap, should stay synced via RTC
        }
    }

    // Update the RX frequency at 10 Hz (every 100 ms) with 1 s table
    // interpolation; the display only needs a 1 Hz refresh.
    if (gDopplerMs % 100u != 0)
    {
        return;
    }

    const uint32_t now = RTC_GetUnix32();

    // Once-per-second housekeeping: erase expired passes, auto-select the
    // slot whose pass is underway, and play the 60 s / 10 s reminders.
    if (secondTick)
    {
        // 0. Update squelch according to whether the active slot is in pass:
        //    outside a pass -> follow global SQL setting; during a pass -> SQL 0.
        const DOPPLER_Satellite_t *pSqlSat = DOPPLER_GetSatellite();
        const bool sqlInPass = DOPPLER_HasData()
            && now >= pSqlSat->start_unix
            && now <= pSqlSat->start_unix + (uint32_t)pSqlSat->sum_time;
        if (sqlInPass)
        {
            DOPPLER_ForceSquelchOpen();
        }
        else if (gDopplerSqlForcedOpen)
        {
            DOPPLER_RestoreSquelch();
            gDopplerSqlForcedOpen = false;
        }

        // 1. Erase every slot whose pass window has fully elapsed (this is
        //    also how a finished pass gets deleted one second after LOS).
        //    When the ACTIVE slot is the one that just got erased, hop to
        //    the slot whose pass starts next. A manually selected empty
        //    slot is left alone (hadData was already false).
        const bool hadData = DOPPLER_HasData();
        if (DOPPLER_EraseExpired(now) > 0)
        {
            gUpdateDisplay = true;
            if (hadData && !DOPPLER_HasData())
            {
                const int next = DOPPLER_FindNext(now);
                if (next >= 0)
                {
                    gDopplerEntryValid = false;
                    gDopplerPassed = false;
                    DOPPLER_SelectSlot((uint8_t)next);
                    gUpdateDisplay = true;
                }
            }
        }

        // 2. Hop to another slot whose pass is underway while ours is not.
        const DOPPLER_Satellite_t *pCur = DOPPLER_GetSatellite();
        const bool curInPass = DOPPLER_HasData()
            && now >= pCur->start_unix
            && now <= pCur->start_unix + (uint32_t)pCur->sum_time;
        if (!curInPass)
        {
            const int passing = DOPPLER_FindPassing(now);
            if (passing >= 0 && passing != (int)DOPPLER_GetSlot())
            {
                gDopplerEntryValid = false;
                gDopplerPassed = false;
                DOPPLER_SelectSlot((uint8_t)passing);
                gUpdateDisplay = true;
            }
        }

        // 3. Pre-pass reminders; also covers late entry / automatic switch.
        DOPPLER_CheckReminders(now);
    }

    const DOPPLER_Satellite_t *pSat = DOPPLER_GetSatellite();

    if (now < pSat->start_unix)
    {
        if (secondTick)
        {
            gUpdateDisplay = true;   // refresh the WAIT countdown once per second
        }
        return; // pass not started yet
    }

    DOPPLER_Entry_t Entry;
    if (DOPPLER_GetEntryInterpolated(now, gDopplerMs, &Entry))
    {
        if (!gDopplerEntryValid || gDopplerEntry.downlink != Entry.downlink)
        {
            gDopplerEntry = Entry;
            gDopplerEntryValid = true;
            // Keep the VFO struct in sync so logs/UI show the tracked frequency
            gTxVfo->freq_config_RX.Frequency = Entry.downlink;
            // Never retune the synthesizer mid-transmission: BK4819_SetFrequency
            // writes the live PLL registers (REG_38/39), so calling it while
            // transmitting would drag the TX carrier onto the downlink frequency.
            // The TX-end path (RADIO_SetupRegisters) restores RX from the VFO
            // struct above, which already holds the newest entry.
            if (gCurrentFunction != FUNCTION_TRANSMIT)
            {
                BK4819_SetFrequency(Entry.downlink);
                BK4819_PickRXFilterPathBasedOnFrequency(Entry.downlink);
            }
        }
        gDopplerPassed = false;
    }
    else
    {
        gDopplerPassed = true;
    }
    if (secondTick)
    {
        gUpdateDisplay = true;
    }
}

// ---------------------------------------------------------------------------
// Tracking screen, same layout as the Losehu K5 V1 satellite screen:
//   status line : inverse satellite name box + modulation/bandwidth
//   FB lines 0-1: big RX frequency (downlink, Doppler-compensated)
//   FB line 2   : live RSSI "-91dBm S1" + ticked signal bar (right half)
//   FB line 3-4 : BK4819 AGC gains LNAs/LNA/PGA + IF register value
//   FB line 5   : uplink frequency + countdown (inverse while transmitting)
//   FB line 6   : current Beijing date/time + pass progress bar
// All drawing uses the stock UI helpers, gFrameBuffer/gStatusLine and
// read-only BK4819 register reads - no hardware-specific pokes.
// ---------------------------------------------------------------------------

// S-level thresholds, same table as the spectrum app (spectrum.h U8RssiMap)
static uint8_t DOPPLER_Dbm2S(int16_t dBm)
{
    static const uint8_t rssiMap[10] = {121, 115, 109, 103, 97, 91, 85, 79, 73, 63};
    const int16_t v = -dBm;
    uint8_t i;
    for (i = 0; i < ARRAY_SIZE(rssiMap); i++)
    {
        if (v >= rssiMap[i])
        {
            return i;
        }
    }
    return i;
}

static void DOPPLER_RenderFrequency(const uint32_t Freq10Hz, const uint8_t Line)
{
    char Buffer[12];
    snprintf(Buffer, sizeof(Buffer), "%lu.%05lu", (unsigned long)(Freq10Hz / 100000u),
             (unsigned long)(Freq10Hz % 100000u));
    // UI_DisplayFrequency 的 Y 是帧缓冲行号(0-6), 大字占 Y 和 Y+1 两行
    UI_DisplayFrequency(Buffer, 0, Line, true);
}

// status line: inverse name box (Losehu style) + "FM 25k"/"FM 12k"; the
// default status icons (battery, ...) on the right are left untouched.
// Also called from UI_DisplayStatus() so status-bar refreshes (battery,
// key events) redraw the name box instead of wiping it.
void DOPPLER_RenderStatusStrip(const DOPPLER_Satellite_t *pSat)
{
    // "N:NAME" - slot digit prefix; the name is truncated so the inverse
    // box (max 67 px) stays clear of the bandwidth label at x=72.
    char Name[12];
    Name[0] = (char)('1' + DOPPLER_GetSlot());
    Name[1] = ':';
    uint8_t len = (uint8_t)strlen(pSat->name);
    if (len > 7u)
    {
        len = 7u;
    }
    memcpy(&Name[2], pSat->name, len);
    Name[2 + len] = 0;

    const uint8_t w = (uint8_t)((len + 2u) * 7u + 4u);
    memset(gStatusLine, 0, w); // clear default icons under the name
    UI_PrintStringSmallBufferNormal(Name, gStatusLine + 2);
    for (uint8_t x = 0; x < w; x++)
    {
        gStatusLine[x] ^= 0xFF; // inverse-video name box
    }

    const char *pBw = (gTxVfo->CHANNEL_BANDWIDTH == BK4819_FILTER_BW_WIDE) ? "FM 25k" : "FM 12k";
    UI_PrintStringSmallBufferNormal(pBw, gStatusLine + 72);

    ST7565_BlitStatusLine();
}

// line 2: "-91dBm S1" + ticked signal bar (bar range -130..-50 dBm)
static void DOPPLER_RenderRssi(void)
{
    char Buffer[16];
    const bool tx = (gCurrentFunction == FUNCTION_TRANSMIT);

    const uint8_t X0 = 72; // bar occupies x=72..127 (worst-case text "-127dBm S10" ends at 71)
    memset(&gFrameBuffer[2][X0], 0b01000000, LCD_WIDTH - X0);
    for (uint8_t i = 0; i < LCD_WIDTH - X0; i += 6)
    {
        gFrameBuffer[2][X0 + i] = 0b01100000; // tick marks
    }

    if (tx)
    {
        // RSSI is meaningless while transmitting
        snprintf(Buffer, sizeof(Buffer), "TX");
    }
    else
    {
        const int16_t dBm = BK4819_GetRSSI_dBm();
        snprintf(Buffer, sizeof(Buffer), "%ddBm S%u", (int)dBm, (unsigned)DOPPLER_Dbm2S(dBm));

        int32_t px = ((int32_t)dBm + 130) * (LCD_WIDTH - X0) / 80;
        if (px < 0) px = 0;
        if (px > LCD_WIDTH - X0) px = LCD_WIDTH - X0;
        for (uint8_t i = 0; i < (uint8_t)px; i++)
        {
            if (i % 6 != 0)
            {
                gFrameBuffer[2][X0 + i] |= 0b00001110;
            }
        }

        // marker at the squelch-open threshold (same conversion as RSSI dBm)
        const int16_t trigDbm = (int16_t)(gTxVfo->SquelchOpenRSSIThresh / 2) - 160;
        int32_t txp = ((int32_t)trigDbm + 130) * (LCD_WIDTH - X0) / 80;
        if (txp >= 0 && txp < LCD_WIDTH - X0)
        {
            gFrameBuffer[2][X0 + (uint8_t)txp] = 0b11111111;
        }
    }
    UI_PrintStringSmallNormal(Buffer, 2, 0, 2);
}

// lines 3-4: BK4819 AGC register readback, same source as Losehu
// (LNAs/LNA/PGA from REG_13, IF from REG_3D) - read-only, safe
static void DOPPLER_RenderGains(void)
{
    const uint16_t reg13 = BK4819_ReadRegister(BK4819_REG_13);
    const uint16_t regIf = BK4819_ReadRegister(BK4819_REG_3D);
    char Buffer[8];

    UI_PrintStringSmallNormal("LNAs", 2, 0, 3);
    UI_PrintStringSmallNormal("LNA", 36, 0, 3);
    UI_PrintStringSmallNormal("PGA", 70, 0, 3);
    UI_PrintStringSmallNormal("IF", 93, 0, 3);

    snprintf(Buffer, sizeof(Buffer), "%u", (unsigned)((reg13 >> 8) & 0x3u));
    UI_PrintStringSmallNormal(Buffer, 2, 0, 4);
    snprintf(Buffer, sizeof(Buffer), "%u", (unsigned)((reg13 >> 5) & 0x7u));
    UI_PrintStringSmallNormal(Buffer, 36, 0, 4);
    snprintf(Buffer, sizeof(Buffer), "%u", (unsigned)(reg13 & 0x7u));
    UI_PrintStringSmallNormal(Buffer, 70, 0, 4);
    snprintf(Buffer, sizeof(Buffer), "%u", (unsigned)regIf);
    UI_PrintStringSmallNormal(Buffer, 93, 0, 4);
}

// line 5: uplink frequency + pass countdown at the right edge;
// the line is inverse-video while transmitting
static void DOPPLER_RenderUplink(const DOPPLER_Satellite_t *pSat, const uint32_t Now)
{
    char Buffer[20];
    const bool tx = (gCurrentFunction == FUNCTION_TRANSMIT);

    if (gDopplerPassed)
    {
        UI_PrintStringSmallNormalInverse("PASSED", 2, 0, 5);
        return;
    }

    uint32_t up = 0;
    if (gDopplerEntryValid)
    {
        up = gDopplerEntry.uplink;
    }
    else
    {
        // pass not started: preview the first table entry
        DOPPLER_Entry_t e0;
        if (DOPPLER_GetEntryInterpolated((int32_t)pSat->start_unix, 0, &e0))
        {
            up = e0.uplink;
        }
    }

    if (up > 0)
    {
        // 2 decimals keeps the line clear of the countdown slot at x=93
        snprintf(Buffer, sizeof(Buffer), "%s:%lu.%02lu", tx ? "TX" : "UPLink",
                 (unsigned long)(up / 100000u), (unsigned long)((up % 100000u) / 1000u));
    }
    else
    {
        snprintf(Buffer, sizeof(Buffer), "%s: ---", tx ? "TX" : "UPLink");
    }
    if (tx)
    {
        UI_PrintStringSmallNormalInverse(Buffer, 2, 0, 5);
        // while transmitting the right slot confirms the TX tone instead
        if (pSat->send_ctcss > 0)
        {
            snprintf(Buffer, sizeof(Buffer), "T:%u.%u", (unsigned)(pSat->send_ctcss / 10u),
                     (unsigned)(pSat->send_ctcss % 10u));
            UI_PrintStringSmallNormal(Buffer, (uint8_t)(LCD_WIDTH - strlen(Buffer) * 7u), 0, 5);
        }
        return;
    }
    UI_PrintStringSmallNormal(Buffer, 2, 0, 5);

    // countdown slot (max 4 chars): "Long" / "-320" / "+45"
    const int32_t togo = (int32_t)pSat->start_unix - (int32_t)Now;
    if (togo > 1000)
    {
        snprintf(Buffer, sizeof(Buffer), "Long");
    }
    else if (togo > 0)
    {
        snprintf(Buffer, sizeof(Buffer), "-%ld", (long)togo);
    }
    else
    {
        snprintf(Buffer, sizeof(Buffer), "+%ld", (long)((int32_t)pSat->sum_time + togo));
    }
    UI_PrintStringSmallNormal(Buffer, (uint8_t)(LCD_WIDTH - strlen(Buffer) * 7u), 0, 5);
}

// Draw progress bar; AOS bar empties from W to 0, pass bar fills from 0 to W.
static void __attribute__((noinline)) DOPPLER_DrawProgressBar(const DOPPLER_Satellite_t *pSat, const uint32_t Now, const uint8_t Line, const uint8_t X0, const uint8_t W)
{
    uint8_t process = 0;
    if (gDopplerPassed)
    {
        process = W;
    }
    else
    {
        const int32_t togo = (int32_t)pSat->start_unix - (int32_t)Now;
        if (togo > 0)
        {
            if (togo <= 1000)
                process = (uint8_t)((uint32_t)togo * W / 1000u);
        }
        else
        {
            const int32_t remain = (int32_t)pSat->sum_time + togo;
            if (remain > 0)
                process = (uint8_t)(W - (uint32_t)remain * W / pSat->sum_time);
            else
                process = W;
        }
    }

    memset(&gFrameBuffer[Line][X0], 0b01000000, W);
    gFrameBuffer[Line][X0 - 1] = 0b00111110;
    gFrameBuffer[Line][X0 + W] = 0b00111110;
    for (uint8_t i = 0; i < W; i++)
    {
        gFrameBuffer[Line][X0 + i] = (i < process) ? 0b00111110 : 0b00100010;
    }
}

static void __attribute__((noinline)) DOPPLER_RenderDateProgress(const DOPPLER_Satellite_t *pSat, const uint32_t Now)
{
    char Buffer[20];
    uint8_t t[6];
    DOPPLER_UnixToDate(Now, t);
    snprintf(Buffer, sizeof(Buffer), "%02u-%02u %02u:%02u:%02u",
             (unsigned)t[1], (unsigned)t[2], (unsigned)t[3], (unsigned)t[4], (unsigned)t[5]);
    UI_PrintStringSmallNormal(Buffer, 2, 0, 6);

    DOPPLER_DrawProgressBar(pSat, Now, 6, 101, 25); // bar body x=101..125
}

// Slot summary screen shown when key 3 is pressed in tracking mode.
static void DOPPLER_RenderSlotSummary(void)
{
    char buffer[20];
    DOPPLER_Satellite_t sat;

    UI_PrintStringSmallNormal("SLOTS", 0, 127, 0);
    for (uint8_t slot = 0; slot < DOPPLER_SLOT_COUNT; slot++)
    {
        if (DOPPLER_SlotGetInfo(slot, &sat))
        {
            snprintf(buffer, sizeof(buffer), "SLOT%u: %s", (unsigned)(slot + 1u), sat.name);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "SLOT%u: EMPTY", (unsigned)(slot + 1u));
        }
        UI_PrintStringSmallNormal(buffer, 2, 0, (uint8_t)(slot + 1u));
    }
}

// supplementary info screen shown when key 2 is pressed in tracking mode
static void DOPPLER_RenderExtraInfo(const DOPPLER_Satellite_t *pSat, const DOPPLER_Entry_t *pEntry, bool entryValid, const uint32_t Now)
{
    char Buffer[20];

    // line 0: satellite name (with slot prefix)
    snprintf(Buffer, sizeof(Buffer), "SAT%u: %s", DOPPLER_GetSlot() + 1u, pSat->name);
    UI_PrintStringSmallNormal(Buffer, 0, 127, 0);

    // line 1-4: altitude, distance, azimuth, elevation
    if (entryValid)
    {
        const uint16_t az = pEntry->azimuth_0_1deg;
        const int16_t el = pEntry->elevation_0_1deg;
        snprintf(Buffer, sizeof(Buffer), "ALT %4ukm", (unsigned)pEntry->altitude_km);
        UI_PrintStringSmallNormal(Buffer, 0, 127, 1);
        snprintf(Buffer, sizeof(Buffer), "DIS %4ukm", (unsigned)pEntry->distance_km);
        UI_PrintStringSmallNormal(Buffer, 0, 127, 2);
        snprintf(Buffer, sizeof(Buffer), "AZ  %3u.%1u", az / 10u, az % 10u);
        UI_PrintStringSmallNormal(Buffer, 0, 127, 3);
        snprintf(Buffer, sizeof(Buffer), "EL  %3u.%1u",
                 (unsigned)(el / 10), (unsigned)(el < 0 ? -(el % 10) : (el % 10)));
        UI_PrintStringSmallNormal(Buffer, 0, 127, 4);
    }
    else
    {
        UI_PrintStringSmallNormal("ALT ----km", 0, 127, 1);
        UI_PrintStringSmallNormal("DIS ----km", 0, 127, 2);
        UI_PrintStringSmallNormal("AZ  ----",   0, 127, 3);
        UI_PrintStringSmallNormal("EL  ----",   0, 127, 4);
    }

    // line 5: countdown to AOS or LOS in xxHxxMxxS format
    const int32_t togo = (int32_t)pSat->start_unix - (int32_t)Now;
    const int32_t remain = (int32_t)pSat->sum_time + togo;
    if (gDopplerPassed || remain <= 0)
    {
        UI_PrintStringSmallNormalInverse("PASSED", 0, 127, 5);
    }
    else
    {
        uint32_t secs = (togo > 0) ? (uint32_t)togo : (uint32_t)remain;
        uint8_t  h = (uint8_t)(secs / 3600u);
        uint8_t  m = (uint8_t)((secs / 60u) % 60u);
        uint8_t  s = (uint8_t)(secs % 60u);
        const char *label = (togo > 0) ? "AOS" : "LOS";
        snprintf(Buffer, sizeof(Buffer), "%s %02uH%02uM%02uS", label, h, m, s);
        UI_PrintStringSmallNormal(Buffer, 0, 127, 5);
    }

    // line 6: keep the familiar date/progress bar
    DOPPLER_RenderDateProgress(pSat, Now);
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

    if (gDopplerState == DOPPLER_STATE_ADJUST)
    {
        // Layout (128x64, 8 text rows of 8px):
        //   0-1 big "ADJ TIME" | 2 small date | 3 small time
        //   4-5 big field name  | 6 small keys  | 7 small exit hint
        // Keep every small-font string <= 18 chars (7 px/char) to stay on screen.
        char buf[12];
        const char *fields[6] = {"YEAR", "MON", "DAY", "HOUR", "MIN", "SEC"};

        UI_PrintString("ADJ TIME", 0, 127, 0, 8);

        snprintf(buf, sizeof(buf), "20%02u-%02u-%02u",
                 gDopplerAdjustTime[0], gDopplerAdjustTime[1], gDopplerAdjustTime[2]);
        UI_PrintStringSmallNormal(buf, 0, 127, 2);

        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 gDopplerAdjustTime[3], gDopplerAdjustTime[4], gDopplerAdjustTime[5]);
        UI_PrintStringSmallNormal(buf, 0, 127, 3);

        UI_PrintString(fields[gDopplerAdjustField], 0, 127, 4, 8);

        UI_PrintStringSmallNormal("1+ 2- 0:NEXT", 0, 127, 6);
        UI_PrintStringSmallNormal("M:MAIN EXIT=SAVE", 0, 127, 7);
        ST7565_BlitFullScreen();
        return;
    }

    // TRACKING
    const DOPPLER_Satellite_t *pSat = DOPPLER_GetSatellite();

    if (gDopplerShowSlots)
    {
        DOPPLER_RenderSlotSummary();
        ST7565_BlitFullScreen();
        return;
    }

    if (!DOPPLER_HasData())
    {
        UI_PrintString("NO DATA", 0, 127, 1, 8);
        char buf[20];
        snprintf(buf, sizeof(buf), "SLOT%u EMPTY", DOPPLER_GetSlot() + 1u);
        UI_PrintStringSmallNormal(buf, 0, 127, 3);
        // slot map: list the slots that actually hold a pass
        snprintf(buf, sizeof(buf), "DATA IN:");
        uint8_t found = 0;
        for (uint8_t s = 0; s < DOPPLER_SLOT_COUNT; s++)
        {
            if (DOPPLER_SlotHasData(s))
            {
                const size_t l = strlen(buf);
                snprintf(buf + l, sizeof(buf) - l, " %u", (unsigned)(s + 1u));
                found++;
            }
        }
        if (found == 0)
        {
            // Nothing valid anywhere: dump slot 1 raw fields to pinpoint why.
            DOPPLER_Diag_t d;
            if (DOPPLER_DiagSlot(0, &d))
            {
                snprintf(buf, sizeof(buf), "S1 n%u s%u", (unsigned)d.name0, (unsigned)d.sum_time);
                UI_PrintStringSmallNormal(buf, 0, 127, 4);
                snprintf(buf, sizeof(buf), "CRC %02X/%02X", (unsigned)d.crc_calc, (unsigned)d.crc_stored);
                UI_PrintStringSmallNormal(buf, 0, 127, 5);
            }
            else
            {
                snprintf(buf, sizeof(buf), "ALL SLOTS EMPTY");
                UI_PrintStringSmallNormal(buf, 0, 127, 4);
            }
        }
        else
        {
            UI_PrintStringSmallNormal(buf, 0, 127, 4);
        }
        UI_PrintStringSmallNormal("F+1-4 SEL", 0, 127, 6);
        ST7565_BlitFullScreen();
        return;
    }

    const uint32_t now = RTC_GetUnix32();

    DOPPLER_RenderStatusStrip(pSat);

    if (gDopplerShowExtra)
    {
        DOPPLER_RenderExtraInfo(pSat, &gDopplerEntry, gDopplerEntryValid, now);
        ST7565_BlitFullScreen();
        return;
    }

    // waiting: show the live VFO frequency (radio not retuned yet);
    // tracking/passed: show the tracked (or last) downlink entry
    const uint32_t down = gDopplerEntryValid ? gDopplerEntry.downlink
                                             : gTxVfo->freq_config_RX.Frequency;
    DOPPLER_RenderFrequency(down, 0);        // FB lines 0-1

    DOPPLER_RenderRssi();                    // FB line 2
    DOPPLER_RenderGains();                   // FB lines 3-4
    DOPPLER_RenderUplink(pSat, now);         // FB line 5
    DOPPLER_RenderDateProgress(pSat, now);   // FB line 6

    ST7565_BlitFullScreen();   // 推送 LCD
}

#endif // ENABLE_FEAT_F4HWN_DOPPLER
