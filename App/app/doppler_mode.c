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
static bool     gDopplerFSlotArm = false;  // F key pressed, waiting for a slot digit (1..4)
static uint8_t  gDopplerLedTicks = 0;      // green RX LED flash countdown (10 ms ticks)

static uint32_t gDopplerSavedRxFreq = 0;
static uint32_t gDopplerSavedTxFreq = 0;
static uint8_t  gDopplerSavedTxCodeType = 0;
static uint8_t  gDopplerSavedTxCode = 0;

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

    // If the default slot (0) is empty, jump to the first slot that holds a
    // valid pass, so the user immediately sees data after writing any slot.
    if (!DOPPLER_HasData())
    {
        for (uint8_t s = 0; s < DOPPLER_SLOT_COUNT; s++)
        {
            if (DOPPLER_SlotHasData(s))
            {
                DOPPLER_SelectSlot(s);
                break;
            }
        }
    }

    gDopplerSavedRxFreq = gTxVfo->freq_config_RX.Frequency;
    gDopplerTxOverride = false;

    RTC_EnableSecondIT(true);

    gDopplerPassed = false;
    gDopplerEntryValid = false;

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
    if (gDopplerLedTicks > 0)
    {
        gDopplerLedTicks = 0;
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    }

    gDopplerState = DOPPLER_STATE_OFF;
    gDopplerShowExtra = false;
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
            if (Key == KEY_EXIT)
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
                if (gDopplerShowExtra)
                {
                    gDopplerShowExtra = false;
                    gUpdateDisplay = true;
                }
                else
                {
                    DOPPLER_ExitMode();
                }
            }
            else if (Key == KEY_2)
            {
                gDopplerShowExtra = !gDopplerShowExtra;
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

void DOPPLER_TimeSlice(void)
{
    if (gDopplerState != DOPPLER_STATE_TRACKING)
    {
        return;
    }

    // Turn the green LED back off once the slot-switch flash expires
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
        UI_PrintStringSmallNormal("EXIT=SAVE", 0, 127, 7);
        ST7565_BlitFullScreen();
        return;
    }

    // TRACKING
    const DOPPLER_Satellite_t *pSat = DOPPLER_GetSatellite();

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
