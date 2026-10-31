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
#ifdef ENABLE_FMRADIO_EMBEDDED
    #include "app/fm.h"
#endif
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    #include "apps/app_overlay.h"
#endif
#include "app/scanner.h"
#include "audio.h"
#ifdef ENABLE_FMRADIO_EMBEDDED
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
#if defined(ENABLE_FEAT_F4HWN_FOXHUNT) || defined(ENABLE_FEAT_F4HWN_BEACON) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
    #include "app/foxhunt.h"
#endif
#ifdef ENABLE_FEAT_F4HWN_ACTION_PICKER
    #include "ui/menu.h"
#endif

#ifdef ENABLE_CW_MODULATOR
#include "app/cwapp.h"
#include "app/cwkeyer.h"
#include "app/cwpopup.h"

    #ifdef ENABLE_CODE_PRACTICE
        #include "app/cpo.h"
    #endif
#endif

#if defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS) && !defined(ENABLE_FEAT_F4HWN_BEAM)
static void ACTION_Beam(void);
#endif

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
static void ACTION_ToneCall(void);
#endif

#if defined(ENABLE_FMRADIO_EMBEDDED)
static void ACTION_Scan_FM(bool bRestart);
#endif

#ifdef ENABLE_TX1750
static void ACTION_1750(void);
#endif

inline static void ACTION_ScanRestart() { ACTION_Scan(true); };

#ifdef ENABLE_CW_MODULATOR
static void ACTION_PlayCWMsg1(void);
static void ACTION_PlayCWMsg2(void);
static void ACTION_PlayCWMsg3(void);
static void ACTION_PlayCWMsg4(void);
static void ACTION_RepeatCWMsg1(void);
static void ACTION_RepeatCWMsg2(void);
static void ACTION_RepeatCWMsg3(void);
static void ACTION_RepeatCWMsg4(void);
static void ACTION_CWProperRoger(void);
static void ACTION_CWBreakIn(void);
#ifdef ENABLE_CODE_PRACTICE
static void ACTION_CPO(void);
#endif
#endif

void (*const action_opt_table[ACTION_OPT_LEN])(void) = {
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
#endif

#ifdef ENABLE_VOX
    [ACTION_OPT_VOX] = &ACTION_Vox,
#endif

#ifdef ENABLE_FMRADIO
    [ACTION_OPT_FM] = &ACTION_FM,
#endif

#ifdef ENABLE_TX1750
    [ACTION_OPT_1750] = &ACTION_1750,
#endif

#ifdef ENABLE_CW_MODULATOR
    [ACTION_OPT_PLAY_CWMSG1] = &ACTION_PlayCWMsg1,
	[ACTION_OPT_PLAY_CWMSG2] = &ACTION_PlayCWMsg2,
	[ACTION_OPT_PLAY_CWMSG3] = &ACTION_PlayCWMsg3,
	[ACTION_OPT_PLAY_CWMSG4] = &ACTION_PlayCWMsg4,
	[ACTION_OPT_REPEAT_CWMSG1] = &ACTION_RepeatCWMsg1,
	[ACTION_OPT_REPEAT_CWMSG2] = &ACTION_RepeatCWMsg2,
	[ACTION_OPT_REPEAT_CWMSG3] = &ACTION_RepeatCWMsg3,
	[ACTION_OPT_REPEAT_CWMSG4] = &ACTION_RepeatCWMsg4,
    #ifdef ENABLE_CODE_PRACTICE
    [ACTION_OPT_CPO] = &ACTION_CPO,
	#endif
	[ACTION_OPT_CW_KEYER_MODE] = &CW_Popup_StepKeyerMode,
	[ACTION_OPT_CW_PROPER_ROGER] = &ACTION_CWProperRoger,
	[ACTION_OPT_CW_SPEED] = &CW_Popup_Speed,
	[ACTION_OPT_CW_KEY_INPUT] = &CW_Popup_StepKeyInput,
	[ACTION_OPT_CW_BREAK_IN] = &ACTION_CWBreakIn,
#endif

#ifdef ENABLE_FEAT_F4HWN
    [ACTION_OPT_RXMODE] = &ACTION_RxMode,
    [ACTION_OPT_MAINONLY] = &ACTION_MainOnly,
    [ACTION_OPT_PTT] = &ACTION_Ptt,
    [ACTION_OPT_WN] = &ACTION_Wn,
    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
        [ACTION_OPT_MUTE] = &ACTION_Mute,
    //#else
    //    [ACTION_OPT_MUTE] = &FUNCTION_NOP,
    //#endif
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        [ACTION_OPT_RXA] = &ACTION_RxA,
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        [ACTION_OPT_POWER_HIGH] = &ACTION_Power_High,
        [ACTION_OPT_REMOVE_OFFSET] = &ACTION_Remove_Offset,
    #endif
#endif
#if defined(ENABLE_FEAT_F4HWN_BEAM) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
    [ACTION_OPT_BEAM] = &ACTION_Beam,
#endif
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    [ACTION_OPT_RXTX_LOG] = &ACTION_RxTxLog,
#endif
#if defined(ENABLE_FEAT_F4HWN_FOXHUNT) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
    [ACTION_OPT_FOXHUNT] = &ACTION_FoxHunt,
#endif
#if defined(ENABLE_FEAT_F4HWN_BEACON) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
    [ACTION_OPT_BEACON] = &ACTION_Beacon,
#endif
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    [ACTION_OPT_TONECALL] = &ACTION_ToneCall,
#endif
};

