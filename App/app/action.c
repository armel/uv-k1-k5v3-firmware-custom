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

#include <assert.h>
#include <string.h>

#include "app/action.h"
#include "app/app.h"
#include "app/chFrScanner.h"
#include "app/common.h"
#include "app/dtmf.h"
#ifdef ENABLE_FLASHLIGHT
    #include "app/flashlight.h"
#endif
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/scanner.h"
#include "audio.h"
#ifdef ENABLE_FMRADIO
    #include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/backlight.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/ui.h"
#ifdef ENABLE_FEAT_F4HWN_BEAM
    #include "app/beam.h"
#endif
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    #include "app/rxtx_log.h"
#endif
#ifdef ENABLE_FEAT_F4HWN_FOXHUNT
    #include "app/foxhunt.h"
#endif
#ifdef ENABLE_FEAT_F4HWN_ACTION_PICKER
    #include "ui/menu.h"
#endif

#if defined(ENABLE_FMRADIO)
static void ACTION_Scan_FM(bool bRestart);
#endif

#ifdef ENABLE_TX1750
static void ACTION_1750(void);
#endif

inline static void ACTION_ScanRestart() { ACTION_Scan(true); };

void (*const action_opt_table[])(void) = {
    [ACTION_OPT_NONE] = &FUNCTION_NOP,
    [ACTION_OPT_POWER] = &ACTION_Power,
    [ACTION_OPT_MONITOR] = &ACTION_Monitor,
    [ACTION_OPT_SCAN] = &ACTION_ScanRestart,
    [ACTION_OPT_KEYLOCK] = &COMMON_KeypadLockToggle,
    [ACTION_OPT_A_B] = &COMMON_SwitchVFOs,
    [ACTION_OPT_VFO_MR] = &COMMON_SwitchVFOMode,
    [ACTION_OPT_SWITCH_DEMODUL] = &ACTION_SwitchDemodul,

#ifdef ENABLE_FLASHLIGHT
    [ACTION_OPT_FLASHLIGHT] = &ACTION_FlashLight,
#else
    [ACTION_OPT_FLASHLIGHT] = &FUNCTION_NOP,
#endif

#ifdef ENABLE_VOX
    [ACTION_OPT_VOX] = &ACTION_Vox,
#else
    [ACTION_OPT_VOX] = &FUNCTION_NOP,
#endif

#ifdef ENABLE_FMRADIO
    [ACTION_OPT_FM] = &ACTION_FM,
#else
    [ACTION_OPT_FM] = &FUNCTION_NOP,
#endif

    [ACTION_OPT_RESERVED_ALARM] = &FUNCTION_NOP,

#ifdef ENABLE_TX1750
    [ACTION_OPT_1750] = &ACTION_1750,
#else
    [ACTION_OPT_1750] = &FUNCTION_NOP,
#endif

    [ACTION_OPT_RESERVED_BLMIN_TMP_OFF] = &FUNCTION_NOP,

#ifdef ENABLE_FEAT_F4HWN
    [ACTION_OPT_RXMODE] = &ACTION_RxMode,
    [ACTION_OPT_MAINONLY] = &ACTION_MainOnly,
    [ACTION_OPT_PTT] = &ACTION_Ptt,
    [ACTION_OPT_WN] = &ACTION_Wn,
    [ACTION_OPT_BACKLIGHT] = &ACTION_BackLight,
    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
        [ACTION_OPT_MUTE] = &ACTION_Mute,
    //#else
    //    [ACTION_OPT_MUTE] = &FUNCTION_NOP,
    //#endif
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        [ACTION_OPT_RXA] = &ACTION_RxA,
    #else
        [ACTION_OPT_RXA] = &FUNCTION_NOP,
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        [ACTION_OPT_POWER_HIGH] = &ACTION_Power_High,
        [ACTION_OPT_REMOVE_OFFSET] = &ACTION_Remove_Offset,
    #endif
#else
    [ACTION_OPT_RXMODE] = &FUNCTION_NOP,
#endif
#ifdef ENABLE_FEAT_F4HWN_BEAM
    [ACTION_OPT_BEAM] = &ACTION_Beam,
#endif
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    [ACTION_OPT_RXTX_LOG] = &ACTION_RxTxLog,
#endif
#ifdef ENABLE_FEAT_F4HWN_FOXHUNT
    [ACTION_OPT_FOXHUNT] = &ACTION_FoxHunt,
#endif
};

