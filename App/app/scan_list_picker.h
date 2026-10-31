/* Licensed under the Apache License, Version 2.0 (the "License");
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

#ifndef APP_SCAN_LIST_PICKER_H
#define APP_SCAN_LIST_PICKER_H

#ifdef ENABLE_FEAT_F4HWN_SCAN_LIST_PICKER

#include <stdbool.h>
#include <stdint.h>
#include "driver/keyboard.h"

/* Scan-list picker: during a memory scan, a short * opens a list of the scan
 * lists (ALL, MIX, then each list that holds a channel) over the main screen.
 * The scan goes on underneath; MENU moves it to the selected list. */

#define SCAN_LIST_PICKER_TIMEOUT_500MS 20u   /* 10 s without a key */

extern bool gScanListPickerOpen;

void SCAN_LIST_PICKER_Open(void);
void SCAN_LIST_PICKER_Close(void);
bool SCAN_LIST_PICKER_ProcessKey(KEY_Code_t key, bool isPressed, bool isHeld);
void SCAN_LIST_PICKER_TimeSlice500ms(void);
void SCAN_LIST_PICKER_Draw(void);

#endif

#endif
