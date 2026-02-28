/* Dual-Mode: EU Safe / Blackout operation modes
 * Copyright 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_OP_MODE_H
#define APP_OP_MODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_FEAT_DUALMODE

typedef enum {
    OP_MODE_EU_SAFE = 0,
    OP_MODE_BLACKOUT = 1,
} OpMode_t;

/* Get current operation mode */
OpMode_t OpMode_GetCurrent(void);

/* Activate a mode. For BLACKOUT, pin must be verified first. */
void OpMode_Activate(OpMode_t mode);

/* Verify PIN, returns true if correct. pin[] has 'len' digits (4-6). */
bool OpMode_VerifyPin(const uint8_t *pin, uint8_t len);

/* Set new PIN (4-6 digits). Saves to EEPROM. */
void OpMode_SetPin(const uint8_t *pin, uint8_t len);

/* Call on boot: force EU Safe, sync gSetting_F_LOCK */
void OpMode_Init(void);

/* Is Blackout currently active? (TX on all freqs) */
bool OpMode_IsBlackout(void);

#define OP_MODE_PIN_MIN 4
#define OP_MODE_PIN_MAX 6
#define OP_MODE_PIN_LEN 6

#endif /* ENABLE_FEAT_DUALMODE */

#endif /* APP_OP_MODE_H */
