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

// Extended AirCopy: after 0x0000..0x1E00, also send 0x2000..0x2080 (2 blocks)
#define AIRCOPY_MAIN_END        0x1E00u
#define AIRCOPY_EXTRA_START     0x2000u
#define AIRCOPY_EXTRA_END       0x2080u
#define AIRCOPY_BLOCK_SIZE      0x0040u // 64 bytes
#define AIRCOPY_MAIN_BLOCKS     (AIRCOPY_MAIN_END / AIRCOPY_BLOCK_SIZE) // 0x78
#define AIRCOPY_EXTRA_BLOCKS    ((AIRCOPY_EXTRA_END - AIRCOPY_EXTRA_START) / AIRCOPY_BLOCK_SIZE) // 2
#define AIRCOPY_TOTAL_BLOCKS    (AIRCOPY_MAIN_BLOCKS + AIRCOPY_EXTRA_BLOCKS)
#define AIRCOPY_BAR_WIDTH       120 // Visible width of the progress gauge for UI

enum AIRCOPY_State_t
{
    AIRCOPY_READY = 0,
    AIRCOPY_TRANSFER,
    AIRCOPY_COMPLETE
};

typedef enum AIRCOPY_State_t AIRCOPY_State_t;

extern AIRCOPY_State_t gAircopyState;
extern uint16_t        gAirCopyBlockNumber;
extern uint16_t        gErrorsDuringAirCopy;
extern uint8_t         gAirCopyIsSendMode;

extern uint16_t        g_FSK_Buffer[36];

bool AIRCOPY_SendMessage(void);
void AIRCOPY_StorePacket(void);
void AIRCOPY_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

#endif

#endif