static_assert(ARRAY_SIZE(action_opt_table) == ACTION_OPT_LEN);

void ACTION_Power(void)
{
    if (++gTxVfo->OUTPUT_POWER > OUTPUT_POWER_HIGH)
        gTxVfo->OUTPUT_POWER = OUTPUT_POWER_LOW1;

    gRequestSaveChannel = 1;

    gRequestDisplayScreen = gScreenToDisplay;

#ifdef ENABLE_VOICE
    gAnotherVoiceID   = VOICE_ID_POWER;
#endif

}

void ACTION_Monitor(void)
{
    if (gCurrentFunction != FUNCTION_MONITOR) { // enable the monitor
        RADIO_SelectVfos();
#ifdef ENABLE_NOAA
        if (IS_NOAA_CHANNEL(gRxVfo->CHANNEL_SAVE) && gIsNoaaMode)
            gNoaaChannel = gRxVfo->CHANNEL_SAVE - NOAA_CHANNEL_FIRST;
#endif
        RADIO_SetupRegisters(true);
        APP_StartListening(FUNCTION_MONITOR);
        return;
    }

    gMonitor = false;

    if (gScanStateDir != SCAN_OFF) {
        gScanPauseDelayIn_10ms = scan_pause_delay_in_1_10ms;
        gScheduleScanListen    = false;
        gScanPauseMode         = true;
    }

#ifdef ENABLE_NOAA
    if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF && gIsNoaaMode) {
        gNOAA_Countdown_10ms = NOAA_countdown_10ms;
        gScheduleNOAA        = false;
    }
#endif

    RADIO_SetupRegisters(true);

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode) {
        FM_Start();
        gRequestDisplayScreen = DISPLAY_FM;
    }
    else
#endif
        gRequestDisplayScreen = gScreenToDisplay;
}

void ACTION_Scan(bool bRestart)
{
    (void)bRestart;

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode) {
        ACTION_Scan_FM(bRestart);
        return;
    }
#endif

    if (SCANNER_IsScanning()) {
        return;
    }

    // not scanning
    gMonitor = false;

#ifdef ENABLE_DTMF_CALLING
    DTMF_clear_RX();
#endif
    gDTMF_RX_live_timeout = 0;
    DTMF_clear_input_box_memory();

    RADIO_SelectVfos();

#ifdef ENABLE_NOAA
    if (IS_NOAA_CHANNEL(gRxVfo->CHANNEL_SAVE)) {
        return;
    }
#endif

    GUI_SelectNextDisplay(DISPLAY_MAIN);

    if (gScanStateDir != SCAN_OFF) {
        // already scanning

        if (!IS_MR_CHANNEL(gNextMrChannel)) {
            CHFRSCANNER_Stop();
#ifdef ENABLE_VOICE
            gAnotherVoiceID = VOICE_ID_SCANNING_STOP;
#endif
            return;
        }

        // channel mode. Keep scanning but toggle between scan lists
        RADIO_NextValidList(1);
        UI_MAIN_NotifyScanListChanged();

        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
            SETTINGS_WriteCurrentState();
        #endif

        // jump to the next channel
        CHFRSCANNER_ManualResume(gScanStateDir);
    } else {
        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        if(gScanRangeStart == 0) // No ScanRange
        {
            gEeprom.CURRENT_STATE = 1;
        }
        else // ScanRange
        {
            gEeprom.CURRENT_STATE = 2;
        }
        SETTINGS_WriteCurrentState();
        #endif
        // start scanning
        CHFRSCANNER_Start(true, SCAN_FWD);

#ifdef ENABLE_VOICE
        AUDIO_SetVoiceID(0, VOICE_ID_SCANNING_BEGIN);
        AUDIO_PlaySingleVoice(true);
#endif

        // clear the other vfo's rssi level (to hide the antenna symbol)
        gVFO_RSSI_bar_level[(gEeprom.RX_VFO + 1) & 1U] = 0;

        // let the user see DW is not active
        gDualWatchActive = false;
    }

    gUpdateStatus = true;
}


