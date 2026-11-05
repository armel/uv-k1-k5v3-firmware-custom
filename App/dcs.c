/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#include <stddef.h>
#include "dcs.h"
#include "misc.h"

// CTCSS Hz * 10
const uint16_t CTCSS_Options[50] = {
     670,  693,  719,  744,  770,  797,  825,  854,  885,  915,
     948,  974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
    1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
    1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
    2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541
};

// Indices in CTCSS_Options for the 12 non-homologated tones.
static const uint8_t CTCSS_ExtraIdx[12] = {
     1, 26, 28, 30, 32, 34, 36, 38, 39, 41,
    45, 49
};

// Sorted DCS codes: bit 8 is clear for indices 0..63, set for 64..103.
static const uint8_t DCS_OptionsLow[DCS_OPTION_COUNT] = {
    0x13, 0x15, 0x16, 0x19, 0x1A, 0x1E, 0x23, 0x27,
    0x29, 0x2B, 0x2C, 0x35, 0x39, 0x3A, 0x3B, 0x3C,
    0x4C, 0x4D, 0x4E, 0x52, 0x55, 0x59, 0x5A, 0x5C,
    0x63, 0x65, 0x6A, 0x6D, 0x6E, 0x72, 0x75, 0x7A,
    0x7C, 0x85, 0x8A, 0x93, 0x95, 0x96, 0xA3, 0xA4,
    0xA5, 0xA6, 0xA9, 0xAA, 0xAD, 0xB1, 0xB3, 0xB5,
    0xB6, 0xB9, 0xBC, 0xC6, 0xC9, 0xCD, 0xD5, 0xD9,
    0xDA, 0xE3, 0xE6, 0xE9, 0xEE, 0xF4, 0xF5, 0xF9,
    0x09, 0x0A, 0x0B, 0x13, 0x19, 0x1A, 0x25, 0x26,
    0x2A, 0x2C, 0x2D, 0x32, 0x34, 0x35, 0x36, 0x43,
    0x46, 0x4E, 0x53, 0x56, 0x5A, 0x66, 0x75, 0x86,
    0x8A, 0x94, 0x97, 0x99, 0x9A, 0xAC, 0xB2, 0xB4,
    0xC3, 0xCA, 0xD3, 0xD9, 0xDA, 0xDC, 0xE3, 0xEC,
};

uint16_t DCS_GetOption(uint8_t Option)
{
    return DCS_OptionsLow[Option] | (Option >= 64u ? 0x100u : 0u);
}

// Indices of the 21 non-PMR446 DCS codes.
static const uint8_t DCS_ExtraIdx[21] = {
     5,  9, 19, 25, 34, 36, 41, 43, 44, 48,
    50, 54, 56, 60, 71, 72, 73, 74, 75, 82,
    83
};

static uint32_t DCS_CalculateGolay(uint32_t CodeWord)
{
    unsigned int i;
    uint32_t Word = CodeWord;
    for (i = 0; i < 12; i++)
    {
        Word <<= 1;
        if (Word & 0x1000)
            Word ^= 0x08EA;
    }
    return CodeWord | ((Word & 0x0FFE) << 11);
}

uint32_t DCS_GetGolayCodeWord(DCS_CodeType_t CodeType, uint8_t Option)
{
    uint32_t Code = DCS_CalculateGolay(DCS_GetOption(Option) + 0x800U);
    if (CodeType == CODE_TYPE_REVERSE_DIGITAL)
        Code ^= 0x7FFFFF;
    return Code;
}

uint8_t DCS_GetCdcssCode(uint32_t Code)
{
    unsigned int i;
    for (i = 0; i < 23; i++)
    {
        uint32_t Shift;

        if (((Code >> 9) & 0x7U) == 4)
        {
            unsigned int j;
            for (j = 0; j < DCS_OPTION_COUNT; j++)
                if (DCS_GetOption(j) == (Code & 0x1FF))
                    if (DCS_GetGolayCodeWord(2, j) == Code)
                        return j;
        }

        Shift = Code >> 1;
        if (Code & 1U)
            Shift |= 0x400000U;
        Code = Shift;
    }

    return 0xFF;
}

uint8_t DCS_GetCtcssCode(int Code)
{
    unsigned int i;
    uint8_t      Result = 0xFF;
    int          Smallest = ARRAY_SIZE(CTCSS_Options);

    for (i = 0; i < ARRAY_SIZE(CTCSS_Options); i++)
    {
        int Delta = Code - CTCSS_Options[i];
        if (Delta < 0)
            Delta = -(Code - CTCSS_Options[i]);
        if (Smallest > Delta)
        {
            Smallest = Delta;
            Result   = i;
        }
    }

    return Result;
}

static uint8_t DCS_GetApprovedIndex(uint8_t Option, size_t options_size, size_t extraidx_size, const uint8_t *extraidx)
{
    unsigned int extra_pos = 0;

    if (Option >= options_size)
        return 0xFF;

    while (extra_pos < extraidx_size && extraidx[extra_pos] < Option)
        extra_pos++;
    if (extra_pos < extraidx_size && extraidx[extra_pos] == Option)
        return 0xFF;
    return Option - extra_pos;
}

uint8_t DCS_GetCtcssApprovedIndex(uint8_t Option)
{
    return DCS_GetApprovedIndex(Option, ARRAY_SIZE(CTCSS_Options), ARRAY_SIZE(CTCSS_ExtraIdx), CTCSS_ExtraIdx);
}

uint8_t DCS_GetDcsApprovedIndex(uint8_t Option)
{
    return DCS_GetApprovedIndex(Option, DCS_OPTION_COUNT, ARRAY_SIZE(DCS_ExtraIdx), DCS_ExtraIdx);
}
