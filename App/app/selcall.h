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

/*
 * Selective calling (5-tone selcall: ZVEI-1, ZVEI-2, CCIR-1, CCIR-2), transmit
 * only, sent on demand.
 *
 * Each memory channel (and each VFO) can hold a selcall type and two 5-digit
 * codes, e.g. the "test" and "production" codes that open a repeater's logic.
 * The key actions "SELCALL 1" / "SELCALL 2" key up with the channel's normal TX
 * settings (CTCSS included), wait SELCALL_PRELOAD_MS, send the code and unkey.
 * Nothing is sent automatically.
 *
 * Tones, no gap between them; a digit equal to the previous tone is replaced by
 * the repeat tone:
 *            0    1    2    3    4    5    6    7    8    9   repeat  tone
 *   ZVEI  2400 1060 1160 1270 1400 1530 1670 1830 2000 2200           70 ms
 *     ZVEI-1 repeat 2600 Hz, ZVEI-2 repeat 970 Hz
 *   CCIR  1981 1124 1197 1275 1358 1446 1540 1640 1747 1860   2110
 *     CCIR-1 100 ms, CCIR-2 70 ms
 *
 * Storage: one 8-byte record per channel in each config bank, outside the
 * CHIRP program area:
 *   SELCALL_BASE + index * 8, index = MR channel 0..1023, then 1024 + band * 2 + vfo
 *   [0] type  [1..3] code 1 (24-bit LE)  [4..6] code 2  [7] reserved
 * Erased flash (0xFF) reads as type off and no code. Types 0-2 are the values
 * of the first ZVEI-only builds, so records written by them stay valid.
 *
 * Serial access (CHIRP): EEPROM-compatible addresses SELCALL_EEPROM_BASE
 * (0xD000) to 0xF06F map to this table in the active bank (driver/eeprom_compat.c).
 */

#ifndef APP_SELCALL_H
#define APP_SELCALL_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SELCALL_OFF = 0,
    SELCALL_ZVEI1,
    SELCALL_ZVEI2,
    SELCALL_CCIR1,
    SELCALL_CCIR2,
    SELCALL_TYPE_COUNT
};

#define SELCALL_DIGITS        5
#define SELCALL_CODE_MAX      99999u
#define SELCALL_CODE_NONE     100000u     /* menu value and API value for "no code" */
#define SELCALL_PRELOAD_MS    300u        /* carrier (+ CTCSS) before the tones */

/* Record table: physical address in each config bank, and the window of the
 * EEPROM-compatible map through which the serial link (CHIRP) reaches it. */
#define SELCALL_BASE          0x00B000u   /* physical, 3 sectors: 0xB000-0xDFFF   */
#define SELCALL_REC_SIZE      8u
#define SELCALL_RECORDS       (1024u + 14u)   /* MR channels, then 7 bands x 2 VFOs */
#define SELCALL_EEPROM_BASE   0xD000u     /* logical window 0xD000-0xF06F          */
#define SELCALL_EEPROM_END    (SELCALL_EEPROM_BASE + SELCALL_RECORDS * SELCALL_REC_SIZE)

/* Fields of a channel's selcall setting, in the order of the menus
 * MENU_SELCALL, MENU_SC_CD1, MENU_SC_CD2 (field = menu id - MENU_SELCALL). */
enum {
    SELCALL_F_TYPE = 0,                   /* SELCALL_OFF .. SELCALL_CCIR2          */
    SELCALL_F_CODE1,                      /* 0..99999, or SELCALL_CODE_NONE        */
    SELCALL_F_CODE2,
    SELCALL_FIELDS
};

/* Menu / display names of the types ("OFF", "ZVEI-1", ...). */
extern const char *const gSelCallTypeNames[SELCALL_TYPE_COUNT];

/* Pure: tone frequencies (Hz) for a code, repeat tone applied. Returns the
 * number of tones (SELCALL_DIGITS), or 0 if type or code is invalid. */
uint8_t  SELCALL_BuildTones(uint8_t type, uint32_t code, uint16_t *freqs);
/* Pure: tone duration (ms) of a type, 0 if invalid. */
uint16_t SELCALL_ToneMs(uint8_t type);

#ifdef ENABLE_FEAT_SELCALL
/* Per-channel storage. Get/Set act on the current TX channel (menus);
 * channel = CHANNEL_SAVE, records of VFO channels are per VFO (TX_VFO). */
int32_t SELCALL_GetField(uint8_t field);
void SELCALL_SetField(uint8_t field, int32_t value);
void SELCALL_Clear(uint16_t channel);
void SELCALL_Copy(uint16_t fromChannel, uint8_t fromVfo, uint16_t toChannel);
void SELCALL_EraseAll(void);

/* On-demand transmission. */
enum {
    SELCALL_TX_IDLE = 0,
    SELCALL_TX_ON_AIR,                    /* a selcall burst is on air             */
    SELCALL_TX_DONE                       /* burst sent: main loop must unkey      */
};
extern uint8_t gSelCallTx;                /* SELCALL_TX_*, nonzero until unkeyed   */
extern uint8_t gSelCallPending;           /* tones of the requested burst, 0 = none */
bool SELCALL_Request(uint8_t which);      /* which = 0 (code 1) or 1 (code 2)      */
static inline bool SELCALL_Pending(void) { return gSelCallPending != 0; }
static inline void SELCALL_Cancel(void)  { gSelCallPending = 0; }   /* TX refused: drop the burst */
void SELCALL_Transmit(void);              /* from FUNCTION_Transmit, blocking      */
#endif

#endif /* APP_SELCALL_H */
