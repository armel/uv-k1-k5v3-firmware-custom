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
#define AIRCOPY_NUM_MAPS             (AIRCOPY_NUM_BANKS + 1u)  // banks + one settings map
#define AIRCOPY_ALL_INDEX            AIRCOPY_NUM_MAPS          // selection sentinel: send/receive everything
#define AIRCOPY_BAR_WIDTH            120      // Visible width of the progress gauge

// ============================================================================
// AirCopy state
// ============================================================================

typedef enum {
    AIRCOPY_READY = 0,
    AIRCOPY_TRANSFER,
    AIRCOPY_COMPLETE,
    AIRCOPY_FAILED
} AIRCOPY_State_t;

// ============================================================================
// Globals
// ============================================================================

extern AIRCOPY_State_t gAircopyState;
extern uint16_t        gAirCopyBlockNumber;
extern uint16_t        gErrorsDuringAirCopy;
extern bool            gAirCopyIsSendMode;
extern bool            gAircopyAll;          // All mode: banks + settings in one pass

extern uint16_t        g_FSK_Buffer[36];

// ============================================================================
// API
// ============================================================================

bool AIRCOPY_SendMessage(void);
void AIRCOPY_StorePacket(void);
void AIRCOPY_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
uint16_t AIRCOPY_GetTotalBlocks(void);
uint8_t  AIRCOPY_CurrentSliceMap(void);   // map index of the block in progress (All slice label)

// XOR-obfuscate `count` words of g_FSK_Buffer starting at index 1.
// Self-inverse: applying twice restores the original buffer.
void AIRCOPY_Obfuscate(unsigned int count);

#endif // ENABLE_AIRCOPY
#endif // APP_AIRCOPY_H