void ACTION_SwitchDemodul(void)
{
    gRequestSaveChannel = 1;

    gTxVfo->Modulation++;

    if(gTxVfo->Modulation == MODULATION_UKNOWN)
        gTxVfo->Modulation = MODULATION_FM;
}


#ifdef ENABLE_FMRADIO
inline static bool ACTION_IsBlockedInFM(uint8_t action)
{
    switch (action) {
        case ACTION_OPT_POWER:
        case ACTION_OPT_MONITOR:
        case ACTION_OPT_A_B:
        case ACTION_OPT_VFO_MR:
        case ACTION_OPT_SWITCH_DEMODUL:
#ifdef ENABLE_VOX
        case ACTION_OPT_VOX:
#endif
#ifdef ENABLE_FEAT_F4HWN
        case ACTION_OPT_RXMODE:
        case ACTION_OPT_MAINONLY:
        case ACTION_OPT_WN:
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        case ACTION_OPT_RXA:
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        case ACTION_OPT_POWER_HIGH:
        case ACTION_OPT_REMOVE_OFFSET:
    #endif
#endif
#ifdef ENABLE_FEAT_F4HWN_BEAM
        case ACTION_OPT_BEAM:
#endif
#ifdef ENABLE_FEAT_F4HWN_FOXHUNT
        case ACTION_OPT_FOXHUNT:
#endif
            return true;

        default:
            return false;
    }
}
#endif

#ifdef ENABLE_FEAT_F4HWN_ACTION_PICKER
static void ACTION_Execute(uint8_t action)
{
    if (action >= ACTION_OPT_LEN || action_opt_table[action] == NULL) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode && ACTION_IsBlockedInFM(action)) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }
#endif

    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    action_opt_table[action]();
}

uint8_t gActionPickerKey;
uint8_t gActionPickerSelection[2] = {1, 1};
uint8_t gActionPickerTimeout_500ms;

bool ACTION_PickerProcessKey(KEY_Code_t key, bool isPressed, bool isHeld)
{
    if (gActionPickerKey == 0)
        return false;
    if (isPressed)
        gActionPickerTimeout_500ms = ACTION_PICKER_TIMEOUT_500MS;
    uint8_t *selection = &gActionPickerSelection[gActionPickerKey - 1];

    switch (key) {
        case KEY_UP:
        case KEY_DOWN:
            if (isPressed && !isHeld) {
                if (key == KEY_UP) {
                    if (--*selection == 0)
                        *selection = gSubMenu_SIDEFUNCTIONS_size - 1;
                }
                else if (++*selection >= gSubMenu_SIDEFUNCTIONS_size) {
                    *selection = 1;
                }

                gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
                gUpdateDisplay = true;
            }
            return true;

        case KEY_MENU:
            if (!isPressed && !isHeld) {
                const uint8_t action = gSubMenu_SIDEFUNCTIONS[*selection].id;
                gActionPickerKey = 0;
                gUpdateDisplay = true;
                ACTION_Execute(action);
            }
            return true;

        case KEY_EXIT:
        case KEY_F:
            if (!isPressed) {
                gActionPickerKey = 0;
                gUpdateDisplay = true;
            }
            return true;

        case KEY_PTT:
            gActionPickerKey = 0;
            gUpdateDisplay = true;
            return false;

        default:
            return true;
    }
}
#endif