static_assert(ARRAY_SIZE(action_opt_table) == ACTION_OPT_LEN);
static_assert(ACTION_OPT_RXTX_LOG == 18);
static_assert(ACTION_OPT_BEAM == 19);
static_assert(ACTION_OPT_POWER_HIGH == 20);
static_assert(ACTION_OPT_REMOVE_OFFSET == 21);
static_assert(ACTION_OPT_FOXHUNT == 22);
static_assert(ACTION_OPT_BEACON == 23);
static_assert(ACTION_OPT_TONECALL == 24);
static_assert(ACTION_OPT_CW_KEY_INPUT == 32);
static_assert(ACTION_OPT_CW_PROPER_ROGER == 45);

uint8_t ACTION_Step(uint8_t action, int8_t direction, uint8_t first)
{
    // Wraps within [first, ACTION_OPT_LEN) and steps over the IDs that are not
    // actions (see ACTION_OPT_IS_DEFINED)
    do {
        if (direction < 0)
            action = (action <= first) ? ACTION_OPT_LEN - 1 : action - 1;
        else
            action = (action + 1 >= ACTION_OPT_LEN) ? first : action + 1;
    } while (!ACTION_OPT_IS_DEFINED(action));
    return action;
}

bool ACTION_IsAvailable(uint8_t action)
{
    if (action >= ACTION_OPT_LEN || action_opt_table[action] == NULL)
        return false;

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    /* An action that opens an overlay app is available once an app holds
     * its shortcut: the switch picks the shortcut ID. */
    uint8_t shortcut;
    switch (action) {
#ifdef ENABLE_FMRADIO
        case ACTION_OPT_FM:       shortcut = APP_SHORTCUT_FM;       break;
#endif
#ifndef ENABLE_FEAT_F4HWN_FOXHUNT
        case ACTION_OPT_FOXHUNT:  shortcut = APP_SHORTCUT_FOXHUNT;  break;
#endif
#ifndef ENABLE_FEAT_F4HWN_BEACON
        case ACTION_OPT_BEACON:   shortcut = APP_SHORTCUT_BEACON;   break;
#endif
#ifndef ENABLE_FEAT_F4HWN_BEAM
        case ACTION_OPT_BEAM:     shortcut = APP_SHORTCUT_BEAM;     break;
#endif
        case ACTION_OPT_TONECALL: shortcut = APP_SHORTCUT_TONECALL; break;
        default:                  return true;
    }
    return APP_OverlayShortcutInstalled(shortcut);
#endif

    return true;
}

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
#ifdef ENABLE_CW_MODULATOR
        if (gRxVfo->Modulation == MODULATION_CW || gRxVfo->Modulation == MODULATION_USB)
            gMonitor = true;
#endif
        RADIO_SetupRegisters(true);
#ifdef ENABLE_CW_MODULATOR
        if (gMonitor)
            BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
