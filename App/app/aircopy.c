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
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
#include "driver/mb_flash.h"
#endif
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
bool     gAircopyAll;

uint16_t g_FSK_Buffer[AIRCOPY_FRAME_WORDS_MAX];
uint8_t  gFskRxExpectedWords = AIRCOPY_DATA_WORDS;

// Stop-and-wait protocol. HASH sends one CRC32 per block in a logical group.
// ACK carries the difference bitmap for HASH and confirms stored DATA.
#define AIRCOPY_PACKET_DATA          0xABCDu
#define AIRCOPY_PACKET_ACK           0xABCEu
#define AIRCOPY_PACKET_HASH          0xABD1u
#define AIRCOPY_PACKET_END           0xDCBAu
#define AIRCOPY_REJECT_MASK          0xFFFFFFFFu
#define AIRCOPY_HASH_GROUP_BLOCKS    24u
#if AIRCOPY_HASH_GROUP_BLOCKS > 32u || (2u + 2u * AIRCOPY_HASH_GROUP_BLOCKS + 2u) > AIRCOPY_DATA_WORDS
#error AirCopy hash group does not fit the bitmap or forward frame
#endif
// Sender's wait for an ACK after a DATA frame. The receiver stores the frame
// and returns a short control packet before this timeout expires.
#define AIRCOPY_ACK_TIMEOUT_10MS     150u
#define AIRCOPY_RX_TIMEOUT_10MS      2000u
#define AIRCOPY_RX_LINGER_10MS       500u
#define AIRCOPY_MAX_RETRIES          3u

static uint16_t AircopyCountdown;
static uint8_t  AircopyRetries;
static uint8_t  AircopyInFlight;
static uint8_t  AircopyGroupCount;
static uint16_t AircopyGroupStart;
static uint32_t AircopyPendingMask;
static bool     AircopyProbePending;
static uint32_t AircopyLastAckMask;

// Header packing: DATA carries a block count, HASH carries the selected map.
#define AIRCOPY_HDR_BLOCK(hdr)  ((hdr) & 0x0FFFu)
#define AIRCOPY_HDR_META(hdr)   ((hdr) >> 12)
#define AIRCOPY_MAKE_HDR(block, meta)  ((uint16_t)(((meta) << 12) | ((block) & 0x0FFFu)))

#if AIRCOPY_ALL_INDEX > 15u || AIRCOPY_ALL_BLOCKS > 4095u
#error AirCopy selection or block index does not fit the frame header
#endif

static uint8_t AircopyCopiedPixels[(AIRCOPY_BAR_WIDTH + 7u) / 8u];

// ============================================================================
// Helper Functions
// ============================================================================

uint16_t AIRCOPY_GetTotalBlocks(void)
{
    if (gAircopyAll)
        return AIRCOPY_ALL_BLOCKS;                 // banks + settings, one continuous run
    return gAircopyCurrentMapIndex == AIRCOPY_NUM_BANKS
         ? AIRCOPY_SETTINGS_BLOCKS
         : AIRCOPY_BANK_BLOCKS;
}

bool AIRCOPY_PixelWasCopied(uint8_t col)
{
    return (AircopyCopiedPixels[col / 8u] & (1u << (col % 8u))) != 0u;
}

static void AIRCOPY_MarkCopied(uint16_t start, uint8_t count)
{
    // Contiguous blocks map to contiguous pixels: a block ends at a ceiling
    // column that is >= the next block's start column, so the run [start,
    // start + count) has no gap and its span is computed in one shot.
    const uint16_t total = AIRCOPY_GetTotalBlocks();
    const uint8_t first = (uint32_t)start * AIRCOPY_BAR_WIDTH / total;
    const uint8_t last = ((uint32_t)(start + count) * AIRCOPY_BAR_WIDTH
                        + total - 1u) / total;
    for (uint8_t col = first; col < last; col++)
        AircopyCopiedPixels[col / 8u] |= 1u << (col % 8u);
}

// Resolve the map that a (possibly global, in All mode) block index lands in.
// On return, *block is rewritten to the block index within that map.
static uint8_t AIRCOPY_ResolveMap(uint16_t *block)
{
    if (!gAircopyAll)
        return gAircopyCurrentMapIndex;

    uint8_t map = 0;
    while (map < AIRCOPY_NUM_BANKS && *block >= AIRCOPY_BANK_BLOCKS)
    {
        *block -= AIRCOPY_BANK_BLOCKS;
        map++;
    }
    return map;   // AIRCOPY_NUM_BANKS once the banks are exhausted (settings map)
}