void ACTION_Handle(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    HideFKeyIcon();

    if (gScreenToDisplay == DISPLAY_MAIN && gDTMF_InputMode){
         // entering DTMF code

        gPttWasReleased = true;

        if (Key != KEY_SIDE1 || bKeyHeld || !bKeyPressed){
            return;
        }

        // side1 btn pressed

        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        gRequestDisplayScreen = DISPLAY_MAIN;

        if (gDTMF_InputBox_Index <= 0) {
            // turn off DTMF input box if no codes left
            gDTMF_InputMode = false;
            return;
        }

        // DTMF codes are in the input box
        gDTMF_InputBox[--gDTMF_InputBox_Index] = '-'; // delete one code

#ifdef ENABLE_VOICE
        gAnotherVoiceID   = VOICE_ID_CANCEL;
#endif
        return;
    }

    enum ACTION_OPT_t func = ACTION_OPT_NONE;
    switch(Key) {
        case KEY_SIDE1:
            if (bKeyHeld)
                func = gEeprom.KEY_1_LONG_PRESS_ACTION;
            else
                func = gEeprom.KEY_1_SHORT_PRESS_ACTION;
            break;
        case KEY_SIDE2:
            if (bKeyHeld)
                func = gEeprom.KEY_2_LONG_PRESS_ACTION;
            else
                func = gEeprom.KEY_2_SHORT_PRESS_ACTION;
            break;
        case KEY_MENU:
            if (bKeyHeld)
                func = gEeprom.KEY_M_LONG_PRESS_ACTION;
            break;
        default:
            break;
    }

    if (bKeyHeld != bKeyPressed) { // button pushed or released after hold 
                                   // (!bKeyHeld && bKeyPressed) or (bKeyHeld && !bKeyPressed)
        return;
    }

    // held or released after short press
#ifdef ENABLE_FEAT_F4HWN_ACTION_PICKER
    ACTION_Execute(func);
#else
    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode && ACTION_IsBlockedInFM(func)) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }
#endif

    action_opt_table[func]();
#endif
}


#ifdef ENABLE_FMRADIO
void ACTION_FM(void)
{
    if (gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR)
    {
        gInputBoxIndex = 0;

        if (gFmRadioMode) {
            FM_TurnOff();
            gFlagReconfigureVfos  = true;
            gRequestDisplayScreen = DISPLAY_MAIN;

#ifdef ENABLE_VOX
            gVoxResumeCountdown = 80;
#endif
            return;
        }

        gMonitor = false;

        if (gScanStateDir != SCAN_OFF) {
            // Stop the channel/frequency scan before switching to the FM radio.
            gScanKeepResult = false;
            CHFRSCANNER_Stop();
        }

        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);

        FM_Start();

        gRequestDisplayScreen = DISPLAY_FM;
    }
}

static void ACTION_Scan_FM(bool bRestart)
{
    if (FUNCTION_IsRx())
        return;

    GUI_SelectNextDisplay(DISPLAY_FM);

    gMonitor = false;

    if (gFM_ScanState != FM_SCAN_OFF) {
        FM_PlayAndUpdate();

#ifdef ENABLE_VOICE
        gAnotherVoiceID = VOICE_ID_SCANNING_STOP;
#endif
        return;
    }

    uint16_t freq;

    if (bRestart) {
        gFM_AutoScan = true;
        gFM_ChannelPosition = 0;
        FM_EraseChannels();
        freq = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
    } else {
        gFM_AutoScan = false;
        gFM_ChannelPosition = 0;
        freq = gEeprom.FM_FrequencyPlaying;
    }

    BK1080_GetFrequencyDeviation(freq);
    FM_Tune(freq, 1, bRestart);

#ifdef ENABLE_VOICE
    gAnotherVoiceID = VOICE_ID_SCANNING_BEGIN;
#endif

}

#endif


#ifdef ENABLE_TX1750
static void ACTION_1750(void)
{
    if(gEeprom.KEY_LOCK && (gSetting_set_lck & SET_LCK_PTT))
        return;

    gTx1750Active = true;
    gInputBoxIndex = 0;
    gFlagPrepareTX = true;

    if (gScreenToDisplay != DISPLAY_MENU)     // 1of11 .. don't close the menu
        gRequestDisplayScreen = DISPLAY_MAIN;
}
#endif

#ifdef ENABLE_VOX
void ACTION_Vox(void)
{
    gEeprom.VOX_SWITCH   = !gEeprom.VOX_SWITCH;
    gRequestSaveSettings = true;
    gFlagReconfigureVfos = true;
    gUpdateStatus        = true;

    #ifdef ENABLE_VOICE
        gAnotherVoiceID  = VOICE_ID_VOX;
    #endif
}
#endif

