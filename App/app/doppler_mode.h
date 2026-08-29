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
 *
 * Doppler tracking mode: dedicated screen + key handling. The user enters
 * the current UTC date/time once (stored into the RTC), then the radio
 * follows the pre-computed uplink/downlink frequency table.
 */

#ifndef APP_DOPPLER_MODE_H
#define APP_DOPPLER_MODE_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

#include "app/doppler.h"

typedef enum {
    DOPPLER_STATE_OFF = 0,
    DOPPLER_STATE_INPUT_DATE,   // entering YYMMDD
    DOPPLER_STATE_INPUT_TIME,   // entering HHMMSS
    DOPPLER_STATE_TRACKING,     // following the pass
    DOPPLER_STATE_ADJUST,       // fine-tuning the RTC time
} DOPPLER_State_t;

// Enters the Doppler mode (initializes RTC/Flash on first use).
void DOPPLER_EnterMode(void);

// Leaves the Doppler mode and restores the previous VFO state.
void DOPPLER_ExitMode(void);

bool DOPPLER_IsActive(void);

// Key handling, wired into ProcessKeysFunctions[DISPLAY_DOPPLER].
void DOPPLER_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

// Periodic hook (main loop, every 10 ms): handles the 1 s tick (frequency
// tracking) and restores the TX frequency after a Doppler transmission.
void DOPPLER_TimeSlice(void);

// Full-screen renderer for the Doppler mode.
void DOPPLER_Render(void);

// Status-line strip: inverse satellite-name box + bandwidth. Called by
// DOPPLER_Render() and by UI_DisplayStatus() (which owns the status line).
void DOPPLER_RenderStatusStrip(const DOPPLER_Satellite_t *pSat);

// Marks the RTC as set for this power session (used by the UART 0x05E8
// "set time" command so the radio skips manual entry on long-press 0).
void DOPPLER_SetTimeFromUart(void);

// Re-arms the AOS-60 s / AOS-10 s reminders of one slot. Called by the
// UART 0x05E0/0x05E1 handlers so a freshly written or erased pass beeps
// again on its next pass window.
void DOPPLER_ResetReminders(uint8_t Slot);

#endif // ENABLE_FEAT_F4HWN_DOPPLER

#endif // APP_DOPPLER_MODE_H
