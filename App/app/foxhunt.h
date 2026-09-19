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

#ifndef APP_FOXHUNT_H
#define APP_FOXHUNT_H

#if defined(ENABLE_FEAT_F4HWN_FOXHUNT) || defined(ENABLE_FEAT_F4HWN_BEACON)

#include "keyboard_state.h"

#include "../bitmaps.h"
#include "../board.h"
#include "py32f0xx.h"
#include "../driver/backlight.h"
#include "../driver/bk4819-regs.h"
#include "../driver/bk4819.h"
#include "../driver/gpio.h"
#include "../driver/keyboard.h"
#include "../driver/py25q16.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../driver/systick.h"
#include "../external/printf/printf.h"
#include "../font.h"
#include "../helper/battery.h"
#include "../misc.h"
#include "../radio.h"
#include "../settings.h"
#include "../ui/battery.h"
#include "../ui/helper.h"
#include "../ui/ui.h"
#include "../audio.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#endif

#if defined(ENABLE_FEAT_F4HWN_FOXHUNT) || defined(ENABLE_FEAT_F4HWN_BEACON) || defined(ENABLE_FEAT_F4HWN_OVERLAY_APPS)
// Assignable resident/overlay launchers (see action.c).
void ACTION_FoxHunt(void);
void ACTION_Beacon(void);
#endif

#ifdef ENABLE_FEAT_F4HWN_FOXHUNT
// Self-contained modal loop that owns the screen and keypad until EXIT.
void APP_RunFoxHunt(void);
#endif

#ifdef ENABLE_FEAT_F4HWN_BEACON
void APP_RunBeacon(void);
#endif

#endif // APP_FOXHUNT_H
