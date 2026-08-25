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
#include "driver/system.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/ui.h"
#include "settings.h"
#include <stddef.h>
#include <string.h>

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
#include "k5viewer.h"
#endif

static const uint16_t Obfuscation[8] = { 0x6C16, 0xE614, 0x912E, 0x400D, 0x3521, 0x40D5, 0x0313, 0x80E9 };

AIRCOPY_State_t gAircopyState;
uint16_t gAirCopyBlockNumber;
uint16_t gErrorsDuringAirCopy;
bool     gAirCopyIsSendMode;

uint16_t g_FSK_Buffer[36];

// Stop-and-wait protocol. Every frame keeps the original 64-byte payload:
// DATA is acknowledged only after storage, ACK confirms that offset, and
// RESEND requests the same offset immediately instead of waiting for timeout.
#define AIRCOPY_PACKET_DATA          0xABCDu
#define AIRCOPY_PACKET_ACK           0xABCEu
#define AIRCOPY_PACKET_RESEND        0xABCFu
#define AIRCOPY_PACKET_END           0xDCBAu
#define AIRCOPY_ACK_TIMEOUT_10MS     400u
#define AIRCOPY_RX_TIMEOUT_10MS      2000u
#define AIRCOPY_RX_LINGER_10MS       500u
#define AIRCOPY_MAX_RETRIES          3u

static uint16_t AircopyCountdown;
static uint8_t  AircopyRetries;

#define AIRCOPY_NUM_MAPS        (AIRCOPY_NUM_BANKS + 1u)
#define AIRCOPY_BANK_BLOCKS     68u
#define AIRCOPY_SETTINGS_BLOCKS 12u

// ============================================================================
// Helper Functions
// ============================================================================

uint8_t AIRCOPY_GetTotalBlocks(void)
{
    return gAircopyCurrentMapIndex == AIRCOPY_NUM_BANKS
         ? AIRCOPY_SETTINGS_BLOCKS
         : AIRCOPY_BANK_BLOCKS;
}

static uint16_t AIRCOPY_GetBlockOffset(uint16_t block)
{
    if (gAircopyCurrentMapIndex == AIRCOPY_NUM_BANKS)
    {
        // Settings: 6 blocks at 0xA000, 2 at 0x880E and 4 at 0x9000.
        if (block < 6u)
            return 0xA000u + block * AIRCOPY_BLOCK_SIZE;
        if (block < 8u)
            return 0x880Eu + (block - 6u) * AIRCOPY_BLOCK_SIZE;
        return 0x9000u + (block - 8u) * AIRCOPY_BLOCK_SIZE;
    }

    // A bank contains 32 frequency, 32 name and 4 attribute blocks.
    const uint16_t channelOffset = gAircopyCurrentMapIndex * 0x0800u;
    if (block < 32u)
        return channelOffset + block * AIRCOPY_BLOCK_SIZE;
    if (block < 64u)
        return 0x4000u + channelOffset + (block - 32u) * AIRCOPY_BLOCK_SIZE;
    return 0x8000u + gAircopyCurrentMapIndex * 0x0100u
         + (block - 64u) * AIRCOPY_BLOCK_SIZE;
}

static void AIRCOPY_clear()
{
    #ifdef ENABLE_FEAT_F4HWN_K5VIEWER
        K5VIEWER_Update(true);
    #endif
}

static void AIRCOPY_Finish(AIRCOPY_State_t state)
{
    AircopyCountdown = 0;
    gAircopyState = state;
    gUpdateDisplay = true;
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    K5VIEWER_Update(false);
#endif
}

void AIRCOPY_Obfuscate(unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) {
        g_FSK_Buffer[i + 1] ^= Obfuscation[i % 8];
    }
}

static void AIRCOPY_TransmitBuffer(void)
{
    // Both sides need time to leave TX and re-arm FSK RX before the reply.
    SYSTEM_DelayMs(50);
    RADIO_SetTxParameters();
    BK4819_SendFSKData(g_FSK_Buffer);
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
}

static void AIRCOPY_FinalizeAndSend(void)
{
    g_FSK_Buffer[34] = CRC_Calculate(&g_FSK_Buffer[0],
                                     4 + AIRCOPY_BLOCK_SIZE);
    g_FSK_Buffer[35] = AIRCOPY_PACKET_END;
    AIRCOPY_Obfuscate(34);
    AIRCOPY_TransmitBuffer();
    gFSKWriteIndex = 0;
    BK4819_PrepareFSKReceive();
}

