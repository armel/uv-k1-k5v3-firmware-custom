/* Copyright 2026
 * https://github.com/armel
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
 *
 * Automatic satellite Doppler tracking: storage and lookup of the
 * pre-computed uplink/downlink frequency table (see doppler.h).
 */

#include <string.h>

#include "doppler.h"
#include "driver/py25q16.h"

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

static DOPPLER_Satellite_t gDopplerSatellite;
static bool gDopplerValid = false;

// 布局契约: 结构体必须恰好 32 字节 (无填充), 否则 UART Size 校验与
// 网页工具 (protocol.js) 的紧凑布局会错位. start_unix 已在偏移 0 保证对齐.
_Static_assert(sizeof(DOPPLER_Satellite_t) == 32, "DOPPLER_Satellite_t must be 32 bytes");
_Static_assert(sizeof(DOPPLER_Entry_t) == 8, "DOPPLER_Entry_t must be 8 bytes");

// Valid frequency range, in 10 Hz units (100 MHz .. 1 GHz)
#define DOPPLER_FREQ_MIN 10000000u
#define DOPPLER_FREQ_MAX 100000000u

static uint8_t DOPPLER_Crc8(const uint8_t *pBuffer, uint16_t Size)
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

static bool DOPPLER_IsLeapYear(uint8_t Year2000)
{
    uint16_t year = (uint16_t)2000 + Year2000;
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool DOPPLER_IsValid(const DOPPLER_Satellite_t *pSat)
{
    if (pSat->name[9] != 0)
    {
        return false;
    }
    if (pSat->name[0] < 32 || pSat->name[0] > 126)
    {
        return false;
    }
    if (pSat->sum_time == 0 || pSat->sum_time > 2u * DOPPLER_MAX_ENTRIES)
    {
        return false;
    }
    // Reject clearly invalid time fields (year 2000-based, 1..99)
    if (pSat->start_time[0] > 99 || pSat->end_time[0] > 99)
    {
        return false;
    }
    return DOPPLER_Crc8((const uint8_t *)pSat, 30) == pSat->crc8;
}

void DOPPLER_Init(void)
{
    PY25Q16_ReadBuffer(DOPPLER_FLASH_BASE, &gDopplerSatellite, sizeof(gDopplerSatellite));
    gDopplerValid = DOPPLER_IsValid(&gDopplerSatellite);
}

bool DOPPLER_HasData(void)
{
    return gDopplerValid;
}

const DOPPLER_Satellite_t *DOPPLER_GetSatellite(void)
{
    return &gDopplerSatellite;
}

uint32_t DOPPLER_UnixTime(const uint8_t t[6])
{
    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    uint32_t seconds = 0;
    const uint8_t year = t[0];
    const uint8_t month = (t[1] > 12u) ? 12u : t[1]; // defensive clamp: days_in_month has 12 entries

    for (uint8_t y = 0; y < year; y++)
    {
        seconds += (DOPPLER_IsLeapYear(y) ? 366u : 365u) * 86400u;
    }
    for (uint8_t m = 1; m < month; m++)
    {
        seconds += days_in_month[m - 1] * 86400u;
        if (m == 2 && DOPPLER_IsLeapYear(year))
        {
            seconds += 86400u;
        }
    }
    if (t[2] > 0)
    {
        seconds += (uint32_t)(t[2] - 1) * 86400u;
    }
    seconds += (uint32_t)t[3] * 3600u;
    seconds += (uint32_t)t[4] * 60u;
    seconds += t[5];

    return seconds;
}

bool DOPPLER_GetEntry(int32_t unixNow, DOPPLER_Entry_t *pEntry)
{
    if (!gDopplerValid || pEntry == NULL)
    {
        return false;
    }

    const int32_t diff = unixNow - (int32_t)gDopplerSatellite.start_unix;
    if (diff < 0)
    {
        return false; // pass not started yet
    }

    const uint16_t entryCount = (uint16_t)((gDopplerSatellite.sum_time + 1u) >> 1);
    const uint16_t index = (uint16_t)(diff >> 1);
    if (index >= entryCount || index >= DOPPLER_MAX_ENTRIES)
    {
        return false; // pass already over
    }

    PY25Q16_ReadBuffer(DOPPLER_FLASH_TABLE + (uint32_t)index * sizeof(DOPPLER_Entry_t),
                       pEntry, sizeof(DOPPLER_Entry_t));

    if (pEntry->uplink < DOPPLER_FREQ_MIN || pEntry->uplink > DOPPLER_FREQ_MAX ||
        pEntry->downlink < DOPPLER_FREQ_MIN || pEntry->downlink > DOPPLER_FREQ_MAX)
    {
        return false;
    }
    return true;
}

void DOPPLER_Erase(void)
{
    for (uint32_t addr = DOPPLER_FLASH_BASE; addr < DOPPLER_FLASH_BASE + 4u * 0x1000u; addr += 0x1000u)
    {
        PY25Q16_SectorErase(addr);
    }
    gDopplerValid = false;
}

bool DOPPLER_WriteSatellite(const DOPPLER_Satellite_t *pSat)
{
    if (pSat == NULL)
    {
        return false;
    }

    DOPPLER_Satellite_t copy;
    memcpy(&copy, pSat, sizeof(copy));
    copy.crc8 = 0;
    copy.reserved = 0;
    copy.crc8 = DOPPLER_Crc8((const uint8_t *)&copy, 30);

    PY25Q16_WriteBuffer(DOPPLER_FLASH_BASE, &copy, sizeof(copy), false);

    // 数据完整性由 CRC8 保证 (DOPPLER_IsValid 校验), 无需写后回读。
    // 注: 写后立即用 DMA 回读 (SPI_ReadBuf) 数据不可靠, 不用它做校验。
    memcpy(&gDopplerSatellite, &copy, sizeof(copy));
    gDopplerValid = DOPPLER_IsValid(&gDopplerSatellite);
    return gDopplerValid;
}

bool DOPPLER_WriteEntry(uint16_t Index, const DOPPLER_Entry_t *pEntry)
{
    if (pEntry == NULL || Index >= DOPPLER_MAX_ENTRIES)
    {
        return false;
    }

    const uint32_t addr = DOPPLER_FLASH_TABLE + (uint32_t)Index * sizeof(DOPPLER_Entry_t);
    PY25Q16_WriteBuffer(addr, pEntry, sizeof(DOPPLER_Entry_t), false);
    return true;
}

#endif // ENABLE_FEAT_F4HWN_DOPPLER
