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
static uint8_t gDopplerSlot = 0;   // active slot 0..3

// 布局契约: 结构体必须恰好 32 字节 (无填充), 否则 UART Size 校验与
// 网页工具 (protocol.js) 的紧凑布局会错位. start_unix 已在偏移 0 保证对齐.
_Static_assert(sizeof(DOPPLER_Satellite_t) == 32, "DOPPLER_Satellite_t must be 32 bytes");
_Static_assert(sizeof(DOPPLER_Entry_t) == 16, "DOPPLER_Entry_t must be 16 bytes");

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

bool DOPPLER_IsLeapYear(uint8_t Year2000)
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
    if (pSat->sum_time == 0 || pSat->sum_time >= DOPPLER_MAX_ENTRIES)
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

// Reads and validates the satellite info block of one slot.
static bool DOPPLER_LoadSlot(uint8_t Slot, DOPPLER_Satellite_t *pSat)
{
    if (Slot >= DOPPLER_SLOT_COUNT)
    {
        return false;
    }
    PY25Q16_ReadBuffer(DOPPLER_SLOT_BASE(Slot), pSat, sizeof(*pSat));
    return DOPPLER_IsValid(pSat);
}

void DOPPLER_Init(void)
{
    gDopplerSlot = 0;
    gDopplerValid = DOPPLER_LoadSlot(0, &gDopplerSatellite);
}

uint8_t DOPPLER_GetSlot(void)
{
    return gDopplerSlot;
}

bool DOPPLER_SelectSlot(uint8_t Slot)
{
    if (Slot >= DOPPLER_SLOT_COUNT)
    {
        return false;
    }

    DOPPLER_Satellite_t sat;
    const bool valid = DOPPLER_LoadSlot(Slot, &sat);
    gDopplerSlot = Slot;
    if (valid)
    {
        memcpy(&gDopplerSatellite, &sat, sizeof(sat));
    }
    gDopplerValid = valid;
    return valid;
}

bool DOPPLER_HasData(void)
{
    return gDopplerValid;
}

bool DOPPLER_SlotHasData(uint8_t Slot)
{
    DOPPLER_Satellite_t sat;
    return DOPPLER_LoadSlot(Slot, &sat);
}