static void AIRCOPY_SendControl(uint16_t type, uint16_t offset)
{
    g_FSK_Buffer[0] = type;
    g_FSK_Buffer[1] = offset;
    memset(&g_FSK_Buffer[2], 0, AIRCOPY_BLOCK_SIZE);
    AIRCOPY_FinalizeAndSend();
}

static void AIRCOPY_RequestResend(void)
{
    gErrorsDuringAirCopy++;
    gUpdateDisplay = true;
    AircopyCountdown = AIRCOPY_RX_TIMEOUT_10MS;
    AIRCOPY_SendControl(AIRCOPY_PACKET_RESEND,
                        AIRCOPY_GetBlockOffset(gAirCopyBlockNumber));
}

static bool AIRCOPY_Retry(void)
{
    if (AircopyRetries >= AIRCOPY_MAX_RETRIES)
    {
        AIRCOPY_Finish(AIRCOPY_FAILED);
        return false;
    }

    AircopyRetries++;
    gErrorsDuringAirCopy++;
    gUpdateDisplay = true;
    AircopyCountdown = 0;
    return true;
}

// ============================================================================
// Send/Receive Functions
// ============================================================================

bool AIRCOPY_SendMessage(void)
{
    if (gAircopyState != AIRCOPY_TRANSFER) {
        return 1;
    }

    if (!gAirCopyIsSendMode)
    {
        if (AircopyCountdown != 0 && --AircopyCountdown == 0)
        {
            AIRCOPY_Finish(gAirCopyBlockNumber >= AIRCOPY_GetTotalBlocks()
                           ? AIRCOPY_COMPLETE
                           : AIRCOPY_FAILED);
            return 0;
        }
        return 1;
    }

    if (AircopyCountdown != 0)
    {
        if (--AircopyCountdown != 0)
            return 1;
        if (!AIRCOPY_Retry())
            return 0;
    }

    const uint16_t currentOffset = AIRCOPY_GetBlockOffset(gAirCopyBlockNumber);
    g_FSK_Buffer[0] = AIRCOPY_PACKET_DATA;
    g_FSK_Buffer[1] = currentOffset;
    EEPROM_ReadBuffer(currentOffset, &g_FSK_Buffer[2], AIRCOPY_BLOCK_SIZE);
    AIRCOPY_FinalizeAndSend();
    AircopyCountdown = AIRCOPY_ACK_TIMEOUT_10MS;

    return 1;
}

