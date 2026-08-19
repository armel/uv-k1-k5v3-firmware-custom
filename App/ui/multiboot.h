/* Copyright 2026 F4HWN
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UI_MULTIBOOT_H
#define UI_MULTIBOOT_H

#include <stdint.h>

/* Blocking boot-time slot selector. Returns only when the user chooses EXIT;
 * a successful restore resets the radio from the RAM-resident copier. */
void UI_MultibootSelector(void);

/* Resolve the active settings profile at boot, BEFORE any settings read.
 * Detects a firmware installed outside multiboot (internal != slot[marker]) and,
 * if so, discreetly self-backs it up into slot 0 and adopts profile 0. Returns
 * the profile index (0..MB_SLOT_COUNT-1) to feed PY25Q16_SetProfileBase(). */
uint8_t MB_BootResolveProfile(void);

/* Slot selected by MB_BootResolveProfile for the current session. This is kept
 * in RAM so UI callers do not have to reread the external-flash marker. */
uint8_t MB_GetRunningSlot(void);

#endif
