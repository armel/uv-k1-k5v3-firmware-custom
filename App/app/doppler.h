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
 * Automatic satellite Doppler tracking (table lookup architecture).
 *
 * A companion tool (web page, K5Web-like) pre-computes, from TLE orbital
 * data, the uplink/downlink frequencies (already Doppler-compensated) for
 * every second of one satellite pass, and writes them into the external
 * SPI Flash. This module only stores, validates and looks up that table,
 * using the current time supplied by the RTC driver.
 */

#ifndef APP_DOPPLER_H
#define APP_DOPPLER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

// External SPI Flash layout: 4 slots of 16 KB each (0x1E8000..0x1F8000).
// Sits immediately after the RF log (0x1E0000-0x1E8000) in the flash tail,
// clear of the settings area (<= 0x00A170), calibration (0x010000),
// boot logo (0x011000), the multiboot voice-data region (0x14C000-0x1E2520)
// and the RTC save block (0x1FB000).
#define DOPPLER_SLOT_COUNT       4u          // 4 independent satellite passes
#define DOPPLER_SLOT_SIZE        0x4000u     // 16 KB per slot (4 sectors)
#define DOPPLER_FLASH_BASE       0x1E8000u   // first slot base
#define DOPPLER_FLASH_TABLE_OFF  0x40u       // table offset inside a slot
#define DOPPLER_SLOT_BASE(Slot)  (DOPPLER_FLASH_BASE + (uint32_t)(Slot) * DOPPLER_SLOT_SIZE)
#define DOPPLER_MAX_ENTRIES      1020u       // (16 KB - 64 B) / 16 B, ~17 min pass

// One frequency table entry, stored every second of the pass.
typedef struct {
    uint32_t uplink;          // TX frequency, in 10 Hz units (e.g. 43850000 = 438.5 MHz)
    uint32_t downlink;        // RX frequency, in 10 Hz units
    uint16_t altitude_km;     // satellite altitude above sea level, km
    uint16_t distance_km;     // straight-line range from observer to satellite, km
    uint16_t azimuth_0_1deg;  // azimuth, 0..3600 (0.1 deg)
    int16_t  elevation_0_1deg; // elevation, -900..900 (0.1 deg); during pass >= 0
} DOPPLER_Entry_t;

// Satellite info block, 32 bytes, stored at the base of each slot
// (DOPPLER_SLOT_BASE(Slot)).
// Field order keeps start_unix (u32) 4-byte aligned so there is NO padding:
// sizeof(DOPPLER_Satellite_t) == 32 exactly (the UART size check depends on it).
// Valid data: name[9] == 0, printable name[0], CRC8 over the first 30
// bytes matching crc8, sum_time > 0.
typedef struct {
    uint32_t start_unix;     // 0..3   pass start, seconds since 2000-01-01 00:00:00 Beijing
                             //        wall clock (UTC+8) - same base as the user-entered RTC
                             //        time, NOT UTC. The web tool adds the +8h on its side.
    char     name[10];       // 4..13  satellite name, <= 9 chars + '\0'
    uint8_t  start_time[6];  // 14..19 pass start: year (2000-based)/month/day/hour/minute/second
                             //        (Beijing wall clock, informational only - tracking uses start_unix)
    uint8_t  end_time[6];    // 20..25 pass end, same layout
    uint16_t sum_time;       // 26..27 total pass duration in seconds
    uint16_t send_ctcss;     // 28..29 TX sub-audio tone in Hz/10 (0 = none)
    uint8_t  crc8;           // 30     CRC-8 (poly 0x07) of the first 30 bytes
    uint8_t  reserved;       // 31     0
} DOPPLER_Satellite_t;

// Loads and validates slot 0. Call once at startup.
void DOPPLER_Init(void);

// Selects the active slot (0..3): reloads and validates its satellite block.
// Returns true when the slot holds valid data (DOPPLER_HasData() reflects it).
bool DOPPLER_SelectSlot(uint8_t Slot);

// Current active slot number (0..3).
uint8_t DOPPLER_GetSlot(void);

// True when a valid satellite pass is stored in the active slot.
bool DOPPLER_HasData(void);

// True when the given slot (0..3) holds a valid pass. Read-only: does not
// change the active slot.
bool DOPPLER_SlotHasData(uint8_t Slot);

// Diagnostics for the "ALL SLOTS EMPTY" screen: dumps the raw satellite
// block fields of one slot so a failed validation can be pinpointed.
typedef struct {
    uint8_t  name0;       // name[0] as stored
    uint8_t  name9;       // name[9] as stored (must be 0)
    uint16_t sum_time;    // as stored (must be 1..1019)
    uint8_t  crc_calc;    // CRC-8 recomputed over the stored 30 bytes
    uint8_t  crc_stored;  // crc8 field as stored
} DOPPLER_Diag_t;
bool DOPPLER_DiagSlot(uint8_t Slot, DOPPLER_Diag_t *pDiag);

// Returns a pointer to the in-RAM copy of the satellite info block.
const DOPPLER_Satellite_t *DOPPLER_GetSatellite(void);

// Converts a 6-byte time (year 2000-based / month / day / hour / minute /
// second) into seconds since 2000-01-01 00:00:00.
uint32_t DOPPLER_UnixTime(const uint8_t t[6]);

// Leap-year helper, shared with doppler_mode.c.
bool DOPPLER_IsLeapYear(uint8_t Year2000);

// Inverse of DOPPLER_UnixTime: seconds since 2000-01-01 00:00:00 (Beijing
// wall clock base) back into [year/month/day/hour/minute/second].
void DOPPLER_UnixToDate(uint32_t Seconds, uint8_t t[6]);

// Fetches the frequency/orbit table entry covering "unixNow". When ms==0
// it returns the exact entry (no interpolation), so it can also replace the
// old DOPPLER_GetEntry() call site.  For ms>0 it linearly interpolates
// between the entries surrounding the current time using ms (0..999) as the
// fractional part of the current second, giving smooth sub-second tracking
// when called from a fast periodic hook (e.g. 100 ms).
bool DOPPLER_GetEntryInterpolated(int32_t unixNow, uint16_t ms, DOPPLER_Entry_t *pEntry);

// Erases one slot (4 sectors).
void DOPPLER_EraseSlot(uint8_t Slot);

// --- Automatic pass scheduling helpers (used by doppler_mode.c) ---

// Reads and validates one slot's satellite block without changing the
// active slot. Returns true and copies the block when the slot is valid.
bool DOPPLER_SlotGetInfo(uint8_t Slot, DOPPLER_Satellite_t *pOut);

// Returns the slot whose pass window covers "now"
// (start_unix <= now <= start_unix + sum_time), or -1 when none.
int DOPPLER_FindPassing(uint32_t now);

// Returns the valid slot with the smallest start_unix >= now (the next
// upcoming pass), or -1 when none.
int DOPPLER_FindNext(uint32_t now);

// Erases every slot whose pass window has fully elapsed
// (now > start_unix + sum_time). Returns the number of erased slots.
int DOPPLER_EraseExpired(uint32_t now);

// Writes the satellite info block (CRC8 is computed internally).
bool DOPPLER_WriteSatellite(uint8_t Slot, const DOPPLER_Satellite_t *pSat);

// Writes one frequency table entry. Used by the serial programming tool.
bool DOPPLER_WriteEntry(uint8_t Slot, uint16_t Index, const DOPPLER_Entry_t *pEntry);

#endif // ENABLE_FEAT_F4HWN_DOPPLER

#endif // APP_DOPPLER_H
