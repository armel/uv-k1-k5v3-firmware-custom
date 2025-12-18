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
    // Sorted by EEPROM addr
    _MK_MAPPING(0x000000, 0x0000, 0x0c80),  //
    _MK_MAPPING(0x001000, 0x0c80, 0x0d60),  //
    _MK_MAPPING(0x002000, 0x0d60, 0x0e30),  //
    _MK_MAPPING(HOLE_ADDR, 0x0e30, 0x0e70), //
    _MK_MAPPING(0x004000, 0x0e70, 0x0e80),  //
    _MK_MAPPING(0x005000, 0x0e80, 0x0e88),  //
    _MK_MAPPING(0x006000, 0x0e88, 0x0e90),  //
    _MK_MAPPING(0x007000, 0x0e90, 0x0ee0),  //
    _MK_MAPPING(0x008000, 0x0ee0, 0x0f18),  //
    _MK_MAPPING(0x009000, 0x0f18, 0x0f20),  //
    _MK_MAPPING(HOLE_ADDR, 0x0f20, 0x0f30), //
    _MK_MAPPING(0x00a000, 0x0f30, 0x0f40),  //
    _MK_MAPPING(0x00b000, 0x0f40, 0x0f48),  //
    _MK_MAPPING(HOLE_ADDR, 0x0f48, 0x0f50), //
    _MK_MAPPING(0x00e000, 0x0f50, 0x1bd0),  //
    _MK_MAPPING(HOLE_ADDR, 0x1bd0, 0x1c00), //
    _MK_MAPPING(0x00f000, 0x1c00, 0x1d00),  //
    _MK_MAPPING(HOLE_ADDR, 0x1d00, 0x1e00), //
    _MK_MAPPING(0x010000, 0x1e00, 0x1f90),  //
    _MK_MAPPING(HOLE_ADDR, 0x1f90, 0x1ff0), //
    _MK_MAPPING(0x00c000, 0x1ff0, 0x2000),  //
    _MK_MAPPING(0x003000, 0x2000, 0x2080),  //
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
