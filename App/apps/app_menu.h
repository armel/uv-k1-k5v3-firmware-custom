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

#ifndef APPS_APP_MENU_H
#define APPS_APP_MENU_H

#include <stdint.h>

/* Blocking "Apps" selector: scans the overlay-app slots, lists the committed
 * ones by name, and launches the chosen one. UP/DOWN move, MENU launches, EXIT
 * closes. Digits select installed physical slots; a second digit within one
 * second first tries the two-digit slot, falling back to the new digit alone.
 * Returns when the user leaves. run_slot, if installed, is run at once as if
 * MENU had been pressed on it (power-on resume: its EXIT comes back here);
 * 0xFF for none. */
void APP_MenuOpen(uint8_t run_slot);

#endif /* APPS_APP_MENU_H */