#endif
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

#ifdef ENABLE_CW_MODULATOR
    // Silence the physical speaker amp BEFORE touching BK4819's AF mode
    // below, so any REG_47 mux transient settles with the amp already off.
    if (gRxVfo->Modulation == MODULATION_CW || gRxVfo->Modulation == MODULATION_USB) {
        AUDIO_AudioPathOff();
        gEnableSpeaker = false;
    }
#endif

    // This close-squelch branch calls RADIO_SetupRegisters()
    // directly instead of going through APP_StartListening(). 
    // AF deliberately stays in the BASEBAND2 mode afterward.
    RADIO_SetModulation(gRxVfo->Modulation);

#ifdef ENABLE_FMRADIO_EMBEDDED
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

#ifdef ENABLE_FMRADIO_EMBEDDED
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

#ifdef ENABLE_CW_MODULATOR
	// Arm/dearm keyer ownership immediately so a very quick PTT press after
	// switching to CW cannot beat deferred reconfigure/save paths.
	CW_KeyerReconfigure(gTxVfo->Modulation == MODULATION_CW);

	// CW/USB default to monitor mode (open squelch); other modes start squelched.
	gMonitor = (gTxVfo->Modulation == MODULATION_CW ||
	            gTxVfo->Modulation == MODULATION_USB);

	// Reconfigure radio path now (not only on later key release/save handling).
	gFlagReconfigureVfos = true;
#endif


}

// CW and USB have a single filter, so there's nothing to switch
static void RefuseFilterSwitch(void)
{
#ifdef ENABLE_CW_MODULATOR
	CW_ErrorBeep();  // a sidetone dit in CW, the double beep elsewhere
#else
	gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
#endif
}

void ACTION_SwitchFilter(void)
{
	if (!RADIO_HasFilterChoice(gTxVfo)) {
		RefuseFilterSwitch();
		return;
	}

	// One bit, so this is a straight toggle. What wide and narrow resolve to
	// depends on the modulation - see RADIO_ResolveFilter().
	gTxVfo->CHANNEL_BANDWIDTH = (gTxVfo->CHANNEL_BANDWIDTH == BANDWIDTH_WIDE)
		? BANDWIDTH_NARROW : BANDWIDTH_WIDE;
	gRequestSaveChannel  = 1;
	gFlagReconfigureVfos = true;
}


#ifdef ENABLE_FMRADIO_EMBEDDED
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
#if defined(ENABLE_FEAT_F4HWN_BEAM) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
        case ACTION_OPT_BEAM:
#endif
#if defined(ENABLE_FEAT_F4HWN_FOXHUNT) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
        case ACTION_OPT_FOXHUNT:
#endif
#if defined(ENABLE_FEAT_F4HWN_BEACON) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
        case ACTION_OPT_BEACON:
#endif
            return true;

        default:
            return false;
    }
}
#endif

static void ACTION_Execute(uint8_t action)
{
    if (!ACTION_IsAvailable(action)) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }

#ifdef ENABLE_FMRADIO_EMBEDDED
    if (gFmRadioMode && ACTION_IsBlockedInFM(action)) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }
#endif

    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    action_opt_table[action]();
}

#ifdef ENABLE_FEAT_F4HWN_ACTION_PICKER
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
                *selection = ACTION_Step(*selection, key == KEY_UP ? -1 : 1, 1);

                gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
                gUpdateDisplay = true;
            }
            return true;

        case KEY_MENU:
            if (!isPressed && !isHeld) {
                const uint8_t action = *selection;
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
#ifdef ENABLE_CW_MODULATOR
    // paranoid block: keyer owns SIDE1 (we shouldn't ever get this far)
    if (Key == KEY_SIDE1 && gCW_KeyerUsingSD1) {
        return;
    }
#endif

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
#ifdef ENABLE_CW_MODULATOR
    CW_Popup_OnAction(func);
#endif

    ACTION_Execute(func);
}

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
/* Open the overlay app holding a shortcut; double beep when none is installed. */
static void ACTION_LaunchApp(uint8_t shortcut)
{
#ifdef ENABLE_CW_MODULATOR
    CW_OverlayEnter();
#endif
    const uint8_t rc = APP_LaunchOverlayShortcut(shortcut);
#ifdef ENABLE_CW_MODULATOR
    CW_OverlayExit();
#endif
    if (rc != APP_OK && rc != APP_CHAIN_ABORTED)
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
}
#endif

