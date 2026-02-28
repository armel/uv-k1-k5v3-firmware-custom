/* Dual-Mode: EU Safe / Blackout operation modes
 * Copyright 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef ENABLE_FEAT_DUALMODE

#include "app/op_mode.h"
#include "driver/py25q16.h"
#include "misc.h"
#include "settings.h"

#define BLACKOUT_PIN_EEPROM_ADDR 0x00A170

static OpMode_t s_opMode = OP_MODE_EU_SAFE;

/* PIN: length (4-6) + digits. Default 0000 (len=4). */
static uint8_t s_storedPinLen = OP_MODE_PIN_MIN;
static uint8_t s_storedPin[OP_MODE_PIN_LEN] = { 0, 0, 0, 0, 0, 0 };

static void loadPinFromEeprom(void)
{
    uint8_t buf[7];
    PY25Q16_ReadBuffer(BLACKOUT_PIN_EEPROM_ADDR, buf, sizeof(buf));
    if (buf[0] >= OP_MODE_PIN_MIN && buf[0] <= OP_MODE_PIN_MAX) {
        s_storedPinLen = buf[0];
        for (int i = 0; i < OP_MODE_PIN_LEN; i++)
            s_storedPin[i] = (i < s_storedPinLen) ? (buf[1 + i] % 10) : 0;
    }
}

static void savePinToEeprom(void)
{
    uint8_t buf[7];
    buf[0] = s_storedPinLen;
    for (int i = 0; i < OP_MODE_PIN_LEN; i++)
        buf[1 + i] = (i < s_storedPinLen) ? s_storedPin[i] : 0xFF;
    PY25Q16_WriteBuffer(BLACKOUT_PIN_EEPROM_ADDR, buf, sizeof(buf), false);
}

void OpMode_Init(void)
{
    s_opMode = OP_MODE_EU_SAFE;
    gSetting_F_LOCK = F_LOCK_EU_SAFE;
    gEeprom.TX_VFO = 0;  /* TZ: TX always on VFO A at boot */
    loadPinFromEeprom();
}

OpMode_t OpMode_GetCurrent(void)
{
    return s_opMode;
}

bool OpMode_IsBlackout(void)
{
    return s_opMode == OP_MODE_BLACKOUT;
}

void OpMode_Activate(OpMode_t mode)
{
    s_opMode = mode;
    if (mode == OP_MODE_EU_SAFE) {
        gSetting_F_LOCK = F_LOCK_EU_SAFE;
    } else {
        gSetting_F_LOCK = F_LOCK_NONE;
    }
}

bool OpMode_VerifyPin(const uint8_t *pin, uint8_t len)
{
    if (len != s_storedPinLen || len < OP_MODE_PIN_MIN || len > OP_MODE_PIN_MAX)
        return false;
    for (uint8_t i = 0; i < len; i++) {
        if (pin[i] != s_storedPin[i])
            return false;
    }
    return true;
}

void OpMode_SetPin(const uint8_t *pin, uint8_t len)
{
    if (len < OP_MODE_PIN_MIN || len > OP_MODE_PIN_MAX)
        return;
    s_storedPinLen = len;
    for (uint8_t i = 0; i < OP_MODE_PIN_LEN; i++)
        s_storedPin[i] = (i < len) ? (pin[i] % 10) : 0;
    savePinToEeprom();
}

#endif /* ENABLE_FEAT_DUALMODE */