void AIRCOPY_StorePacket(void)
{
    if (gFSKWriteIndex < 36) {
        return;
    }

    gFSKWriteIndex = 0;
    const uint16_t status = BK4819_ReadRegister(BK4819_REG_0B);
    const uint16_t type = g_FSK_Buffer[0];
    bool valid = (status & 0x0010u) == 0 &&
                 (type == AIRCOPY_PACKET_DATA ||
                  type == AIRCOPY_PACKET_ACK ||
                  type == AIRCOPY_PACKET_RESEND) &&
                 g_FSK_Buffer[35] == AIRCOPY_PACKET_END;

    if (valid)
    {
        AIRCOPY_Obfuscate(34);
        valid = g_FSK_Buffer[34] ==
                CRC_Calculate(&g_FSK_Buffer[0], 4 + AIRCOPY_BLOCK_SIZE);
    }

    if (gAirCopyIsSendMode)
    {
        if (!valid)
        {
            BK4819_PrepareFSKReceive();
            return;
        }

        const uint16_t offset = g_FSK_Buffer[1];
        const uint16_t currentOffset = AIRCOPY_GetBlockOffset(gAirCopyBlockNumber);
        if (type == AIRCOPY_PACKET_ACK && offset == currentOffset)
        {
            AircopyCountdown = 0;
            AircopyRetries = 0;
            gAirCopyBlockNumber++;
            gUpdateDisplay = true;
            if (gAirCopyBlockNumber >= AIRCOPY_GetTotalBlocks())
                AIRCOPY_Finish(AIRCOPY_COMPLETE);
            return;
        }

        if (type == AIRCOPY_PACKET_RESEND && offset == currentOffset)
        {
            (void)AIRCOPY_Retry();
            return;
        }

        BK4819_PrepareFSKReceive();
        return;
    }

    if (!valid)
    {
        if (type == AIRCOPY_PACKET_DATA)
            AIRCOPY_RequestResend();
        else
            BK4819_PrepareFSKReceive();
        return;
    }

    if (type != AIRCOPY_PACKET_DATA)
    {
        BK4819_PrepareFSKReceive();
        return;
    }

    const uint16_t offset = g_FSK_Buffer[1];
    if (gAirCopyBlockNumber != 0u &&
        offset == AIRCOPY_GetBlockOffset(gAirCopyBlockNumber - 1u))
    {
        AircopyCountdown = gAirCopyBlockNumber >= AIRCOPY_GetTotalBlocks()
                         ? AIRCOPY_RX_LINGER_10MS
                         : AIRCOPY_RX_TIMEOUT_10MS;
        AIRCOPY_SendControl(AIRCOPY_PACKET_ACK, offset);
        return;
    }

    if (offset != AIRCOPY_GetBlockOffset(gAirCopyBlockNumber))
    {
        AIRCOPY_RequestResend();
        return;
    }

    EEPROM_WriteBuffer(offset, &g_FSK_Buffer[2], AIRCOPY_BLOCK_SIZE);
    // All pending RX errors concerned this stop-and-wait block.
    gErrorsDuringAirCopy = 0;
    gAirCopyBlockNumber++;
    gUpdateDisplay = true;

    AircopyCountdown = gAirCopyBlockNumber < AIRCOPY_GetTotalBlocks()
                     ? AIRCOPY_RX_TIMEOUT_10MS
                     : AIRCOPY_RX_LINGER_10MS;

    AIRCOPY_SendControl(AIRCOPY_PACKET_ACK, offset);
}

static void AIRCOPY_InitTransfer(bool isSendMode)
{
    if (gAircopyCurrentMapIndex >= AIRCOPY_NUM_MAPS)
        gAircopyCurrentMapIndex = 0;

    gFSKWriteIndex = 0;
    gAirCopyBlockNumber = 0;
    gErrorsDuringAirCopy = 0;
    gInputBoxIndex = 0;
    gAirCopyIsSendMode = isSendMode;

    AircopyCountdown = isSendMode ? 0 : AIRCOPY_RX_TIMEOUT_10MS;
    AircopyRetries = 0;

    AIRCOPY_clear();
    
    gAircopyState = AIRCOPY_TRANSFER;
}

// ============================================================================
// Key Processing
// ============================================================================

static void AIRCOPY_Key_DIGITS(KEY_Code_t Key)
{
    INPUTBOX_Append(Key);

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
}

static void AIRCOPY_Key_EXIT()
{
    if (gInputBoxIndex == 0) {
        AIRCOPY_InitTransfer(0); // Mode: Receive
        BK4819_PrepareFSKReceive();
        
    } else {
        gInputBox[--gInputBoxIndex] = 10;
    }
}

static void AIRCOPY_Key_MENU()
{
    AIRCOPY_InitTransfer(1); // Mode: Send
}

static void AIRCOPY_Key_UP_DOWN(int8_t Direction)
{
    if (!gEeprom.SET_NAV) {
        Direction = -Direction;
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
}

void AIRCOPY_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    if (gAircopyState == AIRCOPY_COMPLETE || gAircopyState == AIRCOPY_FAILED)
    {
        gAircopyState = AIRCOPY_READY;
        gUpdateDisplay = true;
        gRequestDisplayScreen = DISPLAY_AIRCOPY;
        return;
    }

    if (Key != KEY_PTT) {
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    }

    switch (Key) {
    case KEY_0...KEY_9:
        AIRCOPY_Key_DIGITS(Key);
        break;
    case KEY_MENU:
        AIRCOPY_Key_MENU();
        break;
    case KEY_EXIT:
        AIRCOPY_Key_EXIT();
        break;
    case KEY_UP:
    case KEY_DOWN:
        AIRCOPY_Key_UP_DOWN(Key == KEY_UP ? 1 : -1);
        break;
    case KEY_PTT:
        break;
    default:
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        break;
    }

    gRequestDisplayScreen = DISPLAY_AIRCOPY;
}

#endif
