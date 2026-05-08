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

#ifndef APP_AIRCOPY_H
#define APP_AIRCOPY_H

#ifdef ENABLE_AIRCOPY

#include "driver/keyboard.h"

// ============================================================================
// General definitions
// ============================================================================

#define AIRCOPY_BLOCK_SIZE           0x0040u  // 64 bytes per AirCopy block
#define AIRCOPY_CHANNELS_PER_BANK    128
#define AIRCOPY_NUM_BANKS            MR_CHANNELS_MAX / AIRCOPY_CHANNELS_PER_BANK
#define AIRCOPY_CHANNEL_SIZE         16       // bytes per channel (freq/name)
#define AIRCOPY_BANK_SIZE_BYTES      0x1080u  // 0x800 (Freq) + 0x800 (Name) + 0x80 (Attr)
#define AIRCOPY_BAR_WIDTH            120      // Visible width of the progress gauge

// ============================================================================
// Segment write mode
// ============================================================================

/*
 * Defines how a segment must be written to EEPROM.
 *
 * - STRUCT: structured data (frequencies, names)
 * - BYTES : raw byte stream (attributes, settings, etc.)
 */
typedef enum {
    AIRCOPY_WRITE_STRUCT = 0,
    AIRCOPY_WRITE_BYTES  = 1,
} AIRCOPY_WriteMode_t;

// ============================================================================
// Transfer segment structure
// ============================================================================

/*
 * Describes a contiguous EEPROM region involved in AirCopy.
 * The write_mode defines how the RX side must write the data.
 */
typedef struct {
    uint16_t start_offset;
    uint16_t end_offset;
    AIRCOPY_WriteMode_t write_mode;
} AIRCOPY_Segment_t;

// ============================================================================
// Transfer map structure
// ============================================================================

/*
 * A transfer map is a collection of segments describing
 * one complete AirCopy operation (bank, settings, etc.).
 */
typedef struct {
    const AIRCOPY_Segment_t *segments;
    uint16_t num_segments;
    uint16_t total_blocks;
} AIRCOPY_TransferMap_t;

// ============================================================================
// AirCopy state
// ============================================================================

typedef enum {
    AIRCOPY_READY = 0,
    AIRCOPY_TRANSFER,
    AIRCOPY_COMPLETE
} AIRCOPY_State_t;

// ============================================================================
// Globals
// ============================================================================

extern AIRCOPY_State_t gAircopyState;
extern uint16_t        gAirCopyBlockNumber;
extern uint16_t        gErrorsDuringAirCopy;
extern bool            gAirCopyIsSendMode;

extern uint16_t        g_FSK_Buffer[36];

// ============================================================================
// API
// ============================================================================

bool AIRCOPY_SendMessage(void);
void AIRCOPY_StorePacket(void);
void AIRCOPY_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

const AIRCOPY_TransferMap_t* AIRCOPY_GetCurrentMap(void);

// XOR-obfuscate `count` words of g_FSK_Buffer starting at index 1.
// Self-inverse: applying twice restores the original buffer.
void AIRCOPY_Obfuscate(unsigned int count);

#endif // ENABLE_AIRCOPY
#endif // APP_AIRCOPY_H