#if defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS) && !defined(ENABLE_FEAT_F4HWN_BEAM)
static void ACTION_Beam(void)
{
    ACTION_LaunchApp(APP_SHORTCUT_BEAM);
}
#endif

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
static void ACTION_ToneCall(void)
{
    ACTION_LaunchApp(APP_SHORTCUT_TONECALL);
}
#endif

#if defined(ENABLE_FEAT_F4HWN_FOXHUNT) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
void ACTION_FoxHunt(void)
{
#ifdef ENABLE_FEAT_F4HWN_FOXHUNT
    APP_RunFoxHunt();
    GUI_SelectNextDisplay(DISPLAY_MAIN);
#else
    ACTION_LaunchApp(APP_SHORTCUT_FOXHUNT);
#endif
}
#endif

#if defined(ENABLE_FEAT_F4HWN_BEACON) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
void ACTION_Beacon(void)
{
#ifdef ENABLE_FEAT_F4HWN_BEACON
    APP_RunBeacon();
    GUI_SelectNextDisplay(DISPLAY_MAIN);
#else
    ACTION_LaunchApp(APP_SHORTCUT_BEACON);
#endif
}
#endif


#ifdef ENABLE_FMRADIO
void ACTION_FM(void)
{
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    ACTION_LaunchApp(APP_SHORTCUT_FM);
    return;
#else
#ifdef ENABLE_CW_MODULATOR
    // Only an in-progress transmission blocks the toggle. FUNCTION_MONITOR used
    // to block it too, but ENABLE_CW_MODULATOR forces gMonitor (and therefore
    // FUNCTION_MONITOR) on for CW and USB, which made this a silent no-op in
    // exactly the modes this firmware spends most of its time in. Monitor is
    // torn down below anyway: gMonitor is cleared and RADIO_SetupRegisters(true)
    // puts us back in FUNCTION_FOREGROUND before FM_Start().
    if (gCurrentFunction != FUNCTION_TRANSMIT)
#else
    if (gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR)
#endif
    {
        gInputBoxIndex = 0;

        if (gFmRadioMode) {
            FM_TurnOff();
            gFlagReconfigureVfos  = true;
            gRequestDisplayScreen = DISPLAY_MAIN;

#ifdef ENABLE_CW_MODULATOR
            // Re-arm the keyer if we're returning to a CW channel.
            CW_KeyerReconfigure(gTxVfo->Modulation == MODULATION_CW);
#endif

#ifdef ENABLE_VOX
            gVoxResumeCountdown = 80;
#endif
            return;
        }

        // Do not start broadcast FM while a VFO reception is already active.
        // Keeping this check after the block above ensures EXIT can still
        // turn FM off if the UI ever reaches DISPLAY_MAIN with FM mode active.
#ifdef ENABLE_CW_MODULATOR
        // CW and USB sit in FUNCTION_MONITOR all the time (see above), so only
        // a squelch-open reception counts here.
        if (gCurrentFunction == FUNCTION_INCOMING || gCurrentFunction == FUNCTION_RECEIVE) {
#else
        if (FUNCTION_IsRx()) {
#endif
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            return;
        }

        gMonitor = false;

        if (gScanStateDir != SCAN_OFF) {
            // Stop the channel/frequency scan before switching to the FM radio.
            gScanKeepResult = false;
            CHFRSCANNER_Stop();
        }

        RADIO_SelectVfos();

        // Both of these have to happen AFTER RADIO_SelectVfos(), which re-asserts
        // gMonitor and re-arms the keyer for CW/USB channels - doing them first
        // just gets the work undone.
        gMonitor = false;
#ifdef ENABLE_CW_MODULATOR
        // Park the keyer before handing the radio to the BK1080. CW_AppUpdate()
        // keys on gTxVfo->Modulation, not on gFmRadioMode, so an armed keyer
        // would otherwise still be polling paddles - and still holding PA10 -
        // while the user is listening to broadcast FM.
        CW_KeyerReconfigure(false);
#endif

        RADIO_SetupRegisters(true);

        FM_Start();

        gRequestDisplayScreen = DISPLAY_FM;
    }
#endif  /* !ENABLE_FEAT_F4HWN_OVERLAY_APPS */
}

#ifdef ENABLE_FMRADIO_EMBEDDED
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

#endif

#ifdef ENABLE_CW_MODULATOR

#ifdef ENABLE_CODE_PRACTICE

static void ACTION_CPO(void)
{
    CPO_Enter();
}

#endif

static void ACTION_PlayCWMsg1(void)
{
	CW_StartMacroPlayback(0, false);
}

static void ACTION_PlayCWMsg2(void)
{
	CW_StartMacroPlayback(1, false);
}

static void ACTION_PlayCWMsg3(void)
{
	CW_StartMacroPlayback(2, false);
}

static void ACTION_PlayCWMsg4(void)
{
	CW_StartMacroPlayback(3, false);
}

static void ACTION_RepeatCWMsg1(void)
{
	CW_StartMacroPlayback(0, true);
}

static void ACTION_RepeatCWMsg2(void)
{
	CW_StartMacroPlayback(1, true);
}

static void ACTION_RepeatCWMsg3(void)
{
	CW_StartMacroPlayback(2, true);
}

static void ACTION_RepeatCWMsg4(void)
{
	CW_StartMacroPlayback(3, true);
}

// Same toggle as F+7 in CW; the status bar icon shows the new state
static void ACTION_CWBreakIn(void)
{
	gEeprom.CW_BREAKIN_ENABLE = !gEeprom.CW_BREAKIN_ENABLE;
	gRequestSaveSettings = true;
	gUpdateStatus = true;
}

static void ACTION_CWProperRoger(void)
{
	// playback only drives the keyer in CW mode, same rule as playing from the menu
	if (gTxVfo->Modulation != MODULATION_CW) {
		gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
		return;
	}

	CW_StartProperRoger();
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

#ifdef ENABLE_FEAT_F4HWN_FULL_WATCH
uint8_t ACTION_GetRxMode(void)
{
    if (gEeprom.DUAL_WATCH == DUAL_WATCH_FULL)
        return gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF ? 4 : 5;

    return (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) +
           (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF) * 2;
}

void ACTION_SetRxMode(uint8_t mode)
{
    const uint8_t selected = gEeprom.TX_VFO + 1u;
    bool crossBand;
    if (mode >= 4)
    {
        gEeprom.DUAL_WATCH = DUAL_WATCH_FULL;
        crossBand = mode == 5;
    }
    else
    {
        gEeprom.DUAL_WATCH = selected * (mode & 1);
        crossBand = (mode & 2) != 0;
    }
    gEeprom.CROSS_BAND_RX_TX = crossBand ? selected : CROSS_BAND_OFF;
}
#endif

void ACTION_RxMode(void)
{
#ifdef ENABLE_FEAT_F4HWN_FULL_WATCH
    uint8_t mode = ACTION_GetRxMode() + 1;
    ACTION_SetRxMode(mode < 6 ? mode : 0);
#else
    static bool cycle = 0;

    if (cycle) {
        gEeprom.CROSS_BAND_RX_TX = !gEeprom.CROSS_BAND_RX_TX;
    } else {
        gEeprom.DUAL_WATCH = !gEeprom.DUAL_WATCH;
    }

    cycle = !cycle;
#endif

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

    if (!RADIO_HasFilterChoice(pVfo)) {
        RefuseFilterSwitch();
        return;
    }

    pVfo->CHANNEL_BANDWIDTH = !pVfo->CHANNEL_BANDWIDTH;

    // These read the bit through RADIO_ResolveFilter()
    if (pVfo->Modulation == MODULATION_AM || pVfo->Modulation == MODULATION_USB
#ifdef ENABLE_CW_MODULATOR
        || pVfo->Modulation == MODULATION_CW
#endif
    )
    {
        BK4819_SetFilterBandwidth(RADIO_ResolveFilter(pVfo), true);
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
    #ifdef ENABLE_FMRADIO_EMBEDDED
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
