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

// CW quick-settings popup state and its key handling

#include "app/cwkeyer.h"
#include "app/cwpopup.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/ui.h"

#define CW_POPUP_SHOW_10MS     200  // 2 s after the last key press

static CW_PopupKind_t s_kind;
static uint16_t       s_10ms;           // idle time left, counted on the 10 ms tick so it's exact
static uint8_t        s_pending;        // keyer mode or key input index shown, applied only on MENU
static bool           s_speed_changed;  // saved once on close instead of on every step

// Keyer mode and key input pick from a list like the menu does: nothing applies until MENU
static bool IsPending(CW_PopupKind_t kind)
{
	return kind == CW_POPUP_KEYER_MODE || kind == CW_POPUP_KEY_INPUT;
}

static void ApplyKeyerMode(void)
{
	if (s_pending == gEeprom.CW_KEYER_MODE)
		return;

	gEeprom.CW_KEYER_MODE = s_pending;
	CW_KeyerResetRuntime();  // avoid stale squeeze/tracker state bleeding across mode switch
	gRequestSaveSettings = true;
}

static void ApplyKeyInput(void)
{
	if (s_pending == gEeprom.CW_KEY_INPUT_MENU)
		return;

	const uint8_t mode = CW_KEY_INPUT_menu_to_bitmap[s_pending];

	gFlagReconfigureVfos = true;
	gRequestSaveSettings = true;

	if (CW_CheckKeyerInputs(mode)) {
		gEeprom.CW_KEY_INPUT      = mode;
		gEeprom.CW_KEY_INPUT_MENU = s_pending;
		return;
	}

	// same fallback as the menu: a stuck key leaves the radio on the PTT handkey
	gEeprom.CW_KEY_INPUT      = CW_KEY_INPUT_HANDKEY;
	gEeprom.CW_KEY_INPUT_MENU = 0;
	s_pending = 0;
	CW_Popup_Show(CW_POPUP_KEY_STUCK);
}

// A new speed is already live, so it's kept (and saved) however the popup closes. A
// pending keyer mode or key input is applied only on confirm, which is MENU; everything
// else drops it.
static void Close(bool confirm)
{
	const CW_PopupKind_t kind = s_kind;

	s_kind = CW_POPUP_NONE;
	s_10ms = 0;
	gUpdateDisplay = true;

	if (kind == CW_POPUP_SPEED && s_speed_changed) {
		s_speed_changed = false;
		gRequestSaveSettings = true;
	}

	if (!confirm)
		return;

	if (kind == CW_POPUP_KEYER_MODE)
		ApplyKeyerMode();
	else if (kind == CW_POPUP_KEY_INPUT)
		ApplyKeyInput();  // may reopen as CW_POPUP_KEY_STUCK
}

// Also restarts the idle time of the popup already showing
void CW_Popup_Show(CW_PopupKind_t kind)
{
	// switching to a different popup drops a pending choice
	if (s_kind != CW_POPUP_NONE && s_kind != kind)
		Close(false);

	s_kind = kind;
	// a keyer mode or key input waits for MENU as long as the menu itself does
	s_10ms = IsPending(kind) ? menu_timeout_500ms * 50u : CW_POPUP_SHOW_10MS;
	gUpdateDisplay = true;
}

void CW_Popup_Dismiss(CW_PopupKind_t kind)
{
	if (s_kind == kind)
		Close(false);
}

CW_PopupKind_t CW_Popup_Kind(void)
{
	return s_kind;
}

uint8_t CW_Popup_Pending(void)
{
	return s_pending;
}

void CW_Popup_Speed(void)
{
	if (s_kind == CW_POPUP_SPEED)
		Close(false);
	else if (gScreenToDisplay == DISPLAY_MAIN)
		CW_Popup_Show(CW_POPUP_SPEED);
	else
		CW_ErrorBeep();  // up/down only reach the main screen
}

