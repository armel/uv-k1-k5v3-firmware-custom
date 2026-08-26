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
 * every 2 seconds of one satellite pass, and writes them into the external
 * SPI Flash. This module only stores, validates and looks up that table,
 * using the current time supplied by the RTC driver.
 */

#ifndef APP_DOPPLER_H
#define APP_DOPPLER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

// External SPI Flash layout (0x1D0000..0x1D4000, 4 sectors).
// Kept clear of the settings area (<= 0x00A170), calibration (0x010000),
// boot logo (0x011000) and the RF log (0x1E0000).
#define DOPPLER_FLASH_BASE       0x1D0000u   // satellite info block
#define DOPPLER_FLASH_TABLE      0x1D0040u   // frequency table
#define DOPPLER_MAX_ENTRIES      1920u       // 32 min pass, one entry / 2 s

// One frequency table entry, stored every 2 seconds of the pass.
typedef struct {
    uint32_t uplink;    // TX frequency, in 10 Hz units (e.g. 43850000 = 438.5 MHz)
    uint32_t downlink;  // RX frequency, in 10 Hz units
} DOPPLER_Entry_t;

// Satellite info block, 32 bytes, stored at DOPPLER_FLASH_BASE.
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

// Loads and validates the satellite info block. Call once at startup.
void DOPPLER_Init(void);

// True when a valid satellite pass is stored in Flash.
bool DOPPLER_HasData(void);

// Returns a pointer to the in-RAM copy of the satellite info block.
const DOPPLER_Satellite_t *DOPPLER_GetSatellite(void);

// Converts a 6-byte time (year 2000-based / month / day / hour / minute /
// second) into seconds since 2000-01-01 00:00:00.
uint32_t DOPPLER_UnixTime(const uint8_t t[6]);

// Inverse of DOPPLER_UnixTime: seconds since 2000-01-01 00:00:00 (Beijing
// wall clock base) back into [year/month/day/hour/minute/second].
void DOPPLER_UnixToDate(uint32_t Seconds, uint8_t t[6]);

// Fetches the frequency table entry covering "unixNow". Returns true when
// the pass is ongoing and a valid entry exists.
bool DOPPLER_GetEntry(int32_t unixNow, DOPPLER_Entry_t *pEntry);

// Erases the whole Doppler area (4 sectors).
void DOPPLER_Erase(void);

// Writes the satellite info block (CRC8 is computed internally).
bool DOPPLER_WriteSatellite(const DOPPLER_Satellite_t *pSat);

// Writes one frequency table entry. Used by the serial programming tool.
bool DOPPLER_WriteEntry(uint16_t Index, const DOPPLER_Entry_t *pEntry);

#endif // ENABLE_FEAT_F4HWN_DOPPLER

#endif // APP_DOPPLER_H