// Map index of the block currently in progress, for the All-mode slice label.
uint8_t AIRCOPY_CurrentSliceMap(void)
{
    uint16_t block = gAirCopyBlockNumber;
    return AIRCOPY_ResolveMap(&block);
}

static uint16_t AIRCOPY_GetBlockOffset(uint16_t block)
{
    const uint8_t map = AIRCOPY_ResolveMap(&block);

    if (map == AIRCOPY_NUM_BANKS)
    {
        // Settings: 6 blocks at 0xA000, 2 at 0x880E and 4 at 0x9000.
        if (block < 6u)
            return 0xA000u + block * AIRCOPY_BLOCK_SIZE;
        if (block < 8u)
            return 0x880Eu + (block - 6u) * AIRCOPY_BLOCK_SIZE;
        return 0x9000u + (block - 8u) * AIRCOPY_BLOCK_SIZE;
    }

    // A bank contains 32 frequency, 32 name and 4 attribute blocks.
    const uint16_t channelOffset = map * 0x0800u;
    if (block < 32u)
        return channelOffset + block * AIRCOPY_BLOCK_SIZE;
    if (block < 64u)
        return 0x4000u + channelOffset + (block - 32u) * AIRCOPY_BLOCK_SIZE;
    return 0x8000u + map * 0x0100u
         + (block - 64u) * AIRCOPY_BLOCK_SIZE;
}

static uint8_t AIRCOPY_GroupBlockCount(uint16_t start, uint16_t total)
{
    // A new group always starts at a multiple of AIRCOPY_HASH_GROUP_BLOCKS.
    const uint16_t remaining = total - start;
    return (uint8_t)(remaining < AIRCOPY_HASH_GROUP_BLOCKS
                   ? remaining : AIRCOPY_HASH_GROUP_BLOCKS);
}

// Hash one logical block. Each call needs only a 64-byte scratch buffer.
static uint32_t AIRCOPY_BlockCRC32(uint16_t blockNumber)
{
    uint8_t block[AIRCOPY_BLOCK_SIZE];

    EEPROM_ReadBuffer(AIRCOPY_GetBlockOffset(blockNumber), block, sizeof(block));
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    return MB_Crc32Bytes(block, sizeof(block));
#else
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned int j = 0; j < sizeof(block); j++)
    {
        crc ^= block[j];
        for (uint8_t bit = 0; bit < 8u; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }

    return crc ^ 0xFFFFFFFFu;
#endif
}