static void StepSpeed(int8_t direction)
{
	const int wpm = gEeprom.CW_KEY_WPM + direction;

	if (wpm < CW_KEY_WPM_MIN || wpm > CW_KEY_WPM_MAX) {
		CW_ErrorBeep();
		return;
	}

	gEeprom.CW_KEY_WPM = wpm;
	CW_UpdateWPM();
	s_speed_changed = true;
}

// Move the pending choice one place through its menu's list (CWkmod or CWkey), wrapping
static void StepPending(int8_t direction)
{
	const uint8_t count = (s_kind == CW_POPUP_KEYER_MODE) ? CW_IAMBIC_MODE_BUG + 1
	                                                      : ARRAY_SIZE(CW_KEY_INPUT_menu_to_bitmap);

	s_pending = (uint8_t)((s_pending + count + direction) % count);
}

// Up/down as +1/-1, flipped like the menu and dial when SetNav is off
static int8_t KeyDirection(KEY_Code_t Key)
{
	const int8_t direction = (Key == KEY_UP) ? 1 : -1;

	return gEeprom.SET_NAV ? direction : -direction;
}

// The first press shows the current value, later presses step it
static void OpenOrStep(CW_PopupKind_t kind, uint8_t current)
{
	if (s_kind == kind) {
		StepPending(1);
	}
	else if (gScreenToDisplay != DISPLAY_MAIN) {
		CW_ErrorBeep();  // nowhere to show the pending choice
		return;
	}
	else {
		s_pending = current;
	}

	CW_Popup_Show(kind);
}

void CW_Popup_StepKeyerMode(void)
{
	OpenOrStep(CW_POPUP_KEYER_MODE, gEeprom.CW_KEYER_MODE);
}

void CW_Popup_StepKeyInput(void)
{
	OpenOrStep(CW_POPUP_KEY_INPUT, gEeprom.CW_KEY_INPUT_MENU);
}

bool CW_Popup_ProcessKey(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	// PTT keeps keying; side keys go to their actions, see CW_Popup_OnAction
	if (s_kind == CW_POPUP_NONE || Key == KEY_PTT || Key == KEY_SIDE1 || Key == KEY_SIDE2)
		return false;

	if (Key == KEY_MENU || Key == KEY_EXIT) {
		// close on release so the release doesn't reach the main screen. Releasing a long
		// press does nothing, so an M long action that opened the popup leaves it up.
		if (!bKeyPressed && !bKeyHeld)
			Close(Key == KEY_MENU);
		return true;
	}

	if ((s_kind == CW_POPUP_SPEED || IsPending(s_kind)) && (Key == KEY_UP || Key == KEY_DOWN)) {
		if (bKeyPressed) {  // first press and every auto-repeat while held
			if (s_kind == CW_POPUP_SPEED)
				StepSpeed(KeyDirection(Key));
			else
				StepPending(KeyDirection(Key));
			CW_Popup_Show(s_kind);
		}
		return true;
	}

	// any other key closes the popup, dropping a pending choice, then does its usual job
	if (bKeyPressed && !bKeyHeld)
		Close(false);

	return false;
}

void CW_Popup_OnAction(uint8_t action)
{
	// the popup's own actions step, switch or close it themselves
	if (s_kind != CW_POPUP_NONE &&
	    action != ACTION_OPT_CW_KEYER_MODE &&
	    action != ACTION_OPT_CW_SPEED &&
	    action != ACTION_OPT_CW_KEY_INPUT)
		Close(false);
}

void CW_Popup_OnKeying(void)
{
	// keying keeps a new speed and drops a pending keyer mode or key input, since keying
	// means the current ones are in use (and a pressed key would fail the input's check)
	if (s_kind != CW_POPUP_NONE)
		Close(false);
}

void CW_Popup_Tick10ms(void)
{
	if (s_kind == CW_POPUP_NONE)
		return;

	// PTT in any mode closes it like keying does
	if (gCurrentFunction == FUNCTION_TRANSMIT || --s_10ms == 0)
		Close(false);
}
