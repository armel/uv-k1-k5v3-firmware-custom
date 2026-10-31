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

#ifndef APP_CWAPP_H
#define APP_CWAPP_H

#include <stdbool.h>

// Called from main loop after APP_Update().
// Handles CW keyer actions: carrier on/off/hold, suspend/resume, local sidetone.
void CW_AppUpdate(void);

// End CW transmission immediately.
// Unlike the normal PTT release path, this does NOT honor RTTE
// (Repeater Tail Tone Elimination is invalid for CW).
void CW_EndTxNow(void);

// True while the local-only sidetone (no RF) holds the audio path, from the first
// element until the hang time passes with the key up.
bool CW_LocalSidetoneHeld(void);

// Called from the 10ms timeslice to perform CW-related periodic updates
// (macro recording state, playback indicator deadline, TX display holdoff).
void CW_TimeSlice10ms(void);

// Around a modal overlay app (or the app menu): the main loop, and with it
// CW_AppUpdate, stops while it runs. Enter ends any CW TX (a QSK hang included),
// playback and the local sidetone, and hands the keyer's pins (PA10, PA11/PA12,
// PB15) back; Exit puts the keyer back as the current modulation wants it.
void CW_OverlayEnter(void);
void CW_OverlayExit(void);

#endif // APP_CWAPP_H
