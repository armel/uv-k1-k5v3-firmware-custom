/* Copyright 2026 NR7Y
 * https://github.com/briand
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

// CW application-level update loop and end-of-transmission handling
// Extracted from app.c to give CW its own independent PTT / EOT path.

#include <stdint.h>
#include <stdbool.h>

#include "app/cwapp.h"
#include "app/cwkeyer.h"
#include "app/cwguard.h"
#include "app/cwmacro.h"
#include "app/cwpopup.h"
#include "app/cwrit.h"
#include "app/action.h"
#include "app/app.h"
#include "app/menu.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "py32f071_ll_gpio.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "driver/backlight.h"
#include "driver/system.h"
#include "driver/millis.h"
#ifdef ENABLE_CODE_PRACTICE
#include "app/cpo.h"
#endif
#ifdef ENABLE_FLASHLIGHT
#include "app/flashlight.h"
#endif

// Local-only sidetone (no RF): ALAM and the sidetone's DAC gain keep the audio path
// through the gaps, and RX gets it back once the key has been up for the hang time,
// like break-in's suspend. Giving it back after every element swung the DAC gain
// between the sidetone's and RX's on each edge, which popped and let RX in between
// elements.
static bool     s_local_sidetone_held;  // ALAM and the sidetone gain own the audio path
static bool     s_local_keyed;          // tone on
static uint32_t s_local_key_up_ms;      // when the tone last went off

static void LocalSidetoneRelease(void)
{
	s_local_sidetone_held = false;
	RADIO_SetModulation(gRxVfo->Modulation);  // also puts RX's DAC gain (0xF) back
}

bool CW_LocalSidetoneHeld(void)
{
	return s_local_sidetone_held;
}

// ---------------------------------------------------------------------------
// CW_EndTxNow  –  end CW transmission immediately and return to monitor
// ---------------------------------------------------------------------------
void CW_EndTxNow(void)
{
    // Clear CW state when ending transmission entirely
    gCW_State = CW_INACTIVE;

	// Call the common end-of-transmission (sends tail, resets TX regs). Its
	// RADIO_SetupRegisters also puts RX's DAC gain back over the sidetone's.
	APP_EndTransmission();

	// Go straight to FOREGROUND
	FUNCTION_Select(FUNCTION_FOREGROUND);

	gFlagEndTransmission = false;

#ifdef ENABLE_VOX
	gVOX_NoiseDetected = false;
#endif

	RADIO_SetVfoState(VFO_STATE_NORMAL);  // only variables
    RADIO_SelectVfos();                   // only variables
    if (gMonitor)
        APP_StartListening(FUNCTION_MONITOR);
}

void CW_OverlayEnter(void)
{
	CW_StopPlayback();
	if (gCW_State != CW_INACTIVE) {
		gPttIsPressed = false;
		CW_EndTxNow();
	}
	if (s_local_sidetone_held)
		LocalSidetoneRelease();
	CW_KeyerReconfigure(false);
}

void CW_OverlayExit(void)
{
	RADIO_SelectVfos();
	RADIO_SetupRegisters(true);

	// RADIO_SetupRegisters mutes AF (or closes the audio path) and drops to
	// FUNCTION_FOREGROUND, so nothing would reopen it and we'd show MONI with
	// no audio. ACTION_Monitor reopens it and relights the green RX LED.
	if (gMonitor)
		ACTION_Monitor();
}

// ---------------------------------------------------------------------------
// CW_AppUpdate  –  called from main.c at approx 1ms cadence (sometimes a little longer)
// ---------------------------------------------------------------------------
void CW_AppUpdate(void)
{
	if (!(gTxVfo->Modulation == MODULATION_CW
#ifdef ENABLE_CODE_PRACTICE
		|| gCW_CpoActive
#endif
	))
	{
		// Not in CW mode – paranoid check to end CW TX
		if (gCW_State != CW_INACTIVE)
		{
			CW_EndTxNow();
		}
		if (s_local_sidetone_held)
		{
			LocalSidetoneRelease();
		}
        return;  // not in CW mode, nothing else to do
	}

	// ---- poll the keyer / playback engine for the next action ----
	CW_Action_t action;
	const bool fromPlayback = gCW_PlaybackActive;
	const bool cue          = CW_PlaybackIsCue();  // before polling: a paddle can stop it mid-element
	if (fromPlayback)
		action = CW_PlaybackHandleState();
	else
		action = CW_HandleState();

	// ---- transmit timeout: a stuck key or paddle drops TX and locks keying out ----
	CW_GuardEvent_t guard;
	action = CW_Guard_Filter(action, millis(), &guard);
	if (guard == CW_GUARD_TRIPPED) {
		CW_StopPlayback();
		CW_Popup_Show(CW_POPUP_TX_TIMEOUT);
	}
	else if (guard == CW_GUARD_RELEASED) {
		CW_Popup_Dismiss(CW_POPUP_TX_TIMEOUT);
	}
	else if (guard == CW_GUARD_LOCKED && CW_Popup_Kind() == CW_POPUP_NONE) {
		CW_Popup_Show(CW_POPUP_TX_TIMEOUT);  // keep saying so until the key is released
	}

	// keying closes a settings popup, dropping an unconfirmed choice, or RIT/XIT adjust
	// (break-in off never enters TX). An error-beep dit answers a key press in them, so
	// it leaves them up.
	if (action == CW_ACTION_CARRIER_ON && !cue) {
		CW_Popup_OnKeying();
		CW_RIT_OnKeying();
	}

	// Playback started timing this element before the TX/sidetone setup below ran;
	// whatever that setup takes is added back so the element isn't clipped. Paddle
	// keying is left alone so the keyer stays in step with the operator's squeezes.
	const bool     playbackKeyed = fromPlayback && action == CW_ACTION_CARRIER_ON;
	const uint32_t setupStartMs  = millis();

	// Test: a proper roger lights the LED amber (red and green together) while it keys.
	// Worked out here because the local-only path below clears the action.
	const bool rogerLit = fromPlayback && CW_PlaybackIsProperRoger()
		&& (action == CW_ACTION_CARRIER_ON || action == CW_ACTION_CARRIER_HOLD_ON);

	// ---- local-only sidetone path (no RF) ----
	// Used when recording a macro, reading ADC, breakin disabled, code practice, or an error-beep dit
	if (gCW_Recording || !gEeprom.CW_BREAKIN_ENABLE || cue
#ifdef ENABLE_CODE_PRACTICE
		|| gCW_CpoActive
#endif
		) {
		switch (action)
		{
			case CW_ACTION_CARRIER_ON:
				if (gCW_State == CW_INACTIVE && !AUDIO_IsAudioPathOn()) {
					AUDIO_AudioPathOn();
					SYSTEM_DelayMs(10);
				}
				BACKLIGHT_TurnOn();
				// gain before ALAM, so the tone path never opens at RX's DAC gain
				CW_ApplySidetoneGain();
				BK4819_SetAF(BK4819_AF_ALAM);
				BK4819_SetScrambleFrequencyControlWord(cue ? CW_ERROR_TONE_HZ : gEeprom.CW_TONE_FREQUENCY * 10);
				#ifdef ENABLE_FLASHLIGHT
				if (gCW_FlashlightSending) {
					GPIO_SetOutputPin(GPIO_PIN_FLASHLIGHT);
				}
				#endif
				s_local_sidetone_held = true;
				s_local_keyed = true;
				gCW_TxDisplayHoldoff_10ms = 200;
			break;

			case CW_ACTION_NONE:
				// same as the RF path's suspend on NONE, e.g. a paddle stopping playback mid-element
				if (!s_local_keyed)
					break;
				// fall through
			case CW_ACTION_CARRIER_OFF:
				// ALAM and the sidetone gain stay until the hang time passes (CPO keeps them)
				BK4819_SetScrambleFrequencyControlWord(0);
				#ifdef ENABLE_FLASHLIGHT
				if (gCW_FlashlightSending) {
					GPIO_ResetOutputPin(GPIO_PIN_FLASHLIGHT);
				}
				#endif
				s_local_keyed = false;
				s_local_key_up_ms = millis();
				gCW_TxDisplayHoldoff_10ms = 200;
			break;

			default:
			break;
		}
		// don't let RF happen
		action = CW_ACTION_NONE;
	}

	// ---- RF transmit path ----
	switch (action)
	{
		case CW_ACTION_CARRIER_ON:
			gTxTimerCountdown_500ms = 0;
			gCW_TxDisplayHoldoff_10ms = 200;
			gPttIsPressed = true;

			if (gCW_State == CW_INACTIVE)
			{
				if (!AUDIO_IsAudioPathOn()) {
					AUDIO_AudioPathOn();
					SYSTEM_DelayMs(20);
				}
				RADIO_PrepareTX();
				// refused (TX DISABLE, BAT LOW, ...): nothing will clear PTT later, and
				// a held PTT stops the VFO state from timing back to the frequency
				if (gCW_State == CW_INACTIVE)
					gPttIsPressed = false;
			}
			else if (gCW_State == CW_SUSPENDED) {
				RADIO_CW_BeginResume();
                gCW_SuspendCounter_1ms = 0;
			}
			// if already CW_TRANSMITTING: no-op
		break;

		case CW_ACTION_CARRIER_OFF:
			// only suspend once, from active TX
			if (gCW_State == CW_TRANSMITTING) {
				RADIO_CW_Suspend();
				gCW_SuspendCounter_1ms = millis();
			}
			gCW_TxDisplayHoldoff_10ms = 200;
		break;

		case CW_ACTION_CARRIER_HOLD_ON:
			gPttIsPressed = true;
			gTxTimerCountdown_500ms = 0;
			gCW_TxDisplayHoldoff_10ms = 200;

			// if hold arrives while suspended (shouldn't happen), resume once
			if (gCW_State == CW_SUSPENDED) {
				RADIO_CW_BeginResume();
			}
			gCW_SuspendCounter_1ms = millis();
		break;

		case CW_ACTION_NONE:
			if(gCW_State == CW_TRANSMITTING) {
				// if we've been transmitting but now have no carrier, suspend
				RADIO_CW_Suspend();
				gCW_SuspendCounter_1ms = millis();
			}
		default:
		break;
	}

	if (playbackKeyed)
		CW_PlaybackExtendElement(millis_since(setupStartMs));

	// After the RF path, which turns green off when TX starts and red off on suspend.
	// Only on a change, so a held element doesn't rewrite the register every poll.
	static bool s_rogerLit;
	if (rogerLit != s_rogerLit) {
		s_rogerLit = rogerLit;
		BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, rogerLit);
		BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, rogerLit);
	}

	// a timeout drops TX now instead of waiting out the hang time
	if (guard == CW_GUARD_TRIPPED && gCW_State != CW_INACTIVE) {
		gPttIsPressed = false;
		CW_EndTxNow();
	}

	// ---- local sidetone hang timeout → back to RX ----
	if (s_local_sidetone_held)
	{
		if (gCW_State != CW_INACTIVE
#ifdef ENABLE_CODE_PRACTICE
			|| gCW_CpoActive
#endif
			) {
			s_local_sidetone_held = false;  // TX or CPO owns the audio path now
		}
		else if (!s_local_keyed && millis_since(s_local_key_up_ms) >= gEeprom.CW_HANG_10MS * 10u) {
			LocalSidetoneRelease();
		}
	}

	// ---- suspend timeout → end TX ----
	if (gCW_State == CW_SUSPENDED)
	{
		if (millis_since(gCW_SuspendCounter_1ms) >= gEeprom.CW_HANG_10MS * 10u) {
            gCW_SuspendCounter_1ms = 0;
            gCW_TxDisplayHoldoff_10ms = 200;
            gPttIsPressed = false;
			CW_EndTxNow();
		}
	}
}

// Perform CW-derived 10ms timeslice work previously located in app.c
void CW_TimeSlice10ms(void)
{
	// Handle CW macro recording updates
	if (gCW_Recording && gCW_RecordNewChar) {
		gCW_RecordNewChar = false;
		gUpdateDisplay = true;
	}

	// Update playback indicator
	CW_PlaybackIndicatorDeadline();

	// Decrement TX display holdoff timer (runs every 10ms tick regardless of TX state)
	if (gCW_TxDisplayHoldoff_10ms > 0) {
		if (--gCW_TxDisplayHoldoff_10ms == 0)
			gUpdateDisplay = true;  // Trigger screen refresh to switch away from CW display
	}

	// idle timeouts, here rather than on the 500 ms tick so a key press just before
	// that tick doesn't cut them short
	CW_Popup_Tick10ms();
	CW_RIT_Tick10ms();
}
