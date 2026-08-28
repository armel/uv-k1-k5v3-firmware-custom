/* Copyright 2026
 * https://github.com/armel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * RTC time persistence (see rtc_save.h).
 * Storage layout at RTC_SAVE_FLASH_ADDR:
 *   0..3 magic   0x52544353 ("RTCS")
 *   4..7 seconds 2000-epoch Beijing wall clock
 *   8..11 crc8   CRC-8 (poly 0x07) of bytes 0..7
 *   12..15 reserved 0
 */

#include <string.h>

#include "rtc_save.h"
#include "driver/py25q16.h"
#include "driver/rtc.h"
#include "driver/crc.h"

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

#define RTC_SAVE_FLASH_ADDR   0x00B000u   // free physical sector, clear of settings/log/data maps
#define RTC_SAVE_MAGIC        0x52544353u // "RTCS"
#define RTC_SAVE_RESERVED     0u

typedef struct {
    uint32_t magic;
    uint32_t seconds;
    uint8_t  crc8;
    uint8_t  reserved[3];
} RTC_SaveBlock_t;

_Static_assert(sizeof(RTC_SaveBlock_t) == 12, "RTC_SaveBlock_t must be 12 bytes");

static uint8_t RTC_SaveCrc8(const uint8_t *pBuffer, uint16_t Size)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < Size; i++)
    {
        crc ^= pBuffer[i];
        for (uint8_t b = 0; b < 8; b++)
        {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void RTC_SaveTimeToFlash(void)
{
    RTC_SaveBlock_t block;
    block.magic = RTC_SAVE_MAGIC;
    block.seconds = RTC_GetUnix32();
    block.reserved[0] = RTC_SAVE_RESERVED;
    block.reserved[1] = RTC_SAVE_RESERVED;
    block.reserved[2] = RTC_SAVE_RESERVED;
    block.crc8 = RTC_SaveCrc8((const uint8_t *)&block, 7);

    PY25Q16_WriteBuffer(RTC_SAVE_FLASH_ADDR, (uint8_t *)&block, sizeof(block), false);
}

bool RTC_LoadTimeFromFlash(void)
{
    RTC_SaveBlock_t block;
    PY25Q16_ReadBuffer(RTC_SAVE_FLASH_ADDR, (uint8_t *)&block, sizeof(block));

    if (block.magic != RTC_SAVE_MAGIC)
        return false;
    if (RTC_SaveCrc8((const uint8_t *)&block, 7) != block.crc8)
        return false;
    if (block.seconds < 68000000u) // reject pre-2025 timestamps
        return false;

    RTC_Init();
    RTC_SetUnix32(block.seconds);
    return true;
}

#endif // ENABLE_FEAT_F4HWN_DOPPLER
