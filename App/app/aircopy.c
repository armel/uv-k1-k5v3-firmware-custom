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

#ifdef ENABLE_AIRCOPY

#include "app/aircopy.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/crc.h"
#include "driver/eeprom.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/ui.h"
#include "settings.h"
#include <stddef.h>

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "screenshot.h"
#endif

static const uint16_t Obfuscation[8] = { 0x6C16, 0xE614, 0x912E, 0x400D, 0x3521, 0x40D5, 0x0313, 0x80E9 };

AIRCOPY_State_t gAircopyState;
uint16_t gAirCopyBlockNumber;
uint16_t gErrorsDuringAirCopy;
uint8_t gAirCopyIsSendMode;

uint16_t g_FSK_Buffer[36];

// ============================================================================
// Transfer Maps Definition
// ============================================================================

#define AIRCOPY_BANK_SEGMENTS(bank)                     \
{                                                       \
    { 0x0000 + (bank)*0x0800, 0x0000 + (bank)*0x0800 + 0x0800, AIRCOPY_WRITE_STRUCT }, \
    { 0x4000 + (bank)*0x0800, 0x4000 + (bank)*0x0800 + 0x0800, AIRCOPY_WRITE_STRUCT }, \
    { 0x8000 + (bank)*0x0100, 0x8000 + (bank)*0x0100 + 0x0100, AIRCOPY_WRITE_BYTES  }, \
}

#define AIRCOPY_STD_MAP(seg_array) \
{                                  \
    .segments = seg_array,         \
    .num_segments = 3,             \
    .total_blocks = 68             \
}

// total_blocks = 68 (blocs of 64 bytes) because (16 bytes + 16 bytes + 2 bytes) * 128 = 4352 / 64 = 68