static uint16_t AIRCOPY_NextPendingBlock(void)
{
    for (uint8_t i = 0; i < AircopyGroupCount; i++)
        if (AircopyPendingMask & (1u << i))
            return AircopyGroupStart + i;
    return AircopyGroupStart + AircopyGroupCount;
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

// Encode a frame length (in words) into the BK4829 FSK Data Length register:
// REG_5D holds (bytes - 1) as an 11-bit field, low 8 bits in <15:8>, high 3 in <7:5>.
static uint16_t AIRCOPY_Reg5D(uint8_t words)
{
    const uint16_t len = (uint16_t)(words * 2u - 1u);
    return (uint16_t)(((len & 0x00FFu) << 8) | (((len >> 8) & 0x07u) << 5));
}

// Arm FSK RX for the frame this role expects: the receiver waits for DATA, the
// sender waits for a tiny ACK. Both REG_5D and the drain length must be
// set before the frame arrives so RX-finished fires at the right byte count.
static void AIRCOPY_ArmReceive(void)
{
    const uint8_t words = gAirCopyIsSendMode ? AIRCOPY_CTRL_WORDS : AIRCOPY_DATA_WORDS;
    gFskRxExpectedWords = words;
    BK4819_WriteRegister(BK4819_REG_5D, AIRCOPY_Reg5D(words));
    BK4819_PrepareFSKReceive();
}

static void AIRCOPY_TransmitBuffer(uint8_t words)
{
    // Both sides need time to leave TX and re-arm FSK RX before the reply.
    SYSTEM_DelayMs(50);
    RADIO_SetTxParameters();
    BK4819_SendFSKData(g_FSK_Buffer, words);
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
}

// Frame layout: [0]=type, [1..words-3]=header+payload, [words-2]=CRC, [words-1]=END.
static void AIRCOPY_FinalizeAndSend(uint8_t words)
{
    g_FSK_Buffer[words - 2u] = CRC_Calculate(&g_FSK_Buffer[0], (uint16_t)(words - 2u) * 2u);
    g_FSK_Buffer[words - 1u] = AIRCOPY_PACKET_END;
    AIRCOPY_Obfuscate(words - 2u);
    BK4819_WriteRegister(BK4819_REG_5D, AIRCOPY_Reg5D(words));
    AIRCOPY_TransmitBuffer(words);
    gFSKWriteIndex = 0;
    AIRCOPY_ArmReceive();
}

static void AIRCOPY_SendAck(uint16_t block, uint32_t mask, uint16_t total)
{
    AircopyCountdown = gAirCopyBlockNumber >= total
                     ? AIRCOPY_RX_LINGER_10MS : AIRCOPY_RX_TIMEOUT_10MS;
    g_FSK_Buffer[0] = AIRCOPY_PACKET_ACK;
    g_FSK_Buffer[1] = block;
    g_FSK_Buffer[2] = (uint16_t)mask;
    g_FSK_Buffer[3] = (uint16_t)(mask >> 16);
    g_FSK_Buffer[4] = 0;
    g_FSK_Buffer[5] = 0;
    AIRCOPY_FinalizeAndSend(AIRCOPY_CTRL_WORDS);
}

static void AIRCOPY_RejectFrame(void)
{
    // The sender retries when no ACK arrives.
    gErrorsDuringAirCopy++;
    gUpdateDisplay = true;
    AircopyCountdown = AIRCOPY_RX_TIMEOUT_10MS;
    AIRCOPY_ArmReceive();
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

    const uint16_t total = AIRCOPY_GetTotalBlocks();
    const uint16_t start = gAirCopyBlockNumber;
    if (AircopyProbePending)
    {
        AircopyGroupStart = start;
        AircopyGroupCount = AIRCOPY_GroupBlockCount(start, total);
        AircopyInFlight = 0;
        g_FSK_Buffer[0] = AIRCOPY_PACKET_HASH;
        g_FSK_Buffer[1] = AIRCOPY_MAKE_HDR(start, gAircopyCurrentMapIndex);
        memset(&g_FSK_Buffer[2], 0, (AIRCOPY_DATA_WORDS - 4u) * sizeof(g_FSK_Buffer[0]));
        for (uint8_t i = 0; i < AircopyGroupCount; i++)
        {
            const uint32_t crc = AIRCOPY_BlockCRC32(start + i);
            g_FSK_Buffer[2u + 2u * i] = (uint16_t)crc;
            g_FSK_Buffer[3u + 2u * i] = (uint16_t)(crc >> 16);
        }
    }
    else
    {
        const uint8_t offset = (uint8_t)(start - AircopyGroupStart);
        uint8_t count = 0;
        while (count < AIRCOPY_BLOCKS_PER_FRAME && offset + count < AircopyGroupCount &&
               (AircopyPendingMask & (1u << (offset + count))))
            count++;
        if (count == 0u)
        {
            AIRCOPY_Finish(AIRCOPY_FAILED);
            return 0;
        }

        AircopyInFlight = count;
        g_FSK_Buffer[0] = AIRCOPY_PACKET_DATA;
        g_FSK_Buffer[1] = AIRCOPY_MAKE_HDR(start, count);
        memset(&g_FSK_Buffer[AIRCOPY_DATA_HEADER_WORDS], 0,
               AIRCOPY_BLOCKS_PER_FRAME * AIRCOPY_BLOCK_SIZE);
        for (uint8_t i = 0; i < count; i++)
            EEPROM_ReadBuffer(AIRCOPY_GetBlockOffset(start + i),
                              &g_FSK_Buffer[AIRCOPY_DATA_HEADER_WORDS + i * AIRCOPY_BLOCK_WORDS],
                              AIRCOPY_BLOCK_SIZE);
    }

    AIRCOPY_FinalizeAndSend(AIRCOPY_DATA_WORDS);
    AircopyCountdown = AIRCOPY_ACK_TIMEOUT_10MS;

    return 1;
}

void AIRCOPY_StorePacket(void)
{
    // The role decides the frame length: the receiver waits for a full DATA
    // frame, the sender for a tiny ACK. CRC and END sit at the tail.
    const uint8_t words = gFskRxExpectedWords;
    if (gFSKWriteIndex < words) {
        return;
    }

    gFSKWriteIndex = 0;
    const uint16_t status = BK4819_ReadRegister(BK4819_REG_0B);
    const uint16_t type = g_FSK_Buffer[0];
    const bool statusOk = (status & 0x0010u) == 0;
    const bool endOk = g_FSK_Buffer[words - 1u] == AIRCOPY_PACKET_END;
    bool valid = statusOk && endOk &&
                 (type == AIRCOPY_PACKET_DATA ||
                  type == AIRCOPY_PACKET_ACK ||
                  type == AIRCOPY_PACKET_HASH);

    if (valid)
    {
        AIRCOPY_Obfuscate(words - 2u);
        valid = g_FSK_Buffer[words - 2u] ==
                CRC_Calculate(&g_FSK_Buffer[0], (uint16_t)(words - 2u) * 2u);
    }

    const uint16_t total = AIRCOPY_GetTotalBlocks();

    if (gAirCopyIsSendMode)
    {
        const uint16_t ackBlock = AIRCOPY_HDR_BLOCK(g_FSK_Buffer[1]);
        const uint32_t responseMask = (uint32_t)g_FSK_Buffer[2]
                                    | ((uint32_t)g_FSK_Buffer[3] << 16);

        if (!valid || type != AIRCOPY_PACKET_ACK || ackBlock != gAirCopyBlockNumber)
        {
            AIRCOPY_ArmReceive();
            return;
        }

        if (AircopyProbePending)
        {
            if (responseMask == AIRCOPY_REJECT_MASK)
            {
                AIRCOPY_Finish(AIRCOPY_FAILED);
                return;
            }
            if (AircopyGroupCount == 0u ||
                (responseMask & ~((1u << AircopyGroupCount) - 1u)) != 0u)
            {
                AIRCOPY_ArmReceive();
                return;
            }
            if (responseMask == 0u)
                gAirCopyBlockNumber += AircopyGroupCount;
            else
            {
                AircopyPendingMask = responseMask;
                AircopyProbePending = false;
                gAirCopyBlockNumber = AIRCOPY_NextPendingBlock();
            }
        }
        else
        {
            if (AircopyInFlight == 0u || responseMask != 0u)
            {
                AIRCOPY_ArmReceive();
                return;
            }
            const uint8_t offset = (uint8_t)(ackBlock - AircopyGroupStart);
            const uint32_t sentMask = ((1u << AircopyInFlight) - 1u) << offset;
            AIRCOPY_MarkCopied(ackBlock, AircopyInFlight);
            AircopyPendingMask &= ~sentMask;
            gAirCopyBlockNumber = AIRCOPY_NextPendingBlock();
            if (AircopyPendingMask == 0u)
                AircopyProbePending = true;
        }

        AircopyCountdown = 0;
        AircopyRetries = 0;
        gUpdateDisplay = true;
        if (gAirCopyBlockNumber >= total)
            AIRCOPY_Finish(AIRCOPY_COMPLETE);
        return;
    }

    if (!valid)
    {
        if (type == AIRCOPY_PACKET_DATA || type == AIRCOPY_PACKET_HASH)
            AIRCOPY_RejectFrame();
        else
            AIRCOPY_ArmReceive();
        return;
    }

    if (type == AIRCOPY_PACKET_HASH)
    {
        const uint16_t start = AIRCOPY_HDR_BLOCK(g_FSK_Buffer[1]);
        if (AIRCOPY_HDR_META(g_FSK_Buffer[1]) != gAircopyCurrentMapIndex)
        {
            // The two selections must describe the same logical block map.
            AIRCOPY_SendAck(start, AIRCOPY_REJECT_MASK, total);
            AIRCOPY_Finish(AIRCOPY_FAILED);
            return;
        }
        if (start >= total || start % AIRCOPY_HASH_GROUP_BLOCKS != 0u)
        {
            AIRCOPY_RejectFrame();
            return;
        }

        if (AircopyGroupCount != 0u && start == AircopyGroupStart)
        {
            AIRCOPY_SendAck(start, AircopyLastAckMask, total);
            return;
        }

        if (start != gAirCopyBlockNumber || AircopyPendingMask != 0u)
        {
            AIRCOPY_RejectFrame();
            return;
        }

        AircopyGroupStart = start;
        AircopyGroupCount = AIRCOPY_GroupBlockCount(start, total);
        uint32_t mask = 0;
        for (uint8_t i = 0; i < AircopyGroupCount; i++)
        {
            const uint32_t sentCRC = (uint32_t)g_FSK_Buffer[2u + 2u * i]
                                   | ((uint32_t)g_FSK_Buffer[3u + 2u * i] << 16);
            if (AIRCOPY_BlockCRC32(start + i) != sentCRC)
                mask |= 1u << i;
        }

        AircopyPendingMask = mask;
        gAirCopyBlockNumber = AIRCOPY_NextPendingBlock();
        AircopyLastAckMask = mask;
        gErrorsDuringAirCopy = 0;
        gUpdateDisplay = true;
        AIRCOPY_SendAck(start, mask, total);
        return;
    }

    if (type != AIRCOPY_PACKET_DATA)
    {
        AIRCOPY_ArmReceive();
        return;
    }

    const uint16_t start = AIRCOPY_HDR_BLOCK(g_FSK_Buffer[1]);
    const uint8_t count = (uint8_t)AIRCOPY_HDR_META(g_FSK_Buffer[1]);

    if (count == 0u || count > AIRCOPY_BLOCKS_PER_FRAME || start >= total)
    {
        AIRCOPY_RejectFrame();
        return;
    }

    if (start < gAirCopyBlockNumber)
    {
        // Repeat the exact decision when a DATA ACK was lost.
        AIRCOPY_SendAck(start, 0, total);
        return;
    }

    if (start != gAirCopyBlockNumber)
    {
        // Ignore an out-of-sequence frame; the sender retries on ACK timeout.
        AIRCOPY_RejectFrame();
        return;
    }

    if (AircopyPendingMask == 0u || start < AircopyGroupStart ||
        (uint16_t)(start + count) > AircopyGroupStart + AircopyGroupCount)
    {
        AIRCOPY_RejectFrame();
        return;
    }

    const uint8_t offset = (uint8_t)(start - AircopyGroupStart);
    const uint32_t frameMask = ((1u << count) - 1u) << offset;
    if ((AircopyPendingMask & frameMask) != frameMask)
    {
        AIRCOPY_RejectFrame();
        return;
    }

    for (uint8_t i = 0; i < count; i++)
        EEPROM_WriteBuffer(AIRCOPY_GetBlockOffset(start + i),
                           &g_FSK_Buffer[AIRCOPY_DATA_HEADER_WORDS + i * AIRCOPY_BLOCK_WORDS],
                           AIRCOPY_BLOCK_SIZE);

    AIRCOPY_MarkCopied(start, count);
    AircopyPendingMask &= ~frameMask;
    // All pending RX errors concerned this run.
    gErrorsDuringAirCopy = 0;
    gAirCopyBlockNumber = AIRCOPY_NextPendingBlock();
    gUpdateDisplay = true;
    AIRCOPY_SendAck(start, 0, total);
}

static void AIRCOPY_InitTransfer(bool isSendMode)
{
    if (gAircopyCurrentMapIndex > AIRCOPY_ALL_INDEX)
        gAircopyCurrentMapIndex = 0;
    gAircopyAll = (gAircopyCurrentMapIndex == AIRCOPY_ALL_INDEX);

    gFSKWriteIndex = 0;
    gAirCopyBlockNumber = 0;
    gErrorsDuringAirCopy = 0;
    memset(AircopyCopiedPixels, 0, sizeof(AircopyCopiedPixels));
    gInputBoxIndex = 0;
    gAirCopyIsSendMode = isSendMode;

    AircopyCountdown = isSendMode ? 0 : AIRCOPY_RX_TIMEOUT_10MS;
    AircopyRetries = 0;
    AircopyInFlight = 0;
    AircopyGroupCount = 0;
    AircopyGroupStart = 0;
    AircopyPendingMask = 0;
    AircopyProbePending = true;
    AircopyLastAckMask = 0;
    // The sender listens for tiny ACKs, the receiver for full DATA frames.
    gFskRxExpectedWords = isSendMode ? AIRCOPY_CTRL_WORDS : AIRCOPY_DATA_WORDS;

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
        AIRCOPY_ArmReceive();

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
            gAircopyCurrentMapIndex = (gAircopyCurrentMapIndex + 1) % (AIRCOPY_NUM_MAPS + 1u);
            break;
        case -1:
            gAircopyCurrentMapIndex = (gAircopyCurrentMapIndex + AIRCOPY_NUM_MAPS) % (AIRCOPY_NUM_MAPS + 1u);
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