bool DOPPLER_DiagSlot(uint8_t Slot, DOPPLER_Diag_t *pDiag)
{
    if (Slot >= DOPPLER_SLOT_COUNT || pDiag == NULL)
    {
        return false;
    }
    DOPPLER_Satellite_t sat;
    PY25Q16_ReadBuffer(DOPPLER_SLOT_BASE(Slot), &sat, sizeof(sat));
    pDiag->name0     = (uint8_t)sat.name[0];
    pDiag->name9     = (uint8_t)sat.name[9];
    pDiag->sum_time  = sat.sum_time;
    pDiag->crc_stored = sat.crc8;
    pDiag->crc_calc  = DOPPLER_Crc8((const uint8_t *)&sat, 30);
    return true;
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

void DOPPLER_UnixToDate(uint32_t Seconds, uint8_t t[6])
{
    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    uint32_t days = Seconds / 86400u;
    const uint32_t rem = Seconds % 86400u;
    t[3] = (uint8_t)(rem / 3600u);
    t[4] = (uint8_t)((rem % 3600u) / 60u);
    t[5] = (uint8_t)(rem % 60u);

    uint8_t year = 0;
    for (;;)
    {
        const uint16_t yearDays = DOPPLER_IsLeapYear(year) ? 366u : 365u;
        if (days < yearDays)
        {
            break;
        }
        days -= yearDays;
        year++;
    }
    t[0] = year;

    uint8_t month = 1;
    for (;;)
    {
        uint8_t monthDays = days_in_month[month - 1];
        if (month == 2 && DOPPLER_IsLeapYear(year))
        {
            monthDays = 29;
        }
        if (days < monthDays)
        {
            break;
        }
        days -= monthDays;
        month++;
    }
    t[1] = month;
    t[2] = (uint8_t)(days + 1u);
}

bool DOPPLER_GetEntryInterpolated(int32_t unixNow, uint16_t ms, DOPPLER_Entry_t *pEntry)
{
    if (!gDopplerValid || pEntry == NULL || ms > 999u)
    {
        return false;
    }

    const int32_t diff = unixNow - (int32_t)gDopplerSatellite.start_unix;
    if (diff < 0)
    {
        return false; // pass not started yet
    }

    const uint16_t entryCount = (uint16_t)(gDopplerSatellite.sum_time + 1u);
    const uint16_t index = (uint16_t)diff;
    if (index >= entryCount || index >= DOPPLER_MAX_ENTRIES)
    {
        return false; // pass already over
    }

    const uint32_t tableBase = DOPPLER_SLOT_BASE(gDopplerSlot) + DOPPLER_FLASH_TABLE_OFF;
    DOPPLER_Entry_t e0;
    PY25Q16_ReadBuffer(tableBase + (uint32_t)index * sizeof(DOPPLER_Entry_t),
                       &e0, sizeof(e0));
    if (e0.uplink < DOPPLER_FREQ_MIN || e0.uplink > DOPPLER_FREQ_MAX ||
        e0.downlink < DOPPLER_FREQ_MIN || e0.downlink > DOPPLER_FREQ_MAX)
    {
        return false;
    }

    const uint16_t nextIndex = index + 1u;
    if (ms == 0 || nextIndex >= entryCount || nextIndex >= DOPPLER_MAX_ENTRIES)
    {
        *pEntry = e0;
        return true;
    }

    DOPPLER_Entry_t e1;
    PY25Q16_ReadBuffer(tableBase + (uint32_t)nextIndex * sizeof(DOPPLER_Entry_t),
                       &e1, sizeof(e1));
    if (e1.uplink < DOPPLER_FREQ_MIN || e1.uplink > DOPPLER_FREQ_MAX ||
        e1.downlink < DOPPLER_FREQ_MIN || e1.downlink > DOPPLER_FREQ_MAX)
    {
        *pEntry = e0;
        return true;
    }

    // Linear interpolation: e0 + (e1 - e0) * ms / 1000.
    const int32_t deltaUp   = (int32_t)e1.uplink   - (int32_t)e0.uplink;
    const int32_t deltaDown = (int32_t)e1.downlink - (int32_t)e0.downlink;
    pEntry->uplink   = (uint32_t)((int32_t)e0.uplink   + (deltaUp   * (int32_t)ms) / 1000);
    pEntry->downlink = (uint32_t)((int32_t)e0.downlink + (deltaDown * (int32_t)ms) / 1000);

    const int32_t deltaAlt = (int32_t)e1.altitude_km - (int32_t)e0.altitude_km;
    const int32_t deltaDst = (int32_t)e1.distance_km - (int32_t)e0.distance_km;
    const int32_t deltaAz  = (int32_t)e1.azimuth_0_1deg - (int32_t)e0.azimuth_0_1deg;
    const int32_t deltaEl  = (int32_t)e1.elevation_0_1deg - (int32_t)e0.elevation_0_1deg;
    pEntry->altitude_km    = (uint16_t)((int32_t)e0.altitude_km    + (deltaAlt * (int32_t)ms) / 1000);
    pEntry->distance_km    = (uint16_t)((int32_t)e0.distance_km    + (deltaDst * (int32_t)ms) / 1000);
    pEntry->azimuth_0_1deg = (uint16_t)((int32_t)e0.azimuth_0_1deg + (deltaAz  * (int32_t)ms) / 1000);
    pEntry->elevation_0_1deg = (int16_t)((int32_t)e0.elevation_0_1deg + (deltaEl * (int32_t)ms) / 1000);

    return true;
}

void DOPPLER_EraseSlot(uint8_t Slot)
{
    if (Slot >= DOPPLER_SLOT_COUNT)
    {
        return;
    }
    const uint32_t base = DOPPLER_SLOT_BASE(Slot);
    for (uint32_t addr = base; addr < base + 4u * 0x1000u; addr += 0x1000u)
    {
        PY25Q16_SectorErase(addr);
    }
    if (Slot == gDopplerSlot)
    {
        gDopplerValid = false;
    }
}

bool DOPPLER_SlotGetInfo(uint8_t Slot, DOPPLER_Satellite_t *pOut)
{
    if (pOut == NULL)
    {
        return false;
    }
    DOPPLER_Satellite_t sat;
    if (!DOPPLER_LoadSlot(Slot, &sat))
    {
        return false;
    }
    memcpy(pOut, &sat, sizeof(sat));
    return true;
}

int DOPPLER_FindPassing(uint32_t now)
{
    DOPPLER_Satellite_t sat;
    for (uint8_t s = 0; s < DOPPLER_SLOT_COUNT; s++)
    {
        if (!DOPPLER_SlotGetInfo(s, &sat))
        {
            continue;
        }
        if (now >= sat.start_unix && now <= sat.start_unix + (uint32_t)sat.sum_time)
        {
            return (int)s;
        }
    }
    return -1;
}

int DOPPLER_FindNext(uint32_t now)
{
    int best = -1;
    uint32_t bestStart = UINT32_MAX;
    DOPPLER_Satellite_t sat;
    for (uint8_t s = 0; s < DOPPLER_SLOT_COUNT; s++)
    {
        if (!DOPPLER_SlotGetInfo(s, &sat))
        {
            continue;
        }
        if (sat.start_unix >= now && sat.start_unix < bestStart)
        {
            best = (int)s;
            bestStart = sat.start_unix;
        }
    }
    return best;
}

int DOPPLER_EraseExpired(uint32_t now)
{
    int erased = 0;
    DOPPLER_Satellite_t sat;
    for (uint8_t s = 0; s < DOPPLER_SLOT_COUNT; s++)
    {
        if (!DOPPLER_SlotGetInfo(s, &sat))
        {
            continue;
        }
        if (now > sat.start_unix + (uint32_t)sat.sum_time)
        {
            DOPPLER_EraseSlot(s);
            erased++;
        }
    }
    return erased;
}

bool DOPPLER_WriteSatellite(uint8_t Slot, const DOPPLER_Satellite_t *pSat)
{
    if (pSat == NULL || Slot >= DOPPLER_SLOT_COUNT)
    {
        return false;
    }

    DOPPLER_Satellite_t copy;
    memcpy(&copy, pSat, sizeof(copy));
    copy.crc8 = 0;
    copy.reserved = 0;
    copy.crc8 = DOPPLER_Crc8((const uint8_t *)&copy, 30);

    PY25Q16_WriteBuffer(DOPPLER_SLOT_BASE(Slot), &copy, sizeof(copy), false);

    // 数据完整性由 CRC8 保证 (DOPPLER_IsValid 校验), 无需写后回读。
    // 注: 写后立即用 DMA 回读 (SPI_ReadBuf) 数据不可靠, 不用它做校验。
    if (Slot == gDopplerSlot)
    {
        memcpy(&gDopplerSatellite, &copy, sizeof(copy));
        gDopplerValid = DOPPLER_IsValid(&gDopplerSatellite);
    }
    return true;
}

bool DOPPLER_WriteEntry(uint8_t Slot, uint16_t Index, const DOPPLER_Entry_t *pEntry)
{
    if (pEntry == NULL || Slot >= DOPPLER_SLOT_COUNT || Index >= DOPPLER_MAX_ENTRIES)
    {
        return false;
    }

    const uint32_t addr = DOPPLER_SLOT_BASE(Slot) + DOPPLER_FLASH_TABLE_OFF
                          + (uint32_t)Index * sizeof(DOPPLER_Entry_t);
    PY25Q16_WriteBuffer(addr, pEntry, sizeof(DOPPLER_Entry_t), false);
    return true;
}

#endif // ENABLE_FEAT_F4HWN_DOPPLER
