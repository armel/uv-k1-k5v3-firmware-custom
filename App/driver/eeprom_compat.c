/* Copyright 2025 muzkr https://github.com/muzkr
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

/**
 * -----------------------------------
 * Note:
 *
 *    Write operations are inherently inefficient; use this module very wisely!!
 *
 * ------------------------------------
 */

#include "driver/eeprom.h"
#include "driver/py25q16.h"
#include <string.h>

#define HOLE_ADDR 0x1000000

typedef struct
{
    uint32_t PY25Q16_Addr; // Sector address
    uint16_t EEPROM_Addr;
    uint16_t Size;
} AddrMapping_t;

#define _MK_MAPPING(PY25Q16_Addr, EEPROM_From, EEPROM_To) {PY25Q16_Addr, EEPROM_From, EEPROM_To - EEPROM_From}

static const AddrMapping_t ADDR_MAPPINGS[] = {
    _MK_MAPPING(0x000000, 0x000000, 0x001000),  // 256 MR Freq * 16 Bytes (ex 0x000000)
    _MK_MAPPING(0x001000, 0x001000, 0x002000),  // 256 MR Freq * 16 Bytes
    _MK_MAPPING(0x002000, 0x002000, 0x003000),  // 256 MR Freq * 16 Bytes
    _MK_MAPPING(0x003000, 0x003000, 0x004000),  // 256 MR Freq * 16 Bytes

    _MK_MAPPING(0x004000, 0x004000, 0x005000),  // 256 MR Name * 16 Bytes (ex 0x00e000)
    _MK_MAPPING(0x005000, 0x005000, 0x006000),  // 256 MR Name * 16 Bytes
    _MK_MAPPING(0x006000, 0x006000, 0x007000),  // 256 MR Name * 16 Bytes
    _MK_MAPPING(0x007000, 0x007000, 0x008000),  // 256 MR Name * 16 Bytes

    _MK_MAPPING(0x008000, 0x008000, 0x00886E),  // 1024 MR  + 7 VFO Attributes * 2 Bytes (ex 0x002000) 0x008000 -> 0x00880E
                                                // List name * 4 Bytes 0x00880E -> 0x00886E

    _MK_MAPPING(0x009000, 0x009000, 0x0090D6),  // 14 VFO * 16 Bytes (ex 0x001000)

    _MK_MAPPING(0x00A000, 0x00A000, 0x00A170),  // Settings * 16 Bytes (ex 0x004000)        0x00A000 -> 0x00A010
                                                // Settings * 16 Bytes (ex 0x005000)        0x00A010 -> 0x00A020
                                                // Settings FM * 8 Bytes (0x006000)         0x00A020 -> 0x00A028
                                                // MR FM * 128 Bytes (0x003000)             0x00A028 -> 0x00A0A8
                                                // Settings * 80 Bytes (0x007000)           0x00A0A8 -> 0x00A0F8
                                                // Settings * 56 Bytes (0x008000)           0x00A0F8 -> 0x00A130
                                                // Settings Scanlist * 8 Bytes (0x009000)   0x00A130 -> 0x00A140
                                                // Settings AES * 16 Bytes (0x00A000)       0x00A140 -> 0x00A150
                                                // Settings * 8 Bytes (0x00B000)            0x00A150 -> 0x00A158
                                                // Settings F4HWN * 8 Bytes (0x00C000)      0x00A158 -> 0x00A160
                                                // Settings Version * 16 Bytes              0x00A160 -> 0x00A170

    _MK_MAPPING(0x010000, 0x00B000, 0x00B200),  // Calibration 512 Bytes!!!
};

static void AddrTranslate(uint16_t EEPROM_Addr, uint16_t Size, uint32_t *PY25Q16_Addr_out, uint16_t *Size_out, bool *End_out);

void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size)
{
    while (Size)
    {
        uint32_t PY_Addr;
        uint16_t PY_Size;
        AddrTranslate(Address, Size, &PY_Addr, &PY_Size, NULL);
        if (PY_Addr >= HOLE_ADDR)
        {
            memset(pBuffer, 0xff, PY_Size);
        }
        else
        {
            PY25Q16_ReadBuffer(PY_Addr, pBuffer, PY_Size);
        }
        Address += PY_Size;
        pBuffer += PY_Size;
        Size -= PY_Size;
    }
}

void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer)
{
    // Write 8 bytes!!

    uint16_t Size = 8;
    while (Size)
    {
        uint32_t PY_Addr;
        uint16_t PY_Size;
        bool AppendFlag;
        AddrTranslate(Address, Size, &PY_Addr, &PY_Size, &AppendFlag);
        if (PY_Addr < HOLE_ADDR)
        {
            PY25Q16_WriteBuffer(PY_Addr, pBuffer, PY_Size, AppendFlag);
        }
        Address += PY_Size;
        pBuffer += PY_Size;
        Size -= PY_Size;
    }
}

static void AddrTranslate(uint16_t EEPROM_Addr, uint16_t Size, uint32_t *PY25Q16_Addr_out, uint16_t *Size_out, bool *End_out)
{
    const AddrMapping_t *p = NULL;
    for (uint32_t i = 0, N = sizeof(ADDR_MAPPINGS) / sizeof(AddrMapping_t); i < N; i++)
    {
        p = ADDR_MAPPINGS + i;
        if (p->EEPROM_Addr <= EEPROM_Addr && EEPROM_Addr < (p->EEPROM_Addr + p->Size))
        {
            goto HIT;
        }
    }

    *PY25Q16_Addr_out = HOLE_ADDR;
    *Size_out = Size;
    return;

HIT:
    const uint16_t Off = EEPROM_Addr - p->EEPROM_Addr;
    const uint16_t Rem = p->Size - Off;
    if (Size > Rem)
    {
        Size = Rem;
    }

    *PY25Q16_Addr_out = HOLE_ADDR == p->PY25Q16_Addr ? HOLE_ADDR : (p->PY25Q16_Addr + Off);
    *Size_out = Size;

    if (End_out && HOLE_ADDR != p->PY25Q16_Addr)
    {
        *End_out = (Size == Rem);
    }
}
