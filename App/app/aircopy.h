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
#define AIRCOPY_BLOCK_WORDS          (AIRCOPY_BLOCK_SIZE / 2u)  // 32 FSK words per block

// Every forward frame stays 100 words long. HASH carries 24 per-block CRC32
// values and the selected map; the receiver replies with an ACK carrying a
// 24-bit difference mask or a rejection when the selections do not match.
// DATA retains the proven [type][header][up to 3 blocks][CRC16][END] layout,
// with unused blocks zero-padded. Only blocks selected by the mask are sent.
// Reverse ACK frames are eight words long. A missing ACK triggers a retry.
//
// This wire format is not compatible with earlier AirCopy versions. Both radios
// must run the same firmware.
#define AIRCOPY_BLOCKS_PER_FRAME     3u
#define AIRCOPY_DATA_HEADER_WORDS    2u
#define AIRCOPY_DATA_WORDS           (AIRCOPY_DATA_HEADER_WORDS + AIRCOPY_BLOCKS_PER_FRAME * AIRCOPY_BLOCK_WORDS + 2u)
#define AIRCOPY_CTRL_WORDS           8u   // multiple of the 4-word RX FIFO threshold
#define AIRCOPY_FRAME_WORDS_MAX      AIRCOPY_DATA_WORDS
#ifdef ENABLE_AIRCOPY_UART
// Enable exactly one of the validated cable rates below:
// #define AIRCOPY_UART_BAUD_RATE       38400u
// #define AIRCOPY_UART_BAUD_RATE       115200u
// #define AIRCOPY_UART_BAUD_RATE       230400u
#define AIRCOPY_UART_BAUD_RATE       460800u
#define AIRCOPY_UART_DEFAULT_BAUD    38400u
#endif

#if AIRCOPY_DATA_WORDS > 128u
#error AirCopy DATA frame exceeds the radio TX FIFO
#endif

#define AIRCOPY_CHANNELS_PER_BANK    128
#define AIRCOPY_NUM_BANKS            MR_CHANNELS_MAX / AIRCOPY_CHANNELS_PER_BANK
#define AIRCOPY_NUM_MAPS             (AIRCOPY_NUM_BANKS + 1u)  // banks + one settings map
#define AIRCOPY_ALL_INDEX            AIRCOPY_NUM_MAPS          // selection sentinel: send/receive everything
#ifdef ENABLE_AIRCOPY_FLASH
#define AIRCOPY_FLASH_INDEX          (AIRCOPY_ALL_INDEX + 1u)   // full external-flash clone
#define AIRCOPY_FLASH_SECTORS        512u                        // 2 MiB / 4 KiB
#endif
#define AIRCOPY_BANK_BLOCKS          68u
#define AIRCOPY_SETTINGS_BLOCKS      12u
#define AIRCOPY_ALL_BLOCKS           (AIRCOPY_NUM_BANKS * AIRCOPY_BANK_BLOCKS + AIRCOPY_SETTINGS_BLOCKS)
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

typedef enum {
    AIRCOPY_TRANSPORT_AIR = 0,
#ifdef ENABLE_AIRCOPY_UART
    AIRCOPY_TRANSPORT_UART,
#endif
} AIRCOPY_Transport_t;

// ============================================================================
// Globals
// ============================================================================

extern AIRCOPY_State_t gAircopyState;
extern uint16_t        gAirCopyBlockNumber;
extern uint16_t        gErrorsDuringAirCopy;
extern bool            gAirCopyIsSendMode;
extern bool            gAircopyAll;          // All mode: banks + settings in one pass
extern AIRCOPY_Transport_t gAircopyTransport;

extern uint16_t        g_FSK_Buffer[AIRCOPY_FRAME_WORDS_MAX];
extern uint8_t         gFskRxExpectedWords;   // frame length the current role expects on RX

// ============================================================================
// API
// ============================================================================

bool AIRCOPY_SendMessage(void);
void AIRCOPY_StorePacket(void);
#ifdef ENABLE_AIRCOPY_UART
void AIRCOPY_StoreUartPacket(const void *data, uint8_t words);
#endif
void AIRCOPY_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
uint16_t AIRCOPY_GetTotalBlocks(void);
bool AIRCOPY_PixelWasCopied(uint8_t col);
uint8_t  AIRCOPY_CurrentSliceMap(void);   // map index of the block in progress (All slice label)
bool AIRCOPY_UsesUart(void);
bool AIRCOPY_IsFlash(void);

// XOR-obfuscate `count` words of g_FSK_Buffer starting at index 1.
// Self-inverse: applying twice restores the original buffer.
void AIRCOPY_Obfuscate(unsigned int count);

#endif // ENABLE_AIRCOPY
#endif // APP_AIRCOPY_H