#ifdef ENABLE_FEAT_F4HWN
void ACTION_Update(void)
{
    gSaveRxMode          = false;
    gFlagReconfigureVfos = true;
    gUpdateStatus        = true;
}

void ACTION_RxMode(void)
{
    static bool cycle = 0;

    if (cycle) {
        gEeprom.CROSS_BAND_RX_TX = !gEeprom.CROSS_BAND_RX_TX;
    } else {
        gEeprom.DUAL_WATCH = !gEeprom.DUAL_WATCH;
    }

    cycle = !cycle;
    ACTION_Update();
}

void ACTION_MainOnly(void)
{
    static bool cycle = 0;
    static uint8_t dw = 0;
    static uint8_t cb = 0;

    if (cycle) {
        gEeprom.DUAL_WATCH = dw;
        gEeprom.CROSS_BAND_RX_TX = cb;
    } else {
        dw = gEeprom.DUAL_WATCH;
        cb = gEeprom.CROSS_BAND_RX_TX;

        gEeprom.DUAL_WATCH = 0;
        gEeprom.CROSS_BAND_RX_TX = 0;
    }

    cycle = !cycle;
    ACTION_Update();
}

#ifdef ENABLE_FEAT_F4HWN_AUDIO
void ACTION_RxA(void)
{
    if(gRxVfo->Modulation == MODULATION_AM)
        gSetting_set_audio_am = (gSetting_set_audio_am + 1) % 3;
    else if (gRxVfo->Modulation == MODULATION_FM)
        gSetting_set_audio_fm = (gSetting_set_audio_fm + 1) % 5;

    RADIO_SetModulation(gRxVfo->Modulation);
}
#endif

void ACTION_Ptt(void)
{
    gSetting_set_ptt_session = !gSetting_set_ptt_session;

    ACTION_Update();
}

void ACTION_Wn(void)
{
    const bool isRx = FUNCTION_IsRx();
    VFO_Info_t *pVfo = isRx ? gRxVfo : gTxVfo;

    pVfo->CHANNEL_BANDWIDTH = !pVfo->CHANNEL_BANDWIDTH;

    if (pVfo->Modulation == MODULATION_AM)
    {
        BK4819_SetFilterBandwidth(RADIO_GetAMFilterBandwidth(pVfo), true);
        return;
    }

    uint8_t bw = pVfo->CHANNEL_BANDWIDTH;

    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        if (isRx && bw == BANDWIDTH_NARROW && gSetting_set_nfm == 1)
        {
            bw++; 
        }
    #endif

    BK4819_SetFilterBandwidth(bw, false);
}

void ACTION_BackLight(void)
{
    if(gBackLight)
    {
        gEeprom.BACKLIGHT_TIME = gBacklightTimeOriginal;
    }
    gBackLight = false;
    BACKLIGHT_TurnOn();
}

void ACTION_BackLightOnDemand(void)
{
    if(gBackLight == false)
    {
        gBacklightTimeOriginal = gEeprom.BACKLIGHT_TIME;
        gEeprom.BACKLIGHT_TIME = 61;
        gBackLight = true;
    }
    else
    {
        if(gBacklightBrightnessOld == gEeprom.BACKLIGHT_MAX)
        {
            gEeprom.BACKLIGHT_TIME = 0;
        }
        else
        {
            gEeprom.BACKLIGHT_TIME = 61;
        }
    }
    
    BACKLIGHT_TurnOn();
}

void ACTION_Mute(void)
{
    // Toggle mute state
    gMute = !gMute;

    // Update the registers
    #ifdef ENABLE_FMRADIO
        BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, gMute ? 0x0A10 : 0x0A1F);
    #endif
    gEeprom.VOLUME_GAIN = gMute ? 0 : gEeprom.VOLUME_GAIN_BACKUP;
    BK4819_SetRxAudioGain();

    gUpdateStatus = true;
}

#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
void ACTION_ToggleVfoSetting(bool *setting) {
    *setting = !(*setting);
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
}

void ACTION_Power_High(void)
{
    ACTION_ToggleVfoSetting(&gPowerHigh);
}

void ACTION_Remove_Offset(void)
{
    ACTION_ToggleVfoSetting(&gRemoveOffset);
}
#endif
#endif
