/* Copyright 2026 F4HWN
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UI_MULTIBOOT_H
#define UI_MULTIBOOT_H

/* Blocking boot-time slot selector. Returns only when the user chooses EXIT;
 * a successful restore resets the radio from the RAM-resident copier. */
void UI_MultibootSelector(void);

#endif
