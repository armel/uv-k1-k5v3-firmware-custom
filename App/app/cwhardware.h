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

 #ifndef APP_CWHARDWARE_H
#define APP_CWHARDWARE_H

#include <stdint.h>
#include <stdbool.h>
#include "settings.h"

#define CW_ADC_20K_MIN 100  // anything less is not a valid target (would have to be an open circuit)
#define CW_ADC_10K_MIN 200 // anything less is not a valid target (wouldn't be able to tell them apart)
#define CW_ADC_MAX 1000 // anything more is not a valid target (would have to be a short circuit or VCC)
#define CW_ADC_GLITCH_GUARDBAND 20  // ADC readings must be within ±20 of expected value to be considered valid
#define CW_ADC_RANGE_LIMIT 50  // Furthest the read value is allowed away from centroid 

// Normalized paddle input + edge flags
typedef struct {
    bool dit;
    bool dah;
    bool dit_rise;
    bool dah_rise;
    bool last_is_dah;  // true if dah was the most recently pressed paddle (for Ultimatic mode's tie-break)
    bool ptt;          // USB port paddle modes: PTT, a straight key next to the paddle (keyed in cwkeyer.c)
} CW_Input;

// Read raw inputs for a specific mode
bool CW_ReadKeysForMode(uint8_t mode, bool *dit_out, bool *dah_out);

// Raw read of the USB paddle pins (PA12=tip/DP, PA11=ring/DM), independent of
// key-input mode flags. Used for USB iambic paddle reads and for USB Port
// Handkey mode (straight key via either pin).
void CW_ReadUSBPaddleRaw(bool *tip_out, bool *ring_out);

// Read normalized paddle inputs (computes edges)
void CW_ReadKeys(CW_Input *in);

// Keypad-lock gate for raw CW key input (paddles, handkey, PTT-as-key). Pass
// whether any key contact is closed right now; returns true when the input
// must be ignored. See the definition for details.
bool CW_KeyInputLockedOut(bool pressed);

// Configure port pins for paddle interface
void CW_ConfigurePortGround(bool enable);
void CW_ConfigurePortRing(bool enable);
void CW_ConfigureUsbPaddlePins(bool enable);

// Reset hardware-sampled state (call from keyer init)
void CW_HW_ResetKeySamples(void);

#endif // APP_CWHARDWARE_H
