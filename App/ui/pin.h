/* PIN entry UI for DualMode Blackout activation
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UI_PIN_H
#define UI_PIN_H

#include <stdbool.h>

#ifdef ENABLE_FEAT_DUALMODE

/* Blocking PIN entry for Blackout activation. Returns true if valid, false if cancelled. */
bool UI_DisplayPinEntry(void);

/* Blocking: set new PIN. Enter 4-6 digits, MENU, re-enter to confirm. Returns true if saved. */
bool UI_DisplaySetPin(void);

#endif /* ENABLE_FEAT_DUALMODE */

#endif /* UI_PIN_H */