#define DECLARE_AIRCOPY_BANK(n)                                     \
    static const AIRCOPY_Segment_t AIRCOPY_Segments_Bank##n[] =     \
        AIRCOPY_BANK_SEGMENTS(n);                                   \
                                                                    \
    static const AIRCOPY_TransferMap_t AIRCOPY_Map_Bank##n =        \
        AIRCOPY_STD_MAP(AIRCOPY_Segments_Bank##n);

DECLARE_AIRCOPY_BANK(0)
DECLARE_AIRCOPY_BANK(1)
#if AIRCOPY_NUM_BANKS >= 4 // if 512 MR CHANNEL
    DECLARE_AIRCOPY_BANK(2)
    DECLARE_AIRCOPY_BANK(3)
#endif
#if AIRCOPY_NUM_BANKS >= 6 // if 758 MR CHANNEL
    DECLARE_AIRCOPY_BANK(4)
    DECLARE_AIRCOPY_BANK(5)
#endif
#if AIRCOPY_NUM_BANKS >= 8 // if 1024 MR CHANNEL
    DECLARE_AIRCOPY_BANK(6)
    DECLARE_AIRCOPY_BANK(7)
#endif

// For settings only

static const AIRCOPY_Segment_t AIRCOPY_Segments_Settings[] = {
    { 0xA000, 0xA160, AIRCOPY_WRITE_BYTES },
};

static const AIRCOPY_TransferMap_t AIRCOPY_Map_Settings = {
    .segments = AIRCOPY_Segments_Settings,
    .num_segments = 1,
    .total_blocks = 6
};

// Finally

static const AIRCOPY_TransferMap_t *AIRCOPY_AvailableMaps[] = {
    &AIRCOPY_Map_Bank0,
    &AIRCOPY_Map_Bank1,
    #if AIRCOPY_NUM_BANKS >= 4 // if 512 MR CHANNEL
        &AIRCOPY_Map_Bank2,
        &AIRCOPY_Map_Bank3,
    #endif
    #if AIRCOPY_NUM_BANKS >= 6 // if 758 MR CHANNEL
        &AIRCOPY_Map_Bank4,
        &AIRCOPY_Map_Bank5,
    #endif
    #if AIRCOPY_NUM_BANKS >= 8 // if 1024 MR CHANNEL
        &AIRCOPY_Map_Bank6,
        &AIRCOPY_Map_Bank7,
    #endif
    &AIRCOPY_Map_Settings,
};

#define AIRCOPY_NUM_MAPS (sizeof(AIRCOPY_AvailableMaps) / sizeof(AIRCOPY_AvailableMaps[0]))

// ============================================================================
// Helper Functions
// ============================================================================

const AIRCOPY_TransferMap_t* AIRCOPY_GetCurrentMap(void)
{
    if (gAircopyCurrentMapIndex >= AIRCOPY_NUM_MAPS) {
        gAircopyCurrentMapIndex = 0;
    }
    return AIRCOPY_AvailableMaps[gAircopyCurrentMapIndex];
}

static void AIRCOPY_clear()
{
    for (uint8_t i = 0; i < 15; i++)
    {
        crc[i] = 0;
    }
    #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
        getScreenShot(true);
    #endif
}

static inline const AIRCOPY_Segment_t *AIRCOPY_FindSegmentForOffset(uint16_t off)
{
    const AIRCOPY_TransferMap_t *map = AIRCOPY_GetCurrentMap();

    for (uint16_t i = 0; i < map->num_segments; i++)
    {
        const AIRCOPY_Segment_t *seg = &map->segments[i];

        if (off >= seg->start_offset && off < seg->end_offset)
            return seg;
    }

    return NULL;
}

static inline void AIRCOPY_CheckComplete(void)
{
    const AIRCOPY_TransferMap_t *map = AIRCOPY_GetCurrentMap();
    uint16_t done = gAirCopyBlockNumber + gErrorsDuringAirCopy;

    if (done >= map->total_blocks)
    {
        gAircopyState = AIRCOPY_COMPLETE;
#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
        getScreenShot(false);
#endif
    }
}

// ============================================================================
// Send/Receive Functions
// ============================================================================

bool AIRCOPY_SendMessage(void)
{
    static uint8_t gAircopySendCountdown = 1;
    static uint16_t CurrentOffset = 0;
    static uint16_t CurrentSegmentIndex = 0;

    if (gAircopyState != AIRCOPY_TRANSFER) {
        return 1;
    }

    if (--gAircopySendCountdown) {
        return 1;
    }

    const AIRCOPY_TransferMap_t *map = AIRCOPY_GetCurrentMap();

    // Initialize on first call
    if (gAirCopyBlockNumber == 0) {
        CurrentSegmentIndex = 0;
        CurrentOffset = map->segments[0].start_offset;
    }

    // Advance to next segment if current is done
    while (CurrentSegmentIndex < map->num_segments &&
           CurrentOffset >= map->segments[CurrentSegmentIndex].end_offset)
    {
        CurrentSegmentIndex++;
        if (CurrentSegmentIndex < map->num_segments) {
            CurrentOffset = map->segments[CurrentSegmentIndex].start_offset;
        }
    }

    // Check if transfer is complete
    if (CurrentSegmentIndex >= map->num_segments) {
        gAircopyState = AIRCOPY_COMPLETE;
        #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            getScreenShot(false);
        #endif
        return 0;
    }

    // Send data from current offset
    g_FSK_Buffer[1] = CurrentOffset;
    EEPROM_ReadBuffer(CurrentOffset, &g_FSK_Buffer[2], 64);

    g_FSK_Buffer[34] = CRC_Calculate(&g_FSK_Buffer[1], 2 + 64);

    for (unsigned int i = 0; i < 34; i++) {
        g_FSK_Buffer[i + 1] ^= Obfuscation[i % 8];
    }

    RADIO_SetTxParameters();

    BK4819_SendFSKData(g_FSK_Buffer);
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    CurrentOffset += 64;
    gAirCopyBlockNumber++;
    gAircopySendCountdown = 30;

    return 0;
}

void AIRCOPY_StorePacket(void)
{
    if (gFSKWriteIndex < 36) {
        return;
    }

    gFSKWriteIndex = 0;
    gUpdateDisplay = true;
    uint16_t Status = BK4819_ReadRegister(BK4819_REG_0B);
    BK4819_PrepareFSKReceive();

    if ((Status & 0x0010U) != 0 || g_FSK_Buffer[0] != 0xABCD || g_FSK_Buffer[35] != 0xDCBA) {
        gErrorsDuringAirCopy++;

        BK4819_ResetFSK();           // <- important
        BK4819_PrepareFSKReceive();  // <- re-arm proprement

        AIRCOPY_CheckComplete();
        return;
    }

    for (unsigned int i = 0; i < 34; i++) {
        g_FSK_Buffer[i + 1] ^= Obfuscation[i % 8];
    }

    uint16_t Crc = CRC_Calculate(&g_FSK_Buffer[1], 2 + 64);
    if (g_FSK_Buffer[34] != Crc) {
        gErrorsDuringAirCopy++;
        AIRCOPY_CheckComplete();
        return;
    }

    uint16_t Offset = g_FSK_Buffer[1];
    const AIRCOPY_TransferMap_t *map = AIRCOPY_GetCurrentMap();

    // Validate offset is in the map
    bool validOffset = false;
    for (uint16_t i = 0; i < map->num_segments; i++) {
        if (Offset >= map->segments[i].start_offset && Offset < map->segments[i].end_offset) {
            validOffset = true;
            break;
        }
    }

    if (!validOffset) {
        gErrorsDuringAirCopy++;
        AIRCOPY_CheckComplete();
        return;
    }

    const AIRCOPY_Segment_t *seg = AIRCOPY_FindSegmentForOffset(Offset);

    if (seg == NULL) {
        gErrorsDuringAirCopy++;
        AIRCOPY_CheckComplete();
        return;
    }

    if (seg->write_mode == AIRCOPY_WRITE_BYTES)
    {
        /* Raw bytes stream, written in 8-byte EEPROM chunks */
        const uint8_t *p8 = (const uint8_t *)&g_FSK_Buffer[2];

        for (unsigned int i = 0; i < 8; i++)
        {
            EEPROM_WriteBuffer(Offset + (i * 8), p8 + (i * 8));
        }
    }
    else
    {
        /* Structured data path (kept as-is) */
        const uint16_t *pData = &g_FSK_Buffer[2];

        for (unsigned int i = 0; i < 8; i++)
        {
            EEPROM_WriteBuffer(Offset, pData);
            pData += 4;
            Offset += 8;
        }
    }

    gAirCopyBlockNumber++;
    AIRCOPY_CheckComplete();
}

// ============================================================================
// Key Processing
// ============================================================================

static void AIRCOPY_Key_DIGITS(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    INPUTBOX_Append(Key);

    gRequestDisplayScreen = DISPLAY_AIRCOPY;

    if (gInputBoxIndex < 6) {
#ifdef ENABLE_VOICE
        gAnotherVoiceID = (VOICE_ID_t)Key;
#endif
        return;
    }

    gInputBoxIndex = 0;
    uint32_t Frequency = StrToUL(INPUTBOX_GetAscii()) * 100;

    for (unsigned int i = 0; i < BAND_N_ELEM; i++) {
        if (Frequency < frequencyBandTable[i].lower || Frequency >= frequencyBandTable[i].upper) {
            continue;
        }

        if (TX_freq_check(Frequency)) {
            continue;
        }

#ifdef ENABLE_VOICE
        gAnotherVoiceID = (VOICE_ID_t)Key;
#endif

        Frequency = FREQUENCY_RoundToStep(Frequency, gRxVfo->StepFrequency);
        gRxVfo->Band = i;
        gRxVfo->freq_config_RX.Frequency = Frequency;
        gRxVfo->freq_config_TX.Frequency = Frequency;
        RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
        gCurrentVfo = gRxVfo;
        RADIO_SetupRegisters(true);
        BK4819_SetupAircopy();
        BK4819_ResetFSK();
        return;
    }

    gRequestDisplayScreen = DISPLAY_AIRCOPY;
}

static void AIRCOPY_Key_EXIT(bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    if (gInputBoxIndex == 0) {
        gAircopyStep = 1;
        gFSKWriteIndex = 0;
        gAirCopyBlockNumber = 0;
        gInputBoxIndex = 0;
        gErrorsDuringAirCopy = lErrorsDuringAirCopy = 0;
        gAirCopyIsSendMode = 0;

        AIRCOPY_clear();

        BK4819_PrepareFSKReceive();

        gAircopyState = AIRCOPY_TRANSFER;
    } else {
        gInputBox[--gInputBoxIndex] = 10;
    }

    gRequestDisplayScreen = DISPLAY_AIRCOPY;
}

static void AIRCOPY_Key_MENU(bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    gAircopyStep = 1;
    gFSKWriteIndex = 0;
    gAirCopyBlockNumber = 0;
    gInputBoxIndex = 0;
    gAirCopyIsSendMode = 1;
    g_FSK_Buffer[0] = 0xABCD;
    g_FSK_Buffer[1] = 0;
    g_FSK_Buffer[35] = 0xDCBA;

    AIRCOPY_clear();

    GUI_DisplayScreen();

    gAircopyState = AIRCOPY_TRANSFER;
}

static void AIRCOPY_Key_UP_DOWN(bool bKeyPressed, bool bKeyHeld, int8_t Direction)
{
    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    switch(Direction)
    {
        case 1:
            gAircopyCurrentMapIndex = (gAircopyCurrentMapIndex + 1) % AIRCOPY_NUM_MAPS;
            break;
        case -1:
            gAircopyCurrentMapIndex = (gAircopyCurrentMapIndex + AIRCOPY_NUM_MAPS - 1) % AIRCOPY_NUM_MAPS;
            break;
    }

    gRequestDisplayScreen = DISPLAY_AIRCOPY;
}

void AIRCOPY_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    switch (Key) {
    case KEY_0:
    case KEY_1:
    case KEY_2:
    case KEY_3:
    case KEY_4:
    case KEY_5:
    case KEY_6:
    case KEY_7:
    case KEY_8:
    case KEY_9:
        AIRCOPY_Key_DIGITS(Key, bKeyPressed, bKeyHeld);
        break;
    case KEY_MENU:
        AIRCOPY_Key_MENU(bKeyPressed, bKeyHeld);
        break;
    case KEY_EXIT:
        AIRCOPY_Key_EXIT(bKeyPressed, bKeyHeld);
        break;
    case KEY_UP:
        if(gEeprom.SET_NAV == 0)
            AIRCOPY_Key_UP_DOWN(bKeyPressed, bKeyHeld, -1);
        else
            AIRCOPY_Key_UP_DOWN(bKeyPressed, bKeyHeld, 1);
        break;
    case KEY_DOWN:
        if(gEeprom.SET_NAV == 0)
            AIRCOPY_Key_UP_DOWN(bKeyPressed, bKeyHeld, 1);
        else
            AIRCOPY_Key_UP_DOWN(bKeyPressed, bKeyHeld, -1);
        break;
    default:
        break;
    }
}

#endif