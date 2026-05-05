/* Copyright 2026 Armel F4HWN
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
 */

#ifndef APP_BEAM_H
#define APP_BEAM_H

#ifdef ENABLE_FEAT_F4HWN_BEAM

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

typedef enum {
    BEAM_MODE_TX = 0,
    BEAM_MODE_RX
} BEAM_Mode_t;

typedef enum {
    BEAM_STATUS_READY = 0,
    BEAM_STATUS_TX_DONE,
    BEAM_STATUS_RX_WAIT,
    BEAM_STATUS_RX_SAVED,
    BEAM_STATUS_RX_FULL,
    BEAM_STATUS_ERROR,
    BEAM_STATUS_TX_WAIT
} BEAM_Status_t;

extern BEAM_Mode_t   gBeamMode;
extern BEAM_Status_t gBeamStatus;
extern uint16_t      gBeamCopiedChannel;
extern uint8_t       gBeamRxWordCount;
extern bool          gBeamActive;

void ACTION_Beam(void);
void BEAM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void BEAM_StorePacket(void);

#endif // ENABLE_FEAT_F4HWN_BEAM
#endif // APP_BEAM_H
