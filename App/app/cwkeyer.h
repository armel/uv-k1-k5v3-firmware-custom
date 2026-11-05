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

// CW Iambic Keyer interface
// a small FSM to handle CW dit/dah timing and iambic A/B semantics

#ifndef APP_CWKEYER_H
#define APP_CWKEYER_H

#include <stdint.h>
#include <stdbool.h>

// Actions emitted by the keyer FSM; mutually exclusive carrier state transitions
typedef enum {
    CW_ACTION_NONE        = 0,  // no carrier active (idle, inter-element gap)
    CW_ACTION_CARRIER_ON  = 1,  // transition carrier from off to on (start element)
    CW_ACTION_CARRIER_OFF = 2,  // transition carrier from on to off (end element)
    CW_ACTION_CARRIER_HOLD_ON = 3, // carrier is on and should remain on (during element)
} CW_Action_t;

// Periodic handler: drive CW state machine and return actions to apply
CW_Action_t CW_HandleState(void);

// Set CW keyer speed from EEPROM; updates internal timing parameters
void CW_UpdateWPM();

// Set the BK4819 Tone1 tuning gain and AF DAC gain for the CWvol level (0=off, 1-15).
// The DAC gain also scales RX audio, so RX needs its own back before it's heard again
// (RADIO_SetModulation or RADIO_SetupRegisters).
void CW_ApplySidetoneGain(void);

// Reset keyer state, to transition modes like CPO safely
void CW_KeyerResetRuntime(void);

// Optional: request reconfigure at next safe boundary (applied at gap or idle)
void CW_KeyerReconfigure(bool enable);

// Check keyer inputs before mode change: returns true if inputs valid, false to abort
// new_mode: The CW_KeyInputType_t mode to validate
bool CW_CheckKeyerInputs(uint8_t new_mode);

// Macro playback API
// Start playback of macro (0-3). Sets playback active flag and loads macro.
// If repeat is true, playback will restart after CW_MESSAGE_REPEAT_DELAY expires.
void CW_StartMacroPlayback(uint8_t macroIndex, bool repeat);

// Send a "proper roger": R with the dah held for CWrgr dits, default 9 (di-daaaaaaaaah-dit)
void CW_StartProperRoger(void);

// True while playback is sending a proper roger
bool CW_PlaybackIsProperRoger(void);

// Play arbitrary text (macro character set, ' ' = word gap). With show false the
// characters stay off the TX display line, e.g. a callsign the operator must copy.
// eff_wpm below the keyer speed stretches the gaps between characters and words
// (Farnsworth) so the text averages eff_wpm; 0 keeps the keyer's own spacing.
void CW_StartTextPlayback(const char *text, bool show, uint8_t eff_wpm);

// The error double beep for a refused key press (a setting at its limit, a key with no
// job), as a single dit on the sidetone in CW, where beeps can't sound because the
// BK4819 runs in baseband. The dit has its own pitch and speed, the error beep's 500 Hz
// at 20 WPM, so it doesn't sound like the operator's sidetone. It never transmits, stays
// off the TX display line and doesn't count as keying. Outside CW, or while keying,
// playback, a repeating macro or a recording is under way, it's the usual double beep.
#define CW_ERROR_TONE_HZ   500
void CW_ErrorBeep(void);

// True while playback is sending CW_ErrorBeep's dit
bool CW_PlaybackIsCue(void);

// True when the paddle/bug keyer has no character or word in progress, i.e. the
// operator has paused for at least a word gap, and no straight key is held
bool CW_KeyerIsIdle(void);

// The playback element just keyed lost setup_ms to TX/sidetone setup before it was
// heard; add that back so it goes out full length. Playback only: stretching the
// paddle keyer's timeline the same way puts it out of step with the operator's
// squeezes, so hand keying keeps its first-element clipping.
void CW_PlaybackExtendElement(uint32_t setup_ms);

// Stop playback and cancel any pending repeat
void CW_StopPlayback(void);

// Periodic handler for playback FSM - returns CW_Action_t (carrier on/off/hold)
CW_Action_t CW_PlaybackHandleState(void);

// Periodic deadline handler to refresh CW playback UI indicator (blinker).
// Call from a periodic context (e.g., APP_TimeSlice10ms). Uses timer_millis.
void CW_PlaybackIndicatorDeadline(void);

#ifdef ENABLE_FLASHLIGHT
extern bool gCW_FlashlightSending;
#endif

#endif // APP_CWKEYER_H
