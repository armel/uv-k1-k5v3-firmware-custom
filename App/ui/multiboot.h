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

#ifndef UI_MULTIBOOT_H
#define UI_MULTIBOOT_H

#include <stdint.h>

/* Blocking boot-time slot selector. Returns only when the user chooses EXIT;
 * a successful restore resets the radio from the RAM-resident copier. */
void UI_MultibootSelector(void);

/* Show a blocking, acknowledged error screen for a failed SetCfg operation. */
void UI_MultibootShowConfigError(uint8_t err);

/* Resolve the active config bank at boot, BEFORE any settings read. Detects a
 * firmware installed outside multiboot (internal identity != marker) and, if so,
 * discreetly self-backs it up into slot 0 and adopts slot 0 / bank 0. Returns the
 * config bank (0..MB_BANK_COUNT-1) to feed PY25Q16_SetBankBase(); the exact
 * firmware slot and bank are cached too (see MB_GetRunningSlot/MB_GetActiveBank). */
uint8_t MB_BootResolveState(void);

/* Exact firmware slot and active config bank resolved for this session. Both
 * are cached in RAM so UI callers never have to reread the marker or scan slot
 * headers merely to render their state. */
uint8_t MB_GetRunningSlot(void);
uint8_t MB_GetActiveBank(void);

#endif
