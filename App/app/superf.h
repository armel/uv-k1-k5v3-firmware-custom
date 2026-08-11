/* Copyright 2026 Armel F4HWN
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

#ifndef APP_SUPERF_H
#define APP_SUPERF_H

#ifdef ENABLE_SUPERF

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/system.h"

extern bool          gSuperFActive;
extern uint8_t       gCurrentSuperFIndex;

void ACTION_SuperF(void);
void SuperF_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void UI_DisplaySuperF(void);

#endif // ENABLE_SUPERF
#endif // APP_SUPERF_H
