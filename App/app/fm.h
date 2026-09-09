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

#ifndef APP_FM_H
#define APP_FM_H

#ifdef ENABLE_FMRADIO

#include "misc.h"

#ifdef ENABLE_FMRADIO_EMBEDDED
#include "driver/keyboard.h"
#endif

#define FM_CHANNEL_UP   0x01
#define FM_CHANNEL_DOWN 0xFF

enum {
    FM_SCAN_OFF = 0U,
};

extern uint16_t          gFM_Channels[FM_CHANNELS_MAX];

bool    FM_CheckValidChannel(uint8_t Channel);
// returns first valid channel starting at Channel
uint8_t FM_FindNextChannel(uint8_t Channel, uint8_t Direction);
int     FM_ConfigureChannelState(void);
int     FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit);

#ifdef ENABLE_FMRADIO_EMBEDDED
extern bool              gFmRadioMode;
extern uint8_t           gFmRadioCountdown_500ms;
extern volatile uint16_t gFmPlayCountdown_10ms;
extern volatile int8_t   gFM_ScanState;
extern bool              gFM_AutoScan;
extern uint8_t           gFM_ChannelPosition;
extern bool              gFM_FoundFrequency;
extern uint16_t          gFM_RestoreCountdown_10ms;

void    FM_TurnOff(void);
void    FM_EraseChannels(void);

void    FM_Tune(uint16_t Frequency, int8_t Step, bool bFlag);
void    FM_PlayAndUpdate(void);

void    FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

void    FM_Play(void);
void    FM_Start(void);
#endif

#endif

#endif
