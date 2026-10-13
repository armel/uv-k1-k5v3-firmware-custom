/* Copyright 2026 Johan Denoyer F4WAT
 * https://github.com/jdenoy
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

/* Selcall tone sequence builder: no hardware dependency (host-tested, see
 * tools/selcall/test_selcall_seq.c). */

#include "app/selcall.h"

const char *const gSelCallTypeNames[SELCALL_TYPE_COUNT] = {
    "OFF", "ZVEI-1", "ZVEI-2", "CCIR-1", "CCIR-2"
};

static const uint16_t ZVEI_DIGIT_HZ[10] = {
    2400, 1060, 1160, 1270, 1400, 1530, 1670, 1830, 2000, 2200
};

static const uint16_t CCIR_DIGIT_HZ[10] = {
    1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860
};

/* Per type, from SELCALL_ZVEI1 on (SELCALL_OFF has no entry). The digit table
 * follows from the type: ZVEI for ZVEI-1/2, CCIR for CCIR-1/2. */
typedef struct {
    uint16_t repeatHz;
    uint16_t toneMs;
} SelCallStd_t;

static const SelCallStd_t STANDARDS[SELCALL_TYPE_COUNT - 1] = {
    [SELCALL_ZVEI1 - 1] = { 2600,  70 },
    [SELCALL_ZVEI2 - 1] = {  970,  70 },
    [SELCALL_CCIR1 - 1] = { 2110, 100 },
    [SELCALL_CCIR2 - 1] = { 2110,  70 },
};

uint16_t SELCALL_ToneMs(uint8_t type)
{
    return (type != SELCALL_OFF && type < SELCALL_TYPE_COUNT) ? STANDARDS[type - 1].toneMs : 0;
}

uint8_t SELCALL_BuildTones(uint8_t type, uint32_t code, uint16_t *freqs)
{
    if (type == SELCALL_OFF || type >= SELCALL_TYPE_COUNT || code > SELCALL_CODE_MAX)
        return 0;

    const uint16_t *digitHz = type >= SELCALL_CCIR1 ? CCIR_DIGIT_HZ : ZVEI_DIGIT_HZ;
    for (uint8_t i = SELCALL_DIGITS; i-- > 0; ) {   /* last digit first */
        freqs[i] = digitHz[code % 10u];
        code /= 10u;
    }
    for (uint8_t i = 1; i < SELCALL_DIGITS; i++)    /* same tone twice: send the repeat tone */
        if (freqs[i] == freqs[i - 1])
            freqs[i] = STANDARDS[type - 1].repeatHz;
    return SELCALL_DIGITS;
}
