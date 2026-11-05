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

#include "tonecall_seq.h"

const char *const tcTypeNames[TC_TYPE_COUNT] = {
    "ZVEI-1", "ZVEI-2", "CCIR-1", "CCIR-2"
};

static const uint16_t ZVEI_DIGIT_HZ[10] = {
    2400, 1060, 1160, 1270, 1400, 1530, 1670, 1830, 2000, 2200
};

static const uint16_t CCIR_DIGIT_HZ[10] = {
    1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860
};

/* Per type: the digit table follows from the type (ZVEI for ZVEI-1/2, CCIR for
 * CCIR-1/2), the repeat tone and the tone duration do not. */
typedef struct {
    uint16_t repeatHz;
    uint16_t toneMs;
} TcStd_t;

static const TcStd_t STANDARDS[TC_TYPE_COUNT] = {
    [TC_ZVEI1] = { 2600,  70 },
    [TC_ZVEI2] = {  970,  70 },
    [TC_CCIR1] = { 2110, 100 },
    [TC_CCIR2] = { 2110,  70 },
};

uint16_t TC_ToneMs(uint8_t type)
{
    return type < TC_TYPE_COUNT ? STANDARDS[type].toneMs : 0;
}

uint8_t TC_BuildTones(uint8_t type, const char *digits, uint16_t *freqs)
{
    if (type >= TC_TYPE_COUNT)
        return 0;

    const uint16_t *digitHz = type >= TC_CCIR1 ? CCIR_DIGIT_HZ : ZVEI_DIGIT_HZ;
    uint8_t n = 0;
    for (; digits[n]; n++) {
        const uint8_t d = (uint8_t)(digits[n] - '0');
        if (d > 9 || n >= TC_MAX_DIGITS)
            return 0;
        freqs[n] = digitHz[d];
        if (n && freqs[n] == freqs[n - 1])     /* same tone twice: send the repeat tone */
            freqs[n] = STANDARDS[type].repeatHz;
    }
    return n;
}

/* ---- preset storage (see tonecall_seq.h); no division, the app links none ---- */

const uint32_t tcPow10[TC_POW10_COUNT] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000
};
#define POW10 tcPow10

#define TC_PRESET_MAX (TC_TYPE_COUNT * TC_CODE_SPACE)   /* 444444440 < 2^29 */

uint32_t TC_PackPreset(uint8_t type, const char *digits)
{
    uint32_t code = 0;
    uint8_t n = 0;
    if (type >= TC_TYPE_COUNT)
        return 0;
    for (; digits[n]; n++) {
        const uint8_t d = (uint8_t)(digits[n] - '0');
        if (d > 9 || n >= TC_MAX_DIGITS)
            return 0;
        code = code * 10u + d;
    }
    if (!n)
        return 0;
    /* the codes of n digits follow the shorter ones: 10 + 100 + ... + 10^(n-1) */
    for (uint8_t k = 1; k < n; k++)
        code += POW10[k];
    return 1u + type * TC_CODE_SPACE + code;
}

uint8_t TC_UnpackPreset(uint32_t value, uint8_t *type, char *digits)
{
    *type = 0;
    digits[0] = '\0';
    if (value == 0 || value > TC_PRESET_MAX)
        return 0;
    value--;
    uint8_t t = 0;
    while (value >= TC_CODE_SPACE) {
        value -= TC_CODE_SPACE;
        t++;
    }
    uint8_t n = 1;
    while (n < TC_MAX_DIGITS && value >= POW10[n]) {   /* past the codes of n digits */
        value -= POW10[n];
        n++;
    }
    for (uint8_t k = 0; k < n; k++) {
        const uint32_t place = POW10[n - 1u - k];
        uint8_t d = 0;
        while (value >= place) {
            value -= place;
            d++;
        }
        digits[k] = (char)('0' + d);
    }
    digits[n] = '\0';
    *type = t;
    return n;
}

void TC_CfgPack(uint32_t *words, const uint32_t *presets, uint8_t last)
{
    const uint16_t tag = (uint16_t)((TC_CFG_MAGIC << 2) | (last & 3u));
    for (uint8_t i = 0; i < TC_PRESET_COUNT; i++)
        words[i] = (presets[i] & 0x1FFFFFFFu) | ((uint32_t)((tag >> (3u * i)) & 7u) << 29);
}

bool TC_CfgUnpack(const uint32_t *words, uint32_t *presets, uint8_t *last)
{
    uint16_t tag = 0;
    for (uint8_t i = 0; i < TC_PRESET_COUNT; i++)
        tag |= (uint16_t)((words[i] >> 29) << (3u * i));
    const bool ok = (tag >> 2) == TC_CFG_MAGIC;
    *last = ok ? (uint8_t)(tag & 3u) : 0u;
    for (uint8_t i = 0; i < TC_PRESET_COUNT; i++) {
        const uint32_t v = words[i] & 0x1FFFFFFFu;
        presets[i] = ok && v <= TC_PRESET_MAX ? v : 0u;
    }
    return ok;
}
