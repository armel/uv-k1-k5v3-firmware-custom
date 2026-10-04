/* Copyright 2025 muzkr https://github.com/muzkr
 * Copyright 2023 Dual Tachyon
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

#include <string.h>

#if !defined(ENABLE_OVERLAY)
    #include "py32f0xx.h"
#endif
#ifdef ENABLE_FMRADIO_EMBEDDED
    #include "app/fm.h"
#endif
#include "app/uart.h"
#include "board.h"
#include "py32f071_ll_dma.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/crc.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "external/printf/printf.h"

#if defined(ENABLE_UART)
#include "driver/uart.h"
#endif

#if defined(ENABLE_USB)
#include "driver/vcp.h"
#endif

#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "version.h"

#ifdef ENABLE_CAT
    #include "dcs.h"
    #include "app/action.h"
    #include "app/dtmf.h"
    #include "frequencies.h"
    #include "radio.h"
    #include "ui/ui.h"
    #include "helper/battery.h"
#endif

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    #include "driver/mb_flash.h"
#endif

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    #include "apps/app_overlay.h"
#endif

#if defined(ENABLE_OVERLAY)
    #include "sram-overlay.h"
#endif

#define UNUSED(x) (void)(x)

#define DMA_INDEX(x, y, z) (((x) + (y)) % (z))

#if defined(ENABLE_UART)
    #define DMA_CHANNEL LL_DMA_CHANNEL_2
#endif

// !! Make sure this is correct!
#define MAX_REPLY_SIZE 144

typedef struct {
    uint16_t ID;
    uint16_t Size;
} Header_t;

typedef struct {
    uint8_t  Padding[2];
    uint16_t ID;
} Footer_t;

typedef struct {
    Header_t Header;
    uint32_t Timestamp;
} CMD_0514_t;

typedef struct {
    Header_t Header;
    struct {
        char     Version[16];
        bool     bHasCustomAesKey;
        bool     bIsInLockScreen;
        uint8_t  Padding[2];
        uint32_t Challenge[4];
    } Data;
} REPLY_0514_t;

typedef struct {
    Header_t Header;
    uint16_t Offset;
    uint8_t  Size;
    uint8_t  Padding;
    uint32_t Timestamp;
} CMD_051B_t;

typedef struct {
    Header_t Header;
    struct {
        uint16_t Offset;
        uint8_t  Size;
        uint8_t  Padding;
        uint8_t  Data[128];
    } Data;
} REPLY_051B_t;

typedef struct {
    Header_t Header;
    uint16_t Offset;
    uint8_t  Size;
    bool     bAllowPassword;
    uint32_t Timestamp;
    uint8_t  Data[0];
} CMD_051D_t;

typedef struct {
    Header_t Header;
    struct {
        uint16_t Offset;
    } Data;
} REPLY_051D_t;

#ifdef ENABLE_EXTRA_UART_CMD
typedef struct {
    Header_t Header;
    struct {
        uint16_t RSSI;
        uint8_t  ExNoiseIndicator;
        uint8_t  GlitchIndicator;
    } Data;
} REPLY_0527_t;

typedef struct {
    Header_t Header;
    struct {
        uint16_t Voltage;
        uint16_t Current;
    } Data;
} REPLY_0529_t;

typedef struct {
    Header_t Header;
    uint32_t Response[4];
} CMD_052D_t;
#endif

typedef struct {
    Header_t Header;
    struct {
        bool bIsLocked;
        uint8_t Padding[3];
    } Data;
} REPLY_052D_t;


#ifdef ENABLE_EXTRA_UART_CMD
typedef struct {
    Header_t Header;
    uint32_t Timestamp;
} CMD_052F_t;
#endif

static const uint8_t Obfuscation[16] =
{
    0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40, 0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80
};

typedef union
{
    uint8_t Buffer[256];
    struct
    {
        Header_t Header;
        uint8_t Data[252];
    };
} UART_Command_t __attribute__ ((aligned (4)));


#if defined(ENABLE_UART)
    static uint32_t UART_Timestamp;
    static UART_Command_t UART_Command;
    static uint16_t gUART_WriteIndex;
#endif
#if defined(ENABLE_USB)
    static uint32_t VCP_Timestamp;
    static UART_Command_t VCP_Command;
    static uint16_t VCP_ReadIndex;
#endif

// static bool     bIsEncrypted = true;
#define bIsEncrypted true

// ============================================================
// === KENWOOD CAT — Rozszerzony protokół telemetrii i skanera
// === Port z uv-k1-k5v3-firmware-CAT (muzkr 2025)
// ============================================================
#ifdef ENABLE_CAT

extern bool gRxIdleMode;
extern bool g_SquelchLost;

// --- S-Meter / auto RSSI reporting variables ---
static bool    g_AutoReportRSSI     = false;
static int16_t g_LastReportedRSSI   = 0;
static uint16_t g_UartRssiTimer_10ms = 0;

// --- SC scanner variables (channel list) ---
#define MAX_UART_SCAN_LIST 25
static uint32_t g_UartScanList[MAX_UART_SCAN_LIST];
static uint8_t  g_UartScanCount        = 0;
static uint8_t  g_UartScanIndex        = 0;
static uint8_t  g_UartScanDelay_10ms   = 0;
static char     g_UartScanResponse[256];
static bool     g_UartScanActive       = false;
static uint32_t g_UartScanOriginalFreq = 0;
static uint8_t  g_UartScanOriginalBand = 0;

// --- SCF scanner variables (single channel fast measurement) ---
static uint8_t  g_SingleScanState        = 0;
static uint8_t  g_SingleScanDelay_10ms   = 0;
static uint32_t g_SingleScanTargetFreq   = 0;
static uint32_t g_SingleScanOriginalFreq = 0;
static uint8_t  g_SingleScanOriginalBand = 0;
static uint32_t g_SingleScanPort         = 0;

// --- ASCII CAT command buffer ---
#define NUM_CAT_PORTS 2
static char     cat_buffer[NUM_CAT_PORTS][64];
static uint8_t  cat_pos[NUM_CAT_PORTS] = {0, 0};
static uint32_t g_UartScanPort         = 0;

// --- FSK modem variables (CAT) ---
static uint8_t  g_FskRxMode           = 0;      // 0 = off, 1 = on (audible), 2 = on (auto-mute)
static uint8_t  g_FskBaud             = 0;      // 0 = 1200 bps, 1 = 2400 bps
static uint16_t g_FskSyncWord         = 0xABCD; // FSK sync word (default 0xABCD)
static uint8_t  g_FskPacketLen        = 32;     // Packet length in bytes (8-72, even)
static bool     g_FskBusyLock         = false;  // Busy channel lockout flag
static bool     g_FskMutedAudio       = false;  // Speaker mute flag in auto-mute mode
static uint8_t  g_FskRxTimeout_10ms   = 0;      // RX frame timeout watchdog (safety timer)
static uint16_t g_CatFskRxBuffer[36];           // FSK RX buffer (up to 72 bytes)
static uint8_t  g_CatFskRxIndex       = 0;      // Received 16-bit word counter
static uint16_t g_TxSafeTimeout_10ms  = 0;      // Safe TX watchdog timer (10ms ticks)

// --- Forward declarations ---
static void UART_HardwareScanner_Periodic(void);
static void UART_SingleScan_Periodic(void);
static void Process_Kenwood_CAT(uint32_t Port, char *cat_buffer, uint8_t cat_pos);
static void CAT_StartTransmit(void);
static void CAT_StopTransmit(void);
void UART_FSK_PrepareReceive(void);
static void UART_FSK_Disable(void);
static bool UART_FSK_Transmit(const uint8_t *payload, uint8_t payloadLen, uint8_t payloadType);

// ---------------------------------------------------------------------------
// UART_SendText - transmit ASCII text (asynchronous, non-blocking)
// ---------------------------------------------------------------------------
void UART_SendText(uint32_t Port, const char *str)
{
    uint32_t len = strlen(str);
    if (!len) return;

#if defined(ENABLE_UART)
    if (Port == UART_PORT_UART) {
        UART_Send((const uint8_t *)str, len);
    }
#endif
#if defined(ENABLE_USB)
    if (Port == UART_PORT_VCP) {
        static uint8_t s_vcp_tx_buf[128];
        if (len > sizeof(s_vcp_tx_buf)) len = sizeof(s_vcp_tx_buf);
        memcpy(s_vcp_tx_buf, str, len);
        VCP_SendAsync(s_vcp_tx_buf, len);
    }
#endif
}

// ---------------------------------------------------------------------------
// Number-to-text converters (without stdio formatting overhead)
// ---------------------------------------------------------------------------
static void UIntToText(char *buffer, uint32_t value, int digits, int offset)
{
    for (int i = offset + digits - 1; i >= offset; i--) {
        buffer[i] = (value % 10) + '0';
        value /= 10;
    }
}

static uint32_t TextToUInt(const char *buffer, int start, int len)
{
    uint32_t val = 0;
    for (int i = start; i < start + len; i++) {
        if (buffer[i] >= '0' && buffer[i] <= '9')
            val = (val * 10) + (buffer[i] - '0');
    }
    return val;
}

static uint16_t ParseDCSCode(const char *buffer, int start)
{
    uint16_t val = 0;
    for (int i = 0; i < 3; i++) {
        char c = buffer[start + i];
        if (c < '0' || c > '7') return 0xFFFF;
        val = (val * 8) + (c - '0');
    }
    return val;
}

// ---------------------------------------------------------------------------
// Radio & UI helpers
// ---------------------------------------------------------------------------
static void ForceScreenUpdate(void)
{
    gUpdateStatus  = true;
    gUpdateDisplay = true;
#ifdef ENABLE_FEAT_F4HWN
    gRequestDisplayScreen = DISPLAY_MAIN;
#endif
}

static void CAT_ApplyAndSave(bool bIsGlobal)
{
    VFO_Info_t *vfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];

    RADIO_ApplyOffset(vfo);
    vfo->pTX->Frequency = vfo->freq_config_TX.Frequency;

    if (bIsGlobal) {
        SETTINGS_SaveSettings();
    } else {
        SETTINGS_SaveChannel(vfo->CHANNEL_SAVE, gEeprom.TX_VFO, vfo, 2);
    }

    if (gRxIdleMode) {
        BK4819_RX_TurnOn();
        gRxIdleMode = false;
    }

    RADIO_ConfigureSquelchAndOutputPower(vfo);
    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);
    if (g_FskRxMode > 0) {
        UART_FSK_PrepareReceive();
    }
    ForceScreenUpdate();
}

static void Apply_Tone_To_Active_VFO(uint8_t code_type, uint8_t code_index)
{
    VFO_Info_t *vfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
    vfo->pTX->CodeType = code_type;
    vfo->pTX->Code     = code_index;
    vfo->pRX->CodeType = code_type;
    vfo->pRX->Code     = code_index;
    vfo->freq_config_TX.CodeType = code_type;
    vfo->freq_config_TX.Code     = code_index;
    vfo->freq_config_RX.CodeType = code_type;
    vfo->freq_config_RX.Code     = code_index;

    CAT_ApplyAndSave(false);
}

// ---------------------------------------------------------------------------
// UART_GetRSSI_dBm - read RSSI from BK4819 in dBm
// ---------------------------------------------------------------------------
static int16_t UART_GetRSSI_dBm(void)
{
    if (gRxIdleMode)
        return -160;
    uint16_t rssi_raw = BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
    return (rssi_raw / 2) - 160;
}

// ---------------------------------------------------------------------------
// UART_HardwareScanner_Periodic - SC channel scan state machine
// Called every 10ms from APP_TimeSlice10ms
// ---------------------------------------------------------------------------
static void UART_HardwareScanner_Periodic(void)
{
    if (!g_UartScanActive) return;

    if (g_UartScanDelay_10ms > 0) {
        g_UartScanDelay_10ms--;

        if (g_UartScanDelay_10ms == 0) {
            uint32_t target_freq = g_UartScanList[g_UartScanIndex];
            uint8_t  band        = FREQUENCY_GetBand(target_freq);

            int16_t dbm = BK4819_GetRSSI_dBm() + dBmCorrTable[band];
            uint8_t sq  = g_SquelchLost ? 1 : 0;

            char temp[16];
            sprintf(temp, ",%d,%d", dbm, sq);
            strcat(g_UartScanResponse, temp);

            g_UartScanIndex++;

            if (g_UartScanIndex >= g_UartScanCount) {
                g_UartScanActive = false;

                VFO_Info_t *vfo       = &gEeprom.VfoInfo[gEeprom.RX_VFO];
                vfo->pRX->Frequency   = g_UartScanOriginalFreq;
                vfo->Band             = g_UartScanOriginalBand;
                RADIO_ConfigureSquelchAndOutputPower(vfo);
                RADIO_SetupRegisters(true);
                if (g_FskRxMode > 0) {
                    UART_FSK_PrepareReceive();
                }
                gUpdateDisplay = true;

                strcat(g_UartScanResponse, ";");
                UART_SendText(g_UartScanPort, g_UartScanResponse);
                return;
            }
        }
        return;
    }

    uint32_t target_freq = g_UartScanList[g_UartScanIndex];
    if (target_freq == 0) {
        strcat(g_UartScanResponse, ",0,0");
        g_UartScanIndex++;
        return;
    }

    VFO_Info_t *vfo     = &gEeprom.VfoInfo[gEeprom.RX_VFO];
    vfo->pRX->Frequency = target_freq;
    vfo->Band           = FREQUENCY_GetBand(target_freq);

    RADIO_ConfigureSquelchAndOutputPower(vfo);
    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;

    g_UartScanDelay_10ms = 9;   // ~90ms per channel
}

// ---------------------------------------------------------------------------
// UART_SingleScan_Periodic - SCF single frequency scan state machine
// Called every 10ms from APP_TimeSlice10ms
// ---------------------------------------------------------------------------
static void UART_SingleScan_Periodic(void)
{
    if (g_SingleScanState == 0) return;

    if (g_SingleScanState == 1) {
        if (g_SingleScanDelay_10ms > 0) {
            g_SingleScanDelay_10ms--;

            if (g_SingleScanDelay_10ms == 0) {
                uint8_t  band     = FREQUENCY_GetBand(g_SingleScanTargetFreq);
                uint16_t rssi_raw = BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
                int16_t  dbm      = (rssi_raw / 2) - 160 + dBmCorrTable[band];
                uint16_t noise    = BK4819_ReadRegister(BK4819_REG_65) & 0x007F;
                uint16_t glitch   = BK4819_ReadRegister(BK4819_REG_63);
                uint8_t  sq       = g_SquelchLost ? 1 : 0;

                uint32_t freqHz   = g_SingleScanTargetFreq * 10;

                char resp[64];
                sprintf(resp, "SQ%011u,%d,%+04d,%u,%u;", freqHz, sq, dbm, noise, glitch);

                VFO_Info_t *vfo   = &gEeprom.VfoInfo[gEeprom.RX_VFO];
                vfo->pRX->Frequency = g_SingleScanOriginalFreq;
                vfo->Band           = g_SingleScanOriginalBand;

                RADIO_ConfigureSquelchAndOutputPower(vfo);
                RADIO_SetupRegisters(true);
                if (g_FskRxMode > 0) {
                    UART_FSK_PrepareReceive();
                }
                gUpdateDisplay = true;

                g_SingleScanState = 0;
                UART_SendText(g_SingleScanPort, resp);
            }
        }
    }
}

static void CAT_StartTransmit(void)
{
    if (gCurrentVfo && TX_freq_check(gCurrentVfo->pTX->Frequency) != 0 && gCurrentVfo->TX_LOCK) {
        return;
    }
    if (gCurrentFunction != FUNCTION_TRANSMIT) {
        FUNCTION_Select(FUNCTION_TRANSMIT);
        ForceScreenUpdate();
    }
}

static void CAT_StopTransmit(void)
{
    g_TxSafeTimeout_10ms = 0;
    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
        BK4819_SetupPowerAmplifier(0, 0);
        BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
        RADIO_SelectVfos();
        RADIO_SetupRegisters(true); // true wymusza FUNCTION_Select(FUNCTION_FOREGROUND)
        BK4819_RX_TurnOn();
        gRxIdleMode = false;
        ForceScreenUpdate();
    }
}

// ---------------------------------------------------------------------------
// UART_ReportRSSI_Periodic - main 10ms task: S-meter reporting + scan dispatcher
// ---------------------------------------------------------------------------
void UART_ReportRSSI_Periodic(void)
{
    // Scan state machine dispatcher (always active, even if auto-report is disabled)
    UART_HardwareScanner_Periodic();
    UART_SingleScan_Periodic();

    if (g_FskRxTimeout_10ms > 0) {
        g_FskRxTimeout_10ms--;
        if (g_FskRxTimeout_10ms == 0) {
            g_CatFskRxIndex = 0;
            if (g_FskMutedAudio) {
                BK4819_SetAF(BK4819_AF_FM);
                g_FskMutedAudio = false;
            }
            UART_FSK_PrepareReceive();
        }
    }

    if (g_TxSafeTimeout_10ms > 0) {
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            g_TxSafeTimeout_10ms--;
            if (g_TxSafeTimeout_10ms == 0) {
                CAT_StopTransmit();
            }
        } else {
            g_TxSafeTimeout_10ms = 0;
        }
    }

    if (!g_AutoReportRSSI) return;

    g_UartRssiTimer_10ms++;

    uint8_t  active_vfo  = gEeprom.RX_VFO;
    int16_t  current_dbm = UART_GetRSSI_dBm();
    static uint8_t g_LastReportedVFO = 0xFF;

    bool time_for_1s   = (g_UartRssiTimer_10ms >= 100);
    bool time_for_200ms = (g_UartRssiTimer_10ms >= 20);
    bool dbm_changed   = (current_dbm != g_LastReportedRSSI);
    bool vfo_changed   = (active_vfo  != g_LastReportedVFO);

    if (time_for_1s || (time_for_200ms && (dbm_changed || vfo_changed))) {
        char resp[32];
        sprintf(resp, "RR%d,%+04d;", active_vfo, current_dbm);
#if defined(ENABLE_UART)
        UART_SendText(UART_PORT_UART, resp);
#endif
#if defined(ENABLE_USB)
        UART_SendText(UART_PORT_VCP, resp);
#endif
        g_LastReportedRSSI    = current_dbm;
        g_LastReportedVFO     = active_vfo;
        g_UartRssiTimer_10ms  = 0;
    }
}

// ---------------------------------------------------------------------------
// FSK Modem Functions (CAT)
// ---------------------------------------------------------------------------
bool UART_FSK_IsRxEnabled(void)
{
    return (g_FskRxMode > 0);
}

void UART_FSK_ApplyRxRegisters(void)
{
    BK4819_WriteRegister(BK4819_REG_70, 0x00C3);
    BK4819_WriteRegister(BK4819_REG_72, (g_FskBaud == 1) ? 0x60CA : 0x3065);
    BK4819_WriteRegister(BK4819_REG_58, (g_FskBaud == 1) ? 0x00C9 : 0x00C1);
    BK4819_WriteRegister(BK4819_REG_5A, 0xAA55);
    BK4819_WriteRegister(BK4819_REG_5B, g_FskSyncWord);
    BK4819_WriteRegister(BK4819_REG_5C, 0xAA30);
    BK4819_WriteRegister(BK4819_REG_5D, (uint16_t)(g_FskPacketLen - 1) << 8);
    BK4819_WriteRegister(0x5E, 0x3204);

    BK4819_WriteRegister(BK4819_REG_59, 0x4068);
    BK4819_WriteRegister(BK4819_REG_59, 0x3068);
}

void UART_FSK_PrepareReceive(void)
{
    g_CatFskRxIndex = 0;
    UART_FSK_ApplyRxRegisters();
}

static void UART_FSK_Disable(void)
{
    g_FskRxMode = 0;
    g_CatFskRxIndex = 0;
    if (g_FskMutedAudio) {
        BK4819_SetAF(BK4819_AF_FM);
        g_FskMutedAudio = false;
    }
    BK4819_WriteRegister(BK4819_REG_59, 0x0068);
    BK4819_WriteRegister(BK4819_REG_58, 0x0000);
    BK4819_WriteRegister(BK4819_REG_70, 0x0000);
    RADIO_SetupRegisters(true);
}

void UART_FSK_OnSync(void)
{
    g_CatFskRxIndex = 0;       // ALWAYS reset index on new sync word detection!
    g_FskRxTimeout_10ms = 35;  // 350 ms frame completion timeout
    if (g_FskRxMode == 2) {
        BK4819_SetAF(BK4819_AF_MUTE);
        g_FskMutedAudio = true;
    }
}

void UART_FSK_HandleRxInterrupt(bool rxFinished)
{
    uint8_t totalWords = g_FskPacketLen / 2;
    if (totalWords > 36) totalWords = 36;
    if (totalWords < 4)  totalWords = 4; // Minimum 4 words = 8 bytes

    g_FskRxTimeout_10ms = 35; // Refresh timeout upon receiving data word

    unsigned int wordsToRead;
    if (rxFinished) {
        wordsToRead = (g_CatFskRxIndex < totalWords) ? (totalWords - g_CatFskRxIndex) : 0u;
    } else {
        wordsToRead = 4u;
        if (g_CatFskRxIndex + wordsToRead > totalWords)
            wordsToRead = totalWords - g_CatFskRxIndex;
    }

    for (unsigned int i = 0; i < wordsToRead; i++) {
        uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
        if (g_CatFskRxIndex < totalWords)
            g_CatFskRxBuffer[g_CatFskRxIndex++] = word;
    }

    if (rxFinished || g_CatFskRxIndex >= totalWords) {
        g_FskRxTimeout_10ms = 0;
        if (g_FskMutedAudio) {
            BK4819_SetAF(BK4819_AF_FM);
            g_FskMutedAudio = false;
        }

        if (g_CatFskRxIndex >= totalWords &&
            g_CatFskRxBuffer[0] == g_FskSyncWord)
        {
            uint16_t expectedCrc = CRC_Calculate(&g_CatFskRxBuffer[1], (totalWords - 2) * 2);
            if (g_CatFskRxBuffer[totalWords - 1] == expectedCrc) {
                uint8_t *payloadPtr = (uint8_t *)&g_CatFskRxBuffer[1];
                uint8_t payloadLen  = payloadPtr[0];
                uint8_t payloadType = payloadPtr[1];
                uint8_t maxPayload  = (totalWords - 2) * 2 - 2;
                if (payloadLen > maxPayload) payloadLen = maxPayload;

                uint8_t  active_vfo = gEeprom.RX_VFO;
                int16_t  dbm        = BK4819_GetRSSI_dBm() + dBmCorrTable[gEeprom.VfoInfo[active_vfo].Band];

                char resp[128];
                if (payloadType == 0) {
                    resp[0] = 'F'; resp[1] = 'P'; resp[2] = 'A';
                    for (uint8_t i = 0; i < payloadLen; i++) {
                        char c = payloadPtr[2 + i];
                        if (c == ';') c = ':';
                        else if (c < 32 || c > 126) c = '.';
                        resp[3 + i] = c;
                    }
                    resp[3 + payloadLen] = 0;
                    char tail[16];
                    sprintf(tail, ",%+04d;", dbm);
                    strcat(resp, tail);

                    // Display received text message in DTMF code field on radio screen
                    uint8_t copyLen = (payloadLen < sizeof(gDTMF_RX_live) - 1) ? payloadLen : (sizeof(gDTMF_RX_live) - 1);
                    for (uint8_t k = 0; k < copyLen; k++) {
                        char c = payloadPtr[2 + k];
                        if (c < 32 || c > 126) c = ' ';
                        gDTMF_RX_live[k] = c;
                    }
                    gDTMF_RX_live[copyLen] = '\0';
                    g_FskRxIsMsg = true;
                    gDTMF_RX_live_timeout = 16; // Display for 8 seconds (16 * 500ms)
                    BACKLIGHT_TurnOn();
                    gUpdateDisplay = true;
                } else {
                    resp[0] = 'F'; resp[1] = 'P'; resp[2] = 'X';
                    uint8_t pos = 3;
                    const char *hexDigits = "0123456789ABCDEF";
                    for (uint8_t i = 0; i < payloadLen && pos < sizeof(resp) - 10; i++) {
                        uint8_t b = payloadPtr[2 + i];
                        resp[pos++] = hexDigits[b >> 4];
                        resp[pos++] = hexDigits[b & 0x0F];
                    }
                    resp[pos] = 0;
                    char tail[16];
                    sprintf(tail, ",%+04d;", dbm);
                    strcat(resp, tail);
                }
#if defined(ENABLE_UART)
                UART_SendText(UART_PORT_UART, resp);
#endif
#if defined(ENABLE_USB)
                UART_SendText(UART_PORT_VCP, resp);
#endif
            }
        }

        UART_FSK_PrepareReceive();
    }
}

static bool UART_FSK_Transmit(const uint8_t *payload, uint8_t payloadLen, uint8_t payloadType)
{
    uint8_t totalWords = g_FskPacketLen / 2;
    if (totalWords > 36) totalWords = 36;
    if (totalWords < 4)  totalWords = 4; // Minimum 4 words = 8 bytes

    uint16_t txBuffer[36];
    memset(txBuffer, 0, sizeof(txBuffer));

    txBuffer[0] = g_FskSyncWord;

    uint8_t *pBytes = (uint8_t *)&txBuffer[1];
    pBytes[0] = payloadLen;
    pBytes[1] = payloadType;

    uint8_t maxPayload = (totalWords - 2) * 2 - 2;
    if (payloadLen > maxPayload) payloadLen = maxPayload;
    memcpy(&pBytes[2], payload, payloadLen);

    txBuffer[totalWords - 1] = CRC_Calculate(&txBuffer[1], (totalWords - 2) * 2);

    if (g_FskRxMode > 0) {
        BK4819_WriteRegister(BK4819_REG_59, 0x0068);
    }

    RADIO_SetTxParameters();
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);

    SYSTEM_DelayMs(25); // Carrier and receiver squelch stabilization (pre-TX lead-in)

    BK4819_WriteRegister(BK4819_REG_70, 0x00C3);
    BK4819_WriteRegister(BK4819_REG_72, (g_FskBaud == 1) ? 0x60CA : 0x3065);
    BK4819_WriteRegister(BK4819_REG_58, (g_FskBaud == 1) ? 0x00C9 : 0x00C1);
    BK4819_WriteRegister(BK4819_REG_5A, 0xAA55);
    BK4819_WriteRegister(BK4819_REG_5B, g_FskSyncWord);
    BK4819_WriteRegister(BK4819_REG_5C, 0xAA30);
    BK4819_WriteRegister(BK4819_REG_5D, (uint16_t)(g_FskPacketLen - 1) << 8);
    BK4819_WriteRegister(0x5E, 0x3204);

    BK4819_WriteRegister(BK4819_REG_3F, BK4819_REG_3F_FSK_TX_FINISHED);

    BK4819_WriteRegister(BK4819_REG_59, 0x8068);
    BK4819_WriteRegister(BK4819_REG_59, 0x0068);

    for (unsigned int i = 0; i < totalWords; i++) {
        BK4819_WriteRegister(BK4819_REG_5F, txBuffer[i]);
    }

    SYSTEM_DelayMs(5);

    BK4819_WriteRegister(BK4819_REG_59, 0x2868);

    uint16_t timeout = 500;
    while (timeout-- && (BK4819_ReadRegister(BK4819_REG_0C) & 1u) == 0) {
        SYSTEM_DelayMs(1);
    }

    BK4819_WriteRegister(BK4819_REG_02, 0);
    SYSTEM_DelayMs(8);

    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);

    if (g_FskRxMode > 0) {
        UART_FSK_PrepareReceive();
    }

    return (timeout > 0);
}

// ---------------------------------------------------------------------------
// UART_ReportDTMF - asynchronous DTMF tone reception report
// ---------------------------------------------------------------------------
void UART_ReportDTMF(char dtmf_code)
{
    SYSTEM_DelayMs(10);

    uint8_t  active_vfo = gEeprom.RX_VFO;
    int16_t  dbm        = BK4819_GetRSSI_dBm() + dBmCorrTable[gEeprom.VfoInfo[active_vfo].Band];

    char resp[32];
    sprintf(resp, "RD%c,%+04d;", dtmf_code, dbm);

#if defined(ENABLE_UART)
    UART_SendText(UART_PORT_UART, resp);
#endif
#if defined(ENABLE_USB)
    UART_SendText(UART_PORT_VCP, resp);
#endif
}

// ---------------------------------------------------------------------------
// UART_ReportSquelch - asynchronous squelch / busy status report
// ---------------------------------------------------------------------------
void UART_ReportSquelch(bool isOpen)
{
    if (gCurrentFunction == FUNCTION_TRANSMIT)
        return;

    const char *resp = isOpen ? "BY1;" : "BY0;";

#if defined(ENABLE_UART)
    UART_SendText(UART_PORT_UART, resp);
#endif
#if defined(ENABLE_USB)
    UART_SendText(UART_PORT_VCP, resp);
#endif
}

// ---------------------------------------------------------------------------
// Process_Kenwood_CAT - Kenwood CAT ASCII command parser
// ---------------------------------------------------------------------------
static void Process_Kenwood_CAT(uint32_t Port, char *cat_buffer, uint8_t cat_pos)
{
    if (gRxIdleMode) {
        BK4819_RX_TurnOn();
        gRxIdleMode = false;
        SYSTEM_DelayMs(20);
    }

    // Skip leading whitespace
    while (*cat_buffer == ' ' || *cat_buffer == '\t' || *cat_buffer == '\r' || *cat_buffer == '\n') {
        cat_buffer++;
        if (cat_pos > 0) cat_pos--;
    }

    // Strip trailing terminators and whitespace (;, \r, \n, ' ', \t)
    while (cat_pos > 0) {
        char last = cat_buffer[cat_pos - 1];
        if (last == ';' || last == '\r' || last == '\n' || last == ' ' || last == '\t') {
            cat_buffer[--cat_pos] = 0;
        } else {
            break;
        }
    }

    if (cat_pos == 0) return;

    // Convert lowercase to uppercase (preserve text payload for FTA)
    if (strncmp(cat_buffer, "FTA", 3) != 0 && strncmp(cat_buffer, "fta", 3) != 0) {
        for (uint8_t i = 0; i < cat_pos; i++) {
            if (cat_buffer[i] >= 'a' && cat_buffer[i] <= 'z') {
                cat_buffer[i] -= 32;
            }
        }
    } else {
        for (uint8_t i = 0; i < 3; i++) {
            if (cat_buffer[i] >= 'a' && cat_buffer[i] <= 'z') {
                cat_buffer[i] -= 32;
            }
        }
    }

    // ID - transceiver identification query (Kenwood standard: ID;)
    // TS-2000 responds with ID020; (required by Hamlib, WSJT-X, flrig, etc.)
    if (strcmp(cat_buffer, "ID") == 0) {
        UART_SendText(Port, "ID020;");
        return;
    }

    // AI - auto information query/set (AI; or AI0;)
    if (strncmp(cat_buffer, "AI", 2) == 0) {
        UART_SendText(Port, "AI0;");
        return;
    }

    // VR - firmware version query (VR;)
    if (strcmp(cat_buffer, "VR") == 0) {
        UART_SendText(Port, "VR6.0.0;");
        return;
    }

    // FA/FB - frequency of VFO A or B
    bool is_fa = (strncmp(cat_buffer, "FA", 2) == 0);
    bool is_fb = (strncmp(cat_buffer, "FB", 2) == 0);
    if (is_fa || is_fb) {
        int vfo_idx = is_fa ? 0 : 1;
        if (cat_buffer[2] == 0) {
            uint32_t freqHz = gEeprom.VfoInfo[vfo_idx].pRX->Frequency * 10;
            char resp[16];
            resp[0] = 'F'; resp[1] = is_fa ? 'A' : 'B';
            UIntToText(resp, freqHz, 11, 2);
            resp[13] = ';'; resp[14] = 0;
            UART_SendText(Port, resp);
        } else {
            uint32_t freqHz = 0;
            const char *p = &cat_buffer[2];
            while (*p >= '0' && *p <= '9') {
                freqHz = (freqHz * 10) + (*p - '0');
                p++;
            }
            if (freqHz > 0) {
                if (freqHz < 10000000) {
                    freqHz *= 1000;
                }
                uint32_t newFreq = freqHz / 10;
                if (RX_freq_check(newFreq) == 0) {
                    VFO_Info_t *vfo = &gEeprom.VfoInfo[vfo_idx];
                    FREQUENCY_Band_t band = FREQUENCY_GetBand(newFreq);

                    vfo->Band                     = band;
                    vfo->freq_config_RX.Frequency = newFreq;
                    vfo->pRX->Frequency           = newFreq;
                    RADIO_ApplyOffset(vfo);
                    vfo->pTX->Frequency           = vfo->freq_config_TX.Frequency;

                    gEeprom.ScreenChannel[vfo_idx] = FREQ_CHANNEL_FIRST + band;
                    gEeprom.FreqChannel[vfo_idx]   = FREQ_CHANNEL_FIRST + band;
                    vfo->CHANNEL_SAVE              = gEeprom.ScreenChannel[vfo_idx];

                    SETTINGS_SaveChannel(vfo->CHANNEL_SAVE, vfo_idx, vfo, 2);

                    if (gRxIdleMode) {
                        BK4819_RX_TurnOn();
                        gRxIdleMode = false;
                    }

                    RADIO_ConfigureSquelchAndOutputPower(vfo);
                    RADIO_SelectVfos();
                    RADIO_SetupRegisters(true);
                    if (g_FskRxMode > 0) {
                        UART_FSK_PrepareReceive();
                    }
                    ForceScreenUpdate();
                }
            }
        }
        return;
    }

    // FR - switch or query active VFO (FR; or FR0; / FR1;)
    if (strncmp(cat_buffer, "FR", 2) == 0) {
        if (cat_buffer[2] == 0) {
            char resp[5];
            resp[0] = 'F'; resp[1] = 'R'; resp[2] = '0' + gEeprom.TX_VFO; resp[3] = ';'; resp[4] = 0;
            UART_SendText(Port, resp);
        } else if (cat_buffer[2] >= '0' && cat_buffer[2] <= '1') {
            char    vfo_char   = cat_buffer[2];
            uint8_t target_vfo = (vfo_char == '1') ? 1 : 0;
            if (gEeprom.TX_VFO != target_vfo) {
                gEeprom.TX_VFO = target_vfo;
                gEeprom.RX_VFO = target_vfo;
                CAT_ApplyAndSave(true);
            }
        }
        return;
    }

    // TXS / TS - safe transmission with automatic watchdog (default 1000 ms = 1 s)
    if (strncmp(cat_buffer, "TXS", 3) == 0 || strncmp(cat_buffer, "TS", 2) == 0) {
        if (strcmp(cat_buffer, "TXS") == 0 || strcmp(cat_buffer, "TS") == 0) {
            g_TxSafeTimeout_10ms = 100; // 100 * 10ms = 1s
            CAT_StartTransmit();
            return;
        }
        uint16_t timeout_ms = 1000;
        char *p = (strncmp(cat_buffer, "TXS", 3) == 0) ? &cat_buffer[3] : &cat_buffer[2];
        if (*p >= '0' && *p <= '9') {
            uint32_t val = 0;
            while (*p >= '0' && *p <= '9') {
                val = (val * 10) + (*p - '0');
                p++;
            }
            if (val >= 50 && val <= 30000) {
                timeout_ms = (uint16_t)val;
            }
        }
        g_TxSafeTimeout_10ms = (timeout_ms + 9) / 10;
        CAT_StartTransmit();
        return;
    }

    // TX - force persistent transmission (without auto-off watchdog)
    if (strcmp(cat_buffer, "TX") == 0) {
        g_TxSafeTimeout_10ms = 0;
        CAT_StartTransmit();
        return;
    }

    // RX - return to receive mode (also disarms Safe TX watchdog)
    if (strcmp(cat_buffer, "RX") == 0) {
        CAT_StopTransmit();
        return;
    }

    // BY - receiver busy / squelch open query (BY;)
    if (strcmp(cat_buffer, "BY") == 0) {
        char resp[5];
        resp[0] = 'B'; resp[1] = 'Y';
        resp[2] = (g_SquelchLost && gCurrentFunction != FUNCTION_TRANSMIT) ? '1' : '0';
        resp[3] = ';'; resp[4] = 0;
        UART_SendText(Port, resp);
        return;
    }

    // MO - monitor (open squelch) query or set (MO; or MO0; / MO1;)
    if (strncmp(cat_buffer, "MO", 2) == 0) {
        if (cat_buffer[2] == 0) {
            UART_SendText(Port, (gCurrentFunction == FUNCTION_MONITOR) ? "MO1;" : "MO0;");
        } else {
            char m = cat_buffer[2];
            if (m == '1') {
                if (gCurrentFunction != FUNCTION_MONITOR && gCurrentFunction != FUNCTION_TRANSMIT)
                    ACTION_Monitor();
            } else if (m == '0') {
                if (gCurrentFunction == FUNCTION_MONITOR)
                    ACTION_Monitor();
            }
        }
        return;
    }

    // MD - modulation (4=FM, 5=AM, 2=USB)
    if (strncmp(cat_buffer, "MD", 2) == 0) {
        if (cat_buffer[2] == 0) {
            uint8_t mod  = gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation;
            char resp[6] = "MD4;";
            if (mod == 1) resp[2] = '5';
            else if (mod == 2) resp[2] = '2';
            UART_SendText(Port, resp);
        } else {
            char    m          = cat_buffer[2];
            uint8_t target_mod = 0;
            if (m == '5') target_mod = 1;
            else if (m == '2') target_mod = 2;
            if (gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation != target_mod) {
                gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation = target_mod;
                if (gTxVfo) gTxVfo->Modulation = target_mod;
                CAT_ApplyAndSave(false);
            }
        }
        return;
    }

    // PC - transmitter output power query or set (PC; or PC0..7;)
    if (strncmp(cat_buffer, "PC", 2) == 0) {
        if (cat_buffer[2] == 0) {
            uint8_t cat_pwr = 6;
            switch (gEeprom.VfoInfo[gEeprom.TX_VFO].OUTPUT_POWER) {
                case OUTPUT_POWER_LOW1: cat_pwr = 0; break;
                case OUTPUT_POWER_LOW2: cat_pwr = 1; break;
                case OUTPUT_POWER_LOW3: cat_pwr = 2; break;
                case OUTPUT_POWER_LOW4: cat_pwr = 3; break;
                case OUTPUT_POWER_LOW5: cat_pwr = 4; break;
                case OUTPUT_POWER_MID:  cat_pwr = 5; break;
                case OUTPUT_POWER_HIGH: cat_pwr = 6; break;
                case OUTPUT_POWER_USER: cat_pwr = 7; break;
            }
            char resp[5];
            resp[0] = 'P'; resp[1] = 'C'; resp[2] = '0' + cat_pwr; resp[3] = ';'; resp[4] = 0;
            UART_SendText(Port, resp);
        } else {
            char p = cat_buffer[2];
            if (p >= '0' && p <= '7') {
                uint8_t cat_level   = p - '0';
                uint8_t radio_level = OUTPUT_POWER_HIGH;
                switch (cat_level) {
                    case 0: radio_level = OUTPUT_POWER_LOW1; break;
                    case 1: radio_level = OUTPUT_POWER_LOW2; break;
                    case 2: radio_level = OUTPUT_POWER_LOW3; break;
                    case 3: radio_level = OUTPUT_POWER_LOW4; break;
                    case 4: radio_level = OUTPUT_POWER_LOW5; break;
                    case 5: radio_level = OUTPUT_POWER_MID;  break;
                    case 6: radio_level = OUTPUT_POWER_HIGH; break;
                    case 7: radio_level = OUTPUT_POWER_USER; break;
                    default: radio_level = OUTPUT_POWER_HIGH; break;
                }
                if (gEeprom.VfoInfo[gEeprom.TX_VFO].OUTPUT_POWER != radio_level) {
                    gEeprom.VfoInfo[gEeprom.TX_VFO].OUTPUT_POWER = radio_level;
                    if (gTxVfo) gTxVfo->OUTPUT_POWER = radio_level;
                    CAT_ApplyAndSave(false);
                }
            }
        }
        return;
    }

    // OF - disable subtone
    if (strcmp(cat_buffer, "OF") == 0) {
        Apply_Tone_To_Active_VFO(0, 0);
        return;
    }

    // CT - CTCSS tone (4 digits, e.g. CT08850;)
    if (strncmp(cat_buffer, "CT", 2) == 0 && cat_pos >= 6) {
        uint32_t ct_freq = TextToUInt(cat_buffer, 2, 4);
        for (uint8_t i = 0; i < 50; i++) {
            if (CTCSS_Options[i] == ct_freq) {
                Apply_Tone_To_Active_VFO(1, i);
                return;
            }
        }
        return;
    }

    // DT - DCS code (3 octal digits, e.g. DT023;)
    if (strncmp(cat_buffer, "DT", 2) == 0 && cat_pos >= 5) {
        uint16_t dcs_val = ParseDCSCode(cat_buffer, 2);
        if (dcs_val != 0xFFFF) {
            for (uint8_t i = 0; i < 104; i++) {
                if (DCS_Options[i] == dcs_val) {
                    Apply_Tone_To_Active_VFO(2, i);
                    return;
                }
            }
        }
        return;
    }

    // SQ - squelch level query or set (SQ; or SQ0..9;)
    if (strncmp(cat_buffer, "SQ", 2) == 0) {
        if (cat_buffer[2] == 0) {
            char resp[5];
            resp[0] = 'S'; resp[1] = 'Q'; resp[2] = '0' + gEeprom.SQUELCH_LEVEL; resp[3] = ';'; resp[4] = 0;
            UART_SendText(Port, resp);
        } else {
            char s = cat_buffer[2];
            if (s >= '0' && s <= '9') {
                uint8_t new_sq = s - '0';
                if (gEeprom.SQUELCH_LEVEL != new_sq) {
                    gEeprom.SQUELCH_LEVEL = new_sq;
                    CAT_ApplyAndSave(true);
                }
            }
        }
        return;
    }

    // OS - offset direction query or set (OS; or OS0..2;)
    if (strncmp(cat_buffer, "OS", 2) == 0) {
        if (cat_buffer[2] == 0) {
            char resp[5];
            resp[0] = 'O'; resp[1] = 'S';
            resp[2] = '0' + gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY_DIRECTION;
            resp[3] = ';'; resp[4] = 0;
            UART_SendText(Port, resp);
        } else {
            char    d   = cat_buffer[2];
            uint8_t dir = 0;
            if (d == '1') dir = 1;
            if (d == '2') dir = 2;
            if (gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY_DIRECTION != dir) {
                gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY_DIRECTION = dir;
                if (gTxVfo) gTxVfo->TX_OFFSET_FREQUENCY_DIRECTION = dir;
                CAT_ApplyAndSave(false);
            }
        }
        return;
    }

    // OV - offset frequency query or set (OV; or OV<offset11>;)
    if (strncmp(cat_buffer, "OV", 2) == 0) {
        if (cat_buffer[2] == 0) {
            uint32_t offsetHz = gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY * 10;
            char resp[16];
            resp[0] = 'O'; resp[1] = 'V';
            UIntToText(resp, offsetHz, 11, 2);
            resp[13] = ';'; resp[14] = 0;
            UART_SendText(Port, resp);
        } else if (cat_pos >= 13) {
            uint32_t offsetHz = TextToUInt(cat_buffer, 2, 11);
            if (gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY != offsetHz / 10) {
                gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY = offsetHz / 10;
                if (gTxVfo) gTxVfo->TX_OFFSET_FREQUENCY = offsetHz / 10;
                CAT_ApplyAndSave(false);
            }
        }
        return;
    }

    // IF - transceiver status & active VFO information
    if (strcmp(cat_buffer, "IF") == 0) {
        char resp[40];
        memset(resp, ' ', 38);
        resp[0] = 'I'; resp[1] = 'F';

        uint32_t freqHz = 0;
        if (gTxVfo)
            freqHz = gTxVfo->pRX->Frequency * 10;
        else
            freqHz = gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->Frequency * 10;

        UIntToText(resp, freqHz, 11, 2);
        resp[13] = '0'; resp[14] = '0'; resp[15] = '0'; resp[16] = '0'; resp[17] = '0';

        uint8_t mod  = gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation;
        resp[29] = (mod == 1) ? '5' : '4';
        bool is_tx   = (gCurrentFunction == FUNCTION_TRANSMIT);
        resp[30] = is_tx ? '1' : '0';
        resp[31] = ';'; resp[32] = 0;
        UART_SendText(Port, resp);
        return;
    }

    // RA / QS - Radio All Settings / Quick Status query (minimal CPU overhead)
    if (strcmp(cat_buffer, "RA") == 0 || strcmp(cat_buffer, "QS") == 0) {
        char resp[120];
        uint32_t rxFreqHz = (gTxVfo ? gTxVfo->pRX->Frequency : gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->Frequency) * 10;
        uint32_t txFreqHz = (gTxVfo ? gTxVfo->pTX->Frequency : gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->Frequency) * 10;
        uint8_t shift_dir = gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY_DIRECTION;
        uint32_t offsetHz = gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY * 10;
        uint8_t mod_raw   = gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation;
        uint8_t mod       = (mod_raw == 1) ? 5 : (mod_raw == 2 ? 2 : 4);

        uint8_t cat_pwr = 6;
        switch (gEeprom.VfoInfo[gEeprom.TX_VFO].OUTPUT_POWER) {
            case OUTPUT_POWER_LOW1: cat_pwr = 0; break;
            case OUTPUT_POWER_LOW2: cat_pwr = 1; break;
            case OUTPUT_POWER_LOW3: cat_pwr = 2; break;
            case OUTPUT_POWER_LOW4: cat_pwr = 3; break;
            case OUTPUT_POWER_LOW5: cat_pwr = 4; break;
            case OUTPUT_POWER_MID:  cat_pwr = 5; break;
            case OUTPUT_POWER_HIGH: cat_pwr = 6; break;
            case OUTPUT_POWER_USER: cat_pwr = 7; break;
        }

        uint8_t bw = (gEeprom.VfoInfo[gEeprom.TX_VFO].CHANNEL_BANDWIDTH == BANDWIDTH_NARROW) ? 1 : 0;
        uint8_t sq = gEeprom.SQUELCH_LEVEL;
        uint8_t busy_lock = g_FskBusyLock ? 1 : 0;

        uint8_t tx_t_type = gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->CodeType;
        uint16_t tx_t_val = 0;
        uint8_t tx_code   = gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->Code;
        if (tx_t_type == CODE_TYPE_CONTINUOUS_TONE && tx_code < 50) {
            tx_t_val = CTCSS_Options[tx_code];
        } else if ((tx_t_type == CODE_TYPE_DIGITAL || tx_t_type == CODE_TYPE_REVERSE_DIGITAL) && tx_code < 104) {
            uint16_t dcs = DCS_Options[tx_code];
            tx_t_val = ((dcs >> 6) & 7) * 100 + ((dcs >> 3) & 7) * 10 + (dcs & 7);
        }

        uint8_t rx_t_type = gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->CodeType;
        uint16_t rx_t_val = 0;
        uint8_t rx_code   = gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->Code;
        if (rx_t_type == CODE_TYPE_CONTINUOUS_TONE && rx_code < 50) {
            rx_t_val = CTCSS_Options[rx_code];
        } else if ((rx_t_type == CODE_TYPE_DIGITAL || rx_t_type == CODE_TYPE_REVERSE_DIGITAL) && rx_code < 104) {
            uint16_t dcs = DCS_Options[rx_code];
            rx_t_val = ((dcs >> 6) & 7) * 100 + ((dcs >> 3) & 7) * 10 + (dcs & 7);
        }

        uint8_t is_tx    = (gCurrentFunction == FUNCTION_TRANSMIT) ? 1 : 0;
        uint8_t sql_open = g_SquelchLost ? 1 : 0;
        uint16_t bat_mv  = gBatteryVoltageAverage * 10;
        uint8_t bat_pct  = BATTERY_VoltsToPercent(gBatteryVoltageAverage);
        int16_t rssi_dbm = UART_GetRSSI_dBm();

        char cmd0 = cat_buffer[0];
        char cmd1 = cat_buffer[1];

        sprintf(resp, "%c%c,%011u,%011u,%u,%011u,%u,%u,%u,%u,%u,%u,%04u,%u,%04u,%u,%u,%04u,%03u,%+04d,%u,%u,%02u;",
                cmd0, cmd1,
                rxFreqHz, txFreqHz, shift_dir, offsetHz, mod, cat_pwr, bw, sq, busy_lock,
                tx_t_type, tx_t_val, rx_t_type, rx_t_val,
                is_tx, sql_open, bat_mv, bat_pct, rssi_dbm,
                g_FskRxMode, (g_FskBaud == 1) ? 2400 : 1200, g_FskPacketLen);
        UART_SendText(Port, resp);
        return;
    }

    // SM - quick signal measurement at specified frequency (or active VFO if none specified: SM; / SM0;)
    if (strncmp(cat_buffer, "SM", 2) == 0) {
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            UART_SendText(Port, "SM,busy;");
            return;
        }

        uint32_t freqHz;
        bool custom_freq = false;
        if (cat_pos >= 13) {
            freqHz = TextToUInt(cat_buffer, 2, 11);
            custom_freq = true;
        } else {
            // SM or SM0 -> measure current active VFO
            uint8_t active_vfo = gEeprom.RX_VFO;
            freqHz = gEeprom.VfoInfo[active_vfo].pRX->Frequency * 10;
        }

        uint32_t target_freq  = freqHz / 10;
        uint32_t current_freq = gEeprom.VfoInfo[gEeprom.RX_VFO].pRX->Frequency;
        uint8_t  target_band  = FREQUENCY_GetBand(target_freq);

        if (custom_freq && current_freq != target_freq) {
            BK4819_SetFrequency(target_freq);
            BK4819_PickRXFilterPathBasedOnFrequency(target_freq);
            BK4819_RX_TurnOn();
            SYSTEM_DelayMs(60);
        } else {
            if (gRxIdleMode) {
                BK4819_RX_TurnOn();
                gRxIdleMode = false;
                SYSTEM_DelayMs(10);
            }
        }

        int16_t dbm = BK4819_GetRSSI_dBm() + dBmCorrTable[target_band];
        uint8_t sq  = g_SquelchLost ? 1 : 0;

        if (custom_freq && current_freq != target_freq) {
            BK4819_SetFrequency(current_freq);
            BK4819_PickRXFilterPathBasedOnFrequency(current_freq);
            BK4819_RX_TurnOn();
            if (g_FskRxMode > 0) {
                UART_FSK_PrepareReceive();
            }
        }

        char resp[32];
        sprintf(resp, "SM%011u,%+04d,%d;", freqHz, dbm, sq);
        UART_SendText(Port, resp);
        return;
    }

    // S1 - read RSSI of current active VFO
    if (strcmp(cat_buffer, "S1") == 0) {
        if (gRxIdleMode) {
            BK4819_RX_TurnOn();
            gRxIdleMode = false;
            SYSTEM_DelayMs(10);
        }
        uint8_t  active_vfo = gEeprom.RX_VFO;
        int16_t  dbm        = BK4819_GetRSSI_dBm() + dBmCorrTable[gEeprom.VfoInfo[active_vfo].Band];
        uint8_t  sq         = g_SquelchLost ? 1 : 0;

        char resp[32];
        sprintf(resp, "S1,%d,%+04d,%d;", active_vfo, dbm, sq);
        UART_SendText(Port, resp);
        return;
    }

    // SL - load one channel into hardware scan list (SL<idx2><freq11>;)
    if (strncmp(cat_buffer, "SL", 2) == 0 && cat_pos >= 13) {
        uint8_t  idx    = TextToUInt(cat_buffer, 2, 2);
        uint8_t  f_offset = (cat_buffer[4] == ',') ? 5 : 4;
        uint32_t freqHz = TextToUInt(cat_buffer, f_offset, 11);
        if (idx < MAX_UART_SCAN_LIST)
            g_UartScanList[idx] = freqHz / 10;
        UART_SendText(Port, "SL_OK;");
        return;
    }

    // SCF - fast hardware measurement on demand (asynchronous)
    if (strncmp(cat_buffer, "SCF", 3) == 0 && cat_pos >= 14) {
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            UART_SendText(Port, "SQ,busy;");
            return;
        }
        if (g_SingleScanState != 0) {
            UART_SendText(Port, "SQ,busy;");
            return;
        }

        uint32_t freqHz      = TextToUInt(cat_buffer, 3, 11);
        uint32_t target_freq = freqHz / 10;
        uint8_t  ticks       = 25;

        if (cat_pos >= 16 && cat_buffer[14] == ',') {
            uint8_t ticks_len = cat_pos - 15;
            if (ticks_len > 0 && ticks_len <= 3)
                ticks = (uint8_t)TextToUInt(cat_buffer, 15, ticks_len);
        }

        VFO_Info_t *vfo          = &gEeprom.VfoInfo[gEeprom.RX_VFO];
        g_SingleScanOriginalFreq = vfo->pRX->Frequency;
        g_SingleScanOriginalBand = vfo->Band;
        g_SingleScanTargetFreq   = target_freq;
        g_SingleScanPort         = Port;

        vfo->pRX->Frequency = target_freq;
        vfo->Band           = FREQUENCY_GetBand(target_freq);

        RADIO_ConfigureSquelchAndOutputPower(vfo);
        RADIO_SetupRegisters(true);
        gUpdateDisplay = true;

        g_SingleScanDelay_10ms = ticks;
        g_SingleScanState      = 1;
        return;
    }

    // SC - start ultrafast group scan (SC<count2>;)
    if (strncmp(cat_buffer, "SC", 2) == 0 && cat_buffer[2] != 'F' && cat_pos >= 4) {
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            UART_SendText(Port, "SC,busy;");
            return;
        }

        g_UartScanCount = (uint8_t)TextToUInt(cat_buffer, 2, 2);
        if (g_UartScanCount > MAX_UART_SCAN_LIST) g_UartScanCount = MAX_UART_SCAN_LIST;
        if (g_UartScanCount == 0) return;

        VFO_Info_t *vfo        = &gEeprom.VfoInfo[gEeprom.RX_VFO];
        g_UartScanOriginalFreq = vfo->pRX->Frequency;
        g_UartScanOriginalBand = vfo->Band;
        g_UartScanPort         = Port;

        strcpy(g_UartScanResponse, "SR");
        g_UartScanIndex      = 0;
        g_UartScanDelay_10ms = 0;
        g_UartScanActive     = true;
        return;
    }

    // RD - enable/disable/query automatic periodic RSSI reporting (RD; or RD0; / RD1;)
    if (strncmp(cat_buffer, "RD", 2) == 0) {
        if (cat_buffer[2] == 0) {
            char resp[5];
            resp[0] = 'R'; resp[1] = 'D'; resp[2] = g_AutoReportRSSI ? '1' : '0'; resp[3] = ';'; resp[4] = 0;
            UART_SendText(Port, resp);
        } else {
            char d = cat_buffer[2];
            if (d == '1')      g_AutoReportRSSI = true;
            else if (d == '0') g_AutoReportRSSI = false;
        }
        return;
    }

    // FE - enable / disable / query FSK receiver (FE0; FE1; FE2; FE;)
    if (strncmp(cat_buffer, "FE", 2) == 0) {
        if (cat_buffer[2] == 0) {
            char resp[8];
            sprintf(resp, "FE%d;", g_FskRxMode);
            UART_SendText(Port, resp);
        } else {
            char m = cat_buffer[2];
            if (m >= '0' && m <= '2') {
                uint8_t mode = m - '0';
                if (mode == 0) {
                    UART_FSK_Disable();
                } else {
                    g_FskRxMode = mode;
                    UART_FSK_PrepareReceive();
                    RADIO_SetupRegisters(true);
                }
                UART_SendText(Port, "FE_OK;");
            } else {
                UART_SendText(Port, "FE_ERR;");
            }
        }
        return;
    }

    // FC - configure FSK modem (FC; or FC<baud>,<sync4hex>,<len>;)
    if (strncmp(cat_buffer, "FC", 2) == 0) {
        if (cat_buffer[2] == 0) {
            char resp[32];
            sprintf(resp, "FC%d,%04X,%d;", (g_FskBaud == 1) ? 2400 : 1200, g_FskSyncWord, g_FskPacketLen);
            UART_SendText(Port, resp);
        } else {
            char *p = &cat_buffer[2];
            uint8_t baud = 0;
            bool baudOk = false;
            if (strncmp(p, "2400,", 5) == 0) {
                baud = 1;
                p += 5;
                baudOk = true;
            } else if (strncmp(p, "1200,", 5) == 0) {
                baud = 0;
                p += 5;
                baudOk = true;
            } else if (*p == '1' && *(p + 1) == ',') {
                baud = 1;
                p += 2;
                baudOk = true;
            } else if (*p == '0' && *(p + 1) == ',') {
                baud = 0;
                p += 2;
                baudOk = true;
            }

            if (baudOk) {
                uint16_t sync = 0;
                int hexCount = 0;
                while (hexCount < 4) {
                    char c = *p++;
                    uint8_t nibble;
                    if (c >= '0' && c <= '9') nibble = c - '0';
                    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
                    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
                    else break;
                    sync = (sync << 4) | nibble;
                    hexCount++;
                }
                if (hexCount == 4 && *p == ',') {
                    p++;
                    uint32_t len = 0;
                    while (*p >= '0' && *p <= '9') {
                        len = (len * 10) + (*p - '0');
                        p++;
                    }
                    if ((*p == 0 || *p == ';') && len >= 8 && len <= 100 && (len % 2 == 0)) {
                        g_FskBaud = baud;
                        g_FskSyncWord = sync;
                        g_FskPacketLen = (uint8_t)len;
                        if (g_FskRxMode > 0) {
                            UART_FSK_PrepareReceive();
                            RADIO_SetupRegisters(true);
                        }
                        UART_SendText(Port, "FC_OK;");
                        return;
                    }
                }
            }
            UART_SendText(Port, "FC_ERR;");
        }
        return;
    }

    // FM - busy channel lockout policy (FM0; FM1; FM;)
    if (strncmp(cat_buffer, "FM", 2) == 0) {
        if (cat_buffer[2] == 0) {
            char resp[8];
            sprintf(resp, "FM%d;", g_FskBusyLock ? 1 : 0);
            UART_SendText(Port, resp);
        } else {
            char m = cat_buffer[2];
            if (m == '0' || m == '1') {
                g_FskBusyLock = (m == '1');
                UART_SendText(Port, "FM_OK;");
            } else {
                UART_SendText(Port, "FM_ERR;");
            }
        }
        return;
    }

    // FTA - transmit ASCII text (FTA<text>;)
    if (strncmp(cat_buffer, "FTA", 3) == 0) {
        uint8_t textLen = (cat_pos > 3) ? (cat_pos - 3) : 0;
        if (textLen == 0) {
            UART_SendText(Port, "FT_ERR;");
            return;
        }
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            UART_SendText(Port, "FT_BUSY;");
            return;
        }
        if (g_FskBusyLock && g_SquelchLost) {
            UART_SendText(Port, "FT_BUSY;");
            return;
        }
        uint8_t totalWords = g_FskPacketLen / 2;
        if (totalWords > 36) totalWords = 36;
        if (totalWords < 4)  totalWords = 4;
        uint8_t maxPayload = (totalWords - 2) * 2 - 2;
        if (textLen > maxPayload) textLen = maxPayload;

        bool ok = UART_FSK_Transmit((const uint8_t *)&cat_buffer[3], textLen, 0);
        UART_SendText(Port, ok ? "FT_OK;" : "FT_ERR;");
        return;
    }

    // FTX - transmit raw binary data in HEX format (FTX<hex>;)
    if (strncmp(cat_buffer, "FTX", 3) == 0) {
        uint8_t hexLen = (cat_pos > 3) ? (cat_pos - 3) : 0;
        if (hexLen == 0 || (hexLen % 2 != 0)) {
            UART_SendText(Port, "FT_ERR;");
            return;
        }
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            UART_SendText(Port, "FT_BUSY;");
            return;
        }
        if (g_FskBusyLock && g_SquelchLost) {
            UART_SendText(Port, "FT_BUSY;");
            return;
        }
        uint8_t byteCount = hexLen / 2;
        uint8_t totalWords = g_FskPacketLen / 2;
        if (totalWords > 36) totalWords = 36;
        if (totalWords < 4)  totalWords = 4;
        uint8_t maxPayload = (totalWords - 2) * 2 - 2;
        if (byteCount > maxPayload) byteCount = maxPayload;

        uint8_t binBuffer[64];
        for (uint8_t i = 0; i < byteCount; i++) {
            char h = cat_buffer[3 + i * 2];
            char l = cat_buffer[3 + i * 2 + 1];
            uint8_t nH = (h >= '0' && h <= '9') ? (h - '0') : ((h >= 'A' && h <= 'F') ? (h - 'A' + 10) : ((h >= 'a' && h <= 'f') ? (h - 'a' + 10) : 0));
            uint8_t nL = (l >= '0' && l <= '9') ? (l - '0') : ((l >= 'A' && l <= 'F') ? (l - 'A' + 10) : ((l >= 'a' && l <= 'f') ? (l - 'a' + 10) : 0));
            binBuffer[i] = (nH << 4) | nL;
        }

        bool ok = UART_FSK_Transmit(binBuffer, byteCount, 1);
        UART_SendText(Port, ok ? "FT_OK;" : "FT_ERR;");
        return;
    }

    // HELPJ - show JSON commands schema with parameters, ranges and allowed values
    if (strncmp(cat_buffer, "HELPJ", 5) == 0) {
        static const char g_CatHelpJson[] =
            "{\"commands\":[\"FA\",\"FB\",\"FR\",\"TX\",\"TXS\",\"TS\",\"RX\",\"MO\",\"MD\",\"PC\","
            "\"OF\",\"CT\",\"DT\",\"SQ\",\"BY\",\"OS\",\"OV\",\"IF\",\"RA\",\"QS\",\"SM\",\"S1\","
            "\"SL\",\"SC\",\"SCF\",\"RD\",\"FE\",\"FC\",\"FM\",\"FTA\",\"FTX\",\"ID\",\"AI\",\"VR\",\"HELP\",\"HELPJ\"]};\r\n";
        UART_SendText(Port, g_CatHelpJson);
        return;
    }

    // HELP - show human-readable list of all CAT commands
    if (strncmp(cat_buffer, "HELP", 4) == 0) {
        static const char g_CatHelpText[] =
            "=== CAT COMMANDS ===\r\n"
            "FA/FB[f11]; : VFO A/B freq (11d Hz)\r\n"
            "FR[0|1];    : Active VFO (0=A, 1=B)\r\n"
            "TX;/RX;     : PTT on/off\r\n"
            "TXS/TS[ms]; : Safe TX watchdog (50-30000ms)\r\n"
            "MO[0|1];    : Monitor (0=off, 1=on)\r\n"
            "MD[2|4|5];  : Mode (2=USB, 4=FM, 5=AM)\r\n"
            "PC[0-7];    : Power (0=20mW..6=5W, 7=User)\r\n"
            "OF;         : Tone off\r\n"
            "CT<t4>;/DT<c3>; : CTCSS/DCS tone\r\n"
            "SQ[0-9];    : Squelch level (0-9)\r\n"
            "BY;         : Squelch status (0=shut, 1=open)\r\n"
            "OS[0-2];/OV[f11]; : Offset dir/freq\r\n"
            "IF;/RA;/QS; : Status / full CSV telemetry\r\n"
            "SM<f>;/S1;  : RSSI query (freq / active)\r\n"
            "SL/SC/SCF;  : Scanner (load/group/fast)\r\n"
            "RD[0|1];    : Auto RSSI report (periodic RR)\r\n"
            "FE[0-2];    : FSK modem (0=off, 1=on, 2=mute)\r\n"
            "FC<b,s,l>;  : FSK cfg (baud,sync,len e.g. 2400,ABCD,32)\r\n"
            "FM[0|1];    : FSK busy lockout (0=off, 1=on)\r\n"
            "FTA<t>;/FTX<h>; : FSK TX text/hex data\r\n"
            "ID;         : Kenwood ID (ID020;)\r\n"
            "AI[0|1];/VR;: Auto-info / Version\r\n"
            "HELP;/HELPJ;: Help / JSON schema\r\n;\r\n";
        UART_SendText(Port, g_CatHelpText);
        return;
    }

    // FT fallback (e.g. sent FT without A or X)
    if (strncmp(cat_buffer, "FT", 2) == 0 && cat_buffer[2] != 'A' && cat_buffer[2] != 'X') {
        UART_SendText(Port, "FT_ERR;");
        return;
    }

    // Kenwood standard fallback: return ?; for unrecognized commands
    UART_SendText(Port, "?;");
}

#endif // ENABLE_CAT
// ============================================================
// === End of CAT Block
// ============================================================

#ifdef ENABLE_USB
static void SendReply_VCP(void *pReply, uint16_t Size)
{
    static uint8_t VCP_ReplyBuf[MAX_REPLY_SIZE + sizeof(Header_t) + sizeof(Footer_t)]
        __attribute__((aligned(4)));

    // !!
    if (Size > MAX_REPLY_SIZE)
    {
        return;
    }

    uint8_t *pBody   = VCP_ReplyBuf + sizeof(Header_t);
    uint8_t *pFooter = pBody + Size;

    memcpy(pBody, pReply, Size);
    pReply = pBody;

    if (bIsEncrypted)
    {
        uint8_t     *pBytes = (uint8_t *)pReply;
        unsigned int i;
        for (i = 0; i < Size; i++)
            pBytes[i] ^= Obfuscation[i % 16];
    }

    /* Build the transport header/footer byte by byte. The reply body may have
     * an odd size, so pFooter is not necessarily half-word aligned; casting it
     * to Footer_t and storing ID as uint16_t can HardFault on Cortex-M0+. */
    VCP_ReplyBuf[0] = 0xAB;
    VCP_ReplyBuf[1] = 0xCD;
    VCP_ReplyBuf[2] = (uint8_t)(Size & 0xFFu);
    VCP_ReplyBuf[3] = (uint8_t)(Size >> 8);

    // VCP_Send((uint8_t *)&Header, sizeof(Header));
    // VCP_Send(pReply, Size);

    if (bIsEncrypted)
    {
        pFooter[0] = Obfuscation[(Size + 0) % 16] ^ 0xFF;
        pFooter[1] = Obfuscation[(Size + 1) % 16] ^ 0xFF;
    }
    else
    {
        pFooter[0] = 0xFF;
        pFooter[1] = 0xFF;
    }
    pFooter[2] = 0xDC;
    pFooter[3] = 0xBA;

    // VCP_Send((uint8_t *)&Footer, sizeof(Footer));

    VCP_SendAsync(VCP_ReplyBuf, sizeof(Header_t) + Size + sizeof(Footer_t));
}
#endif // ENABLE_USB

static void SendReply(uint32_t Port, void *pReply, uint16_t Size)
{
#if defined(ENABLE_USB)
    if (Port == UART_PORT_VCP)
    {
        SendReply_VCP(pReply, Size);
        return;
    }
#endif

#if defined(ENABLE_UART)
    Header_t Header;
    Footer_t Footer;

    if (bIsEncrypted)
    {
        uint8_t     *pBytes = (uint8_t *)pReply;
        unsigned int i;
        for (i = 0; i < Size; i++)
            pBytes[i] ^= Obfuscation[i % 16];
    }

    Header.ID = 0xCDAB;
    Header.Size = Size;

    UART_Send(&Header, sizeof(Header));
    UART_Send(pReply, Size);

    if (bIsEncrypted)
    {
        Footer.Padding[0] = Obfuscation[(Size + 0) % 16] ^ 0xFF;
        Footer.Padding[1] = Obfuscation[(Size + 1) % 16] ^ 0xFF;
    }
    else
    {
        Footer.Padding[0] = 0xFF;
        Footer.Padding[1] = 0xFF;
    }
    Footer.ID = 0xBADC;

    UART_Send(&Footer, sizeof(Footer));
#endif
}

static void SendVersion(uint32_t Port)
{
    REPLY_0514_t Reply;

    Reply.Data.Padding[0] = Reply.Data.Padding[1] = 0;
    Reply.Header.ID = 0x0515;
    Reply.Header.Size = sizeof(Reply.Data);
    strncpy(Reply.Data.Version, Version, sizeof(Reply.Data.Version));
    Reply.Data.bHasCustomAesKey = bHasCustomAesKey;
    Reply.Data.bIsInLockScreen = bIsInLockScreen;
    Reply.Data.Challenge[0] = gChallenge[0];
    Reply.Data.Challenge[1] = gChallenge[1];
    Reply.Data.Challenge[2] = gChallenge[2];
    Reply.Data.Challenge[3] = gChallenge[3];

    SendReply(Port, &Reply, sizeof(Reply));
}

#ifndef ENABLE_FEAT_F4HWN
static bool IsBadChallenge(const uint32_t *pKey, const uint32_t *pIn, const uint32_t *pResponse)
{
    // PY32 has no AES hardware
    /*
    unsigned int i;
    uint32_t     IV[4];

    IV[0] = 0;
    IV[1] = 0;
    IV[2] = 0;
    IV[3] = 0;

    AES_Encrypt(pKey, IV, pIn, IV, true);

    for (i = 0; i < 4; i++)
        if (IV[i] != pResponse[i])
            return true;
    */

    return false;
}
#endif

// session init, sends back version info and state
// timestamp is a session id really
static void CMD_0514(uint32_t Port, const uint8_t *pBuffer)
{
    const CMD_0514_t *pCmd = (const CMD_0514_t *)pBuffer;

    if(0) {}
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART)
    {
        UART_Timestamp = pCmd->Timestamp;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP)
    {
        VCP_Timestamp = pCmd->Timestamp;
    }
#endif

#ifdef ENABLE_FMRADIO_EMBEDDED
    gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
#endif

    gSerialConfigCountDown_500ms = 12; // 6 sec

    // Backlight left untouched: a serial session is neutral, so the normal BLTime
    // inactivity countdown keeps running from the last keypress (no forced turn-off).

    SendVersion(Port);
}

// read eeprom
static void CMD_051B(uint32_t Port, const uint8_t *pBuffer)
{
    const CMD_051B_t *pCmd = (const CMD_051B_t *)pBuffer;
    REPLY_051B_t      Reply;
    bool              bLocked = false;

    uint32_t Timestamp = 0;

    if(0) {}
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART)
    {
        Timestamp = UART_Timestamp;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP)
    {
        Timestamp = VCP_Timestamp;
    }
#endif
    else
    {
        return;
    }

    if (pCmd->Timestamp != Timestamp)
        return;

    gSerialConfigCountDown_500ms = 12; // 6 sec

    #ifdef ENABLE_FMRADIO_EMBEDDED
        gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
    #endif

    // Reject reads that do not fit in the fixed-size reply buffer.
    if (pCmd->Size > sizeof(Reply.Data.Data))
        return;

    memset(&Reply, 0, sizeof(Reply));
    Reply.Header.ID   = 0x051C;
    Reply.Header.Size = pCmd->Size + 4;
    Reply.Data.Offset = pCmd->Offset;
    Reply.Data.Size   = pCmd->Size;

    if (bHasCustomAesKey)
        bLocked = gIsLocked;

    if (!bLocked)
    {
        EEPROM_ReadBuffer(pCmd->Offset, Reply.Data.Data, pCmd->Size);
    }

    SendReply(Port, &Reply, pCmd->Size + 8);
}

// write eeprom
static void CMD_051D(uint32_t Port, const uint8_t *pBuffer)
{
    const CMD_051D_t *pCmd = (const CMD_051D_t *)pBuffer;
    REPLY_051D_t Reply;
    bool bReloadEeprom;
    bool bIsLocked;

    uint32_t Timestamp = 0;

    /* Bound the write against the received frame: Data[] must hold pCmd->Size
     * bytes, otherwise the loop below would read adjacent RAM and persist it to
     * EEPROM (memory disclosure). A non-multiple-of-8 Size is NOT rejected: the
     * Size/8 loop simply truncates the sub-page tail, matching the historical
     * behavior CHIRP relies on for its final (unaligned) config block. */
    if (pCmd->Header.Size < 8u + pCmd->Size)
        return;

    if(0) {}
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART)
    {
        Timestamp = UART_Timestamp;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP)
    {
        Timestamp = VCP_Timestamp;
    }
#endif
    else
    {
        return;
    }

    if (pCmd->Timestamp != Timestamp)
        return;

    gSerialConfigCountDown_500ms = 12; // 6 sec
    
    bReloadEeprom = false;

    #ifdef ENABLE_FMRADIO_EMBEDDED
        gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
    #endif

    Reply.Header.ID   = 0x051E;
    Reply.Header.Size = sizeof(Reply.Data);
    Reply.Data.Offset = pCmd->Offset;

    bIsLocked = bHasCustomAesKey ? gIsLocked : false;

    if (!bIsLocked)
    {
        unsigned int i;
        for (i = 0; i < (pCmd->Size / 8); i++)
        {
            const uint16_t Offset = pCmd->Offset + (i * 8U);

            if (Offset >= 0x0F30 && Offset < 0x0F40)
                if (!gIsLocked)
                    bReloadEeprom = true;

            if ((Offset < 0x0E98 || Offset >= 0x0EA0) || !bIsInLockScreen || pCmd->bAllowPassword)
            {    
                EEPROM_WriteBuffer(Offset, &pCmd->Data[i * 8U], 8);
            }
        }

        if (bReloadEeprom)
            SETTINGS_InitEEPROM();
    }

    SendReply(Port, &Reply, sizeof(Reply));
}

#ifdef ENABLE_EXTRA_UART_CMD
// read RSSI
static void CMD_0527(uint32_t Port)
{
    REPLY_0527_t Reply;

    Reply.Header.ID             = 0x0528;
    Reply.Header.Size           = sizeof(Reply.Data);
    Reply.Data.RSSI             = BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
    Reply.Data.ExNoiseIndicator = BK4819_ReadRegister(BK4819_REG_65) & 0x007F;
    Reply.Data.GlitchIndicator  = BK4819_ReadRegister(BK4819_REG_63);

    SendReply(Port, &Reply, sizeof(Reply));
}

// read ADC
static void CMD_0529(uint32_t Port)
{
    REPLY_0529_t Reply;

    Reply.Header.ID   = 0x52A;
    Reply.Header.Size = sizeof(Reply.Data);

    // Original doesn't actually send current!
    BOARD_ADC_GetBatteryInfo(&Reply.Data.Voltage, &Reply.Data.Current);

    SendReply(Port, &Reply, sizeof(Reply));
}

#ifndef ENABLE_FEAT_F4HWN
static void CMD_052D(uint32_t Port, const uint8_t *pBuffer)
{
    const CMD_052D_t *pCmd = (const CMD_052D_t *)pBuffer;
    REPLY_052D_t      Reply;
    bool              bIsLocked;

    #ifdef ENABLE_FMRADIO_EMBEDDED
        gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
    #endif
    Reply.Header.ID   = 0x052E;
    Reply.Header.Size = sizeof(Reply.Data);

    bIsLocked = bHasCustomAesKey;

    if (!bIsLocked)
        bIsLocked = IsBadChallenge(gCustomAesKey, gChallenge, pCmd->Response);

    if (!bIsLocked)
    {
        bIsLocked = IsBadChallenge(gDefaultAesKey, gChallenge, pCmd->Response);
        if (bIsLocked)
            gTryCount++;
    }

    if (gTryCount < 3)
    {
        if (!bIsLocked)
            gTryCount = 0;
    }
    else
    {
        gTryCount = 3;
        bIsLocked = true;
    }
    
    gIsLocked            = bIsLocked;
    Reply.Data.bIsLocked = bIsLocked;
    Reply.Data.Padding[0] = Reply.Data.Padding[1] = Reply.Data.Padding[2] = 0;

    SendReply(Port, &Reply, sizeof(Reply));
}
#endif

// session init, sends back version info and state
// timestamp is a session id really
// this command also disables dual watch, crossband, 
// DTMF side tones, freq reverse, PTT ID, DTMF decoding, frequency offset
// exits power save, sets main VFO to upper,
static void CMD_052F(uint32_t Port, const uint8_t *pBuffer)
{
    const CMD_052F_t *pCmd = (const CMD_052F_t *)pBuffer;

    gEeprom.DUAL_WATCH                               = DUAL_WATCH_OFF;
    gEeprom.CROSS_BAND_RX_TX                         = CROSS_BAND_OFF;
    gEeprom.RX_VFO                                   = 0;
    gEeprom.DTMF_SIDE_TONE                           = false;
    gEeprom.VfoInfo[0].FrequencyReverse              = false;
    gEeprom.VfoInfo[0].pRX                           = &gEeprom.VfoInfo[0].freq_config_RX;
    gEeprom.VfoInfo[0].pTX                           = &gEeprom.VfoInfo[0].freq_config_TX;
    gEeprom.VfoInfo[0].TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_OFF;
    gEeprom.VfoInfo[0].DTMF_PTT_ID_TX_MODE           = PTT_ID_OFF;
#ifdef ENABLE_DTMF_CALLING
    gEeprom.VfoInfo[0].DTMF_DECODING_ENABLE          = false;
#endif

    #ifdef ENABLE_NOAA
        gIsNoaaMode = false;
    #endif

    if (gCurrentFunction == FUNCTION_POWER_SAVE)
        FUNCTION_Select(FUNCTION_FOREGROUND);

    gSerialConfigCountDown_500ms = 12; // 6 sec

    if(0) {}
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART)
    {
        UART_Timestamp = pCmd->Timestamp;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP)
    {
        VCP_Timestamp = pCmd->Timestamp;
    }
#endif

    // Backlight left untouched: a serial session is neutral, so the normal BLTime
    // inactivity countdown keeps running from the last keypress (no forced turn-off).

    SendVersion(Port);
}
#endif

#ifdef ENABLE_UART_RW_BK_REGS
static void CMD_0601_ReadBK4819Reg(uint32_t Port, const uint8_t *pBuffer)
{
    typedef struct  __attribute__((__packed__)) {
        Header_t header;
        uint8_t reg;
    } CMD_0601_t;

    CMD_0601_t *cmd = (CMD_0601_t*) pBuffer;

    struct __attribute__((__packed__)) {
        Header_t header;
        struct __attribute__((__packed__)) {
            uint8_t reg;
            uint16_t value;
        } data;
    } reply;

    reply.header.ID = 0x0601;
    reply.header.Size = sizeof(reply.data);
    reply.data.reg = cmd->reg;
    reply.data.value = BK4819_ReadRegister(cmd->reg);
    SendReply(Port, &reply, sizeof(reply));
}

static void CMD_0602_WriteBK4819Reg(const uint8_t *pBuffer)
{
    typedef struct __attribute__((__packed__)) {
        Header_t header;
        uint8_t reg;
        uint16_t value;
    } CMD_0602_t;

    CMD_0602_t *cmd = (CMD_0602_t*) pBuffer;
    BK4819_WriteRegister(cmd->reg, cmd->value);
}
#endif

bool UART_IsCommandAvailable(uint32_t Port)
{
    uint16_t Index;
    uint16_t TailIndex;
    uint16_t Size;
    uint16_t Crc;
    uint16_t CommandLength;
    uint16_t DmaLength;
    uint8_t *ReadBuf;
    uint16_t ReadBufSize;
    uint16_t *pReadPointer;
    UART_Command_t *pUART_Command;

    if(0){}
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART)
    {
        DmaLength = (sizeof(UART_DMA_Buffer) - LL_DMA_GetDataLength(DMA1, DMA_CHANNEL)) % sizeof(UART_DMA_Buffer);
        ReadBuf = UART_DMA_Buffer;
        ReadBufSize = sizeof(UART_DMA_Buffer);
        pReadPointer = &gUART_WriteIndex;
        pUART_Command = &UART_Command;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP)
    {
        DmaLength = VCP_RxBufPointer % sizeof(VCP_RxBuf);
        ReadBuf = VCP_RxBuf;
        ReadBufSize = sizeof(VCP_RxBuf);
        pReadPointer = &VCP_ReadIndex;
        pUART_Command = &VCP_Command;
    }
#endif
    else
    {
        return false;
    }

    if ((*pReadPointer) >= ReadBufSize)
        *pReadPointer = 0;

    // Limit iterations to prevent long loops when buffer is full of non-command data
    uint16_t maxIterations = ReadBufSize + 1;

    while (maxIterations--)
    {
        if ((*pReadPointer) == DmaLength)
            return false;

        // Find 0xAB with iteration limit
        uint16_t searchLimit = ReadBufSize;
        while ((*pReadPointer) != DmaLength && ReadBuf[*pReadPointer] != 0xABU && searchLimit--)
        {
#ifdef ENABLE_CAT
            uint8_t pIdx = (Port < NUM_CAT_PORTS) ? (uint8_t)Port : 0;
            uint8_t cat_byte = ReadBuf[*pReadPointer];
            if ((cat_byte >= 32 && cat_byte <= 126) || cat_byte == '\r' || cat_byte == '\n') {
                if (cat_byte == ';' || cat_byte == '\r' || cat_byte == '\n') {
                    if (cat_pos[pIdx] > 0) {
                        cat_buffer[pIdx][cat_pos[pIdx]] = 0;
                        Process_Kenwood_CAT(Port, cat_buffer[pIdx], cat_pos[pIdx]);
                        cat_pos[pIdx] = 0;
                    }
                } else {
                    if (cat_pos[pIdx] < sizeof(cat_buffer[pIdx]) - 1) {
                        cat_buffer[pIdx][cat_pos[pIdx]++] = (char)cat_byte;
                        cat_buffer[pIdx][cat_pos[pIdx]]   = 0;
                    }
                }
            } else {
                cat_pos[pIdx] = 0;
            }
#endif
            *pReadPointer = DMA_INDEX((*pReadPointer), 1, ReadBufSize);
        }

        if (searchLimit == 0)
        {
            // Too many bytes without finding 0xAB - sync to current position and exit
            *pReadPointer = DmaLength;
            return false;
        }

        if ((*pReadPointer) == DmaLength)
            return false;

        if ((*pReadPointer) < DmaLength)
            CommandLength = DmaLength - (*pReadPointer);
        else
            CommandLength = (DmaLength + ReadBufSize) - (*pReadPointer);

        if (CommandLength < 8)
            return 0;

        if (ReadBuf[DMA_INDEX(*pReadPointer, 1, ReadBufSize)] == 0xCD)
            break;

        *pReadPointer = DMA_INDEX(*pReadPointer, 1, ReadBufSize);
    }

    if (maxIterations == 0)
    {
        // Safety: too many outer loop iterations
        *pReadPointer = DmaLength;
        return false;
    }

    Index = DMA_INDEX(*pReadPointer, 2, ReadBufSize);
    Size  = (ReadBuf[DMA_INDEX(Index, 1, ReadBufSize)] << 8) | ReadBuf[Index];

    if ((Size + 8u) > ReadBufSize)
    {
        *pReadPointer = DmaLength;
        return false;
    }

    if (CommandLength < (Size + 8))
        return false;

    Index     = DMA_INDEX(Index, 2, ReadBufSize);
    TailIndex = DMA_INDEX(Index, Size + 2, ReadBufSize);

    if (ReadBuf[TailIndex] != 0xDC || ReadBuf[DMA_INDEX(TailIndex, 1, ReadBufSize)] != 0xBA)
    {
        *pReadPointer = DmaLength;
        return false;
    }

    if (TailIndex < Index)
    {
        const uint16_t ChunkSize = ReadBufSize - Index;
        memcpy(pUART_Command->Buffer, ReadBuf + Index, ChunkSize);
        memcpy(pUART_Command->Buffer + ChunkSize, ReadBuf, TailIndex);
    }
    else
        memcpy(pUART_Command->Buffer, ReadBuf + Index, TailIndex - Index);

    TailIndex = DMA_INDEX(TailIndex, 2, ReadBufSize);
    if (TailIndex < (*pReadPointer))
    {
        memset(ReadBuf + (*pReadPointer), 0, ReadBufSize - (*pReadPointer));
        memset(ReadBuf, 0, TailIndex);
    }
    else
        memset(ReadBuf + (*pReadPointer), 0, TailIndex - (*pReadPointer));

    *pReadPointer = TailIndex;

    /* --
    if (pUART_Command->Header.ID == 0x0514)
        bIsEncrypted = false;

    if (pUART_Command->Header.ID == 0x6902)
        bIsEncrypted = true;
    -- */

    if (bIsEncrypted)
    {
        unsigned int i;
        for (i = 0; i < (Size + 2u); i++)
            pUART_Command->Buffer[i] ^= Obfuscation[i % 16];
    }

    Crc = pUART_Command->Buffer[Size] | (pUART_Command->Buffer[Size + 1] << 8);

    return Size >= sizeof(Header_t) &&
           pUART_Command->Header.Size <= Size - sizeof(Header_t) &&
           CRC_Calculate(pUART_Command->Buffer, Size) == Crc;
}

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
/* Timestamp latched by the device-info handshake (0x0514) for this port. Slot
 * writes/erases require it to match, like the EEPROM write command (CMD_051D). */
static uint32_t mb_port_timestamp(uint32_t Port)
{
#if defined(ENABLE_UART)
    if (Port == UART_PORT_UART)
        return UART_Timestamp;
#endif
#if defined(ENABLE_USB)
    if (Port == UART_PORT_VCP)
        return VCP_Timestamp;
#endif
    (void)Port;
    return 0;
}
#endif

void UART_HandleCommand(uint32_t Port)
{
    UART_Command_t *pUART_Command;

    if (0) {}
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART)
    {
        pUART_Command = &UART_Command;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP)
    {
        pUART_Command = &VCP_Command;
    }
#endif
    else
    {
        return;
    }

    switch (pUART_Command->Header.ID)
    {
        case 0x0514:
            CMD_0514(Port, pUART_Command->Buffer);
            break;

        case 0x051B:
            CMD_051B(Port, pUART_Command->Buffer);
            break;

        case 0x051D:
            CMD_051D(Port, pUART_Command->Buffer);
            break;

        case 0x051F:    // Not implementing non-authentic command
            break;

        case 0x0521:    // Not implementing non-authentic command
            break;

#ifdef ENABLE_EXTRA_UART_CMD
        case 0x0527:
            CMD_0527(Port);
            break;

        case 0x0529:
            CMD_0529(Port);
            break;

        #ifndef ENABLE_FEAT_F4HWN
            case 0x052D:
                CMD_052D(Port, pUART_Command->Buffer);
                break;
        #endif

        case 0x052F:
            CMD_052F(Port, pUART_Command->Buffer);
            break;
#endif

        case 0x05DD: // reset
            #if defined(ENABLE_OVERLAY)
                overlay_FLASH_RebootToBootloader();
            #else
                NVIC_SystemReset();
            #endif
            break;

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
        // ---- M4 slot management ("Firmware Slots") ------------------------
        case 0x0720: // slot info: read the 64-byte header only (fast, no CRC)
        {
            if (pUART_Command->Header.Size < 1u) break;   // needs Data[0] (slot)
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            mb_slot_header_t hdr;
            memset(&hdr, 0, sizeof(hdr));
            uint8_t status = MB_SlotInfo(slot, &hdr);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t  Slot;
                uint8_t  Status;
                uint8_t  Hdr[sizeof(mb_slot_header_t)];
            } Reply;
            Reply.Header.ID   = 0x0721;
            Reply.Header.Size = 2 + sizeof(mb_slot_header_t);
            Reply.Slot        = slot;
            Reply.Status      = status;
            memcpy(Reply.Hdr, &hdr, sizeof(hdr));
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0722: // slot erase: wipe the whole 128 KiB slot region
        {
            if (pUART_Command->Header.Size < 6u) break;   // needs Data[0] slot + Data[2..5] timestamp
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t  slot = pUART_Command->Data[0];
            uint32_t ts   = (uint32_t)pUART_Command->Data[2]
                          | ((uint32_t)pUART_Command->Data[3] << 8)
                          | ((uint32_t)pUART_Command->Data[4] << 16)
                          | ((uint32_t)pUART_Command->Data[5] << 24);
            uint8_t status = (ts != mb_port_timestamp(Port))
                           ? MB_ERR_AUTH : MB_SlotErase(slot);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t  Slot;
                uint8_t  Status;
            } Reply;
            Reply.Header.ID   = 0x0723;
            Reply.Header.Size = 2;
            Reply.Slot        = slot;
            Reply.Status      = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0724: // slot write: program bytes at slot+offset (slot pre-erased)
        {
            if (pUART_Command->Header.Size < 12u)
                break;
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t  slot   = pUART_Command->Data[0];
            uint32_t offset = (uint32_t)pUART_Command->Data[2]
                            | ((uint32_t)pUART_Command->Data[3] << 8)
                            | ((uint32_t)pUART_Command->Data[4] << 16)
                            | ((uint32_t)pUART_Command->Data[5] << 24);
            uint16_t len    = (uint16_t)(pUART_Command->Data[6]
                            | ((uint16_t)pUART_Command->Data[7] << 8));
            uint32_t ts     = (uint32_t)pUART_Command->Data[8]
                            | ((uint32_t)pUART_Command->Data[9] << 8)
                            | ((uint32_t)pUART_Command->Data[10] << 16)
                            | ((uint32_t)pUART_Command->Data[11] << 24);
            uint8_t status;
            if (ts != mb_port_timestamp(Port))
                status = MB_ERR_AUTH;
            else if (len > pUART_Command->Header.Size - 12u)
                status = MB_ERR_SIZE;
            else
                status = MB_SlotWrite(slot, offset, &pUART_Command->Data[12], len);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t  Slot;
                uint8_t  Status;
            } Reply;
            Reply.Header.ID   = 0x0725;
            Reply.Header.Size = 2;
            Reply.Slot        = slot;
            Reply.Status      = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0726: // slot validate: full image CRC-32, no reflash
        {
            if (pUART_Command->Header.Size < 1u) break;   // needs Data[0] (slot)
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t  slot = pUART_Command->Data[0];
            uint32_t crc  = 0;
            uint8_t  status = MB_ValidateSlot(slot, NULL, &crc);
            struct __attribute__((packed)) {
                Header_t Header;
                uint32_t Crc32;   // offset 4: 4-byte aligned, no unaligned store
                uint8_t  Slot;
                uint8_t  Status;
            } Reply;
            Reply.Header.ID   = 0x0727;
            Reply.Header.Size = 6;
            Reply.Crc32       = crc;
            Reply.Slot        = slot;
            Reply.Status      = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0728: // config reset: wipe the 64 KiB of a config bank (1..4)
        {
            if (pUART_Command->Header.Size < 6u) break;   // needs Data[0] bank + Data[2..5] timestamp
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t  bank = pUART_Command->Data[0];
            uint32_t ts   = (uint32_t)pUART_Command->Data[2]
                          | ((uint32_t)pUART_Command->Data[3] << 8)
                          | ((uint32_t)pUART_Command->Data[4] << 16)
                          | ((uint32_t)pUART_Command->Data[5] << 24);
            uint8_t status = (ts != mb_port_timestamp(Port))
                           ? MB_ERR_AUTH : MB_BankErase(bank);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t  Bank;   // echoes the erased bank (same wire layout as slot replies)
                uint8_t  Status;
            } Reply;
            Reply.Header.ID   = 0x0729;
            Reply.Header.Size = 2;
            Reply.Bank        = bank;
            Reply.Status      = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }
#endif

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
        // ---- overlay-app slot management ("Apps") -------------------------
        // Parallels the firmware-slot family (0x072x); targets the external-flash
        // Apps region. External flash only, never brick-critical.
        case 0x0730: // app slot info: read the 64-byte header only
        {
            if (pUART_Command->Header.Size < 1u) break;   // needs Data[0] (slot)
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            app_header_t hdr;
            memset(&hdr, 0, sizeof(hdr));
            uint8_t status = APP_SlotInfo(slot, &hdr);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t  Slot;
                uint8_t  Status;
                uint8_t  Hdr[sizeof(app_header_t)];
            } Reply;
            Reply.Header.ID   = 0x0731;
            Reply.Header.Size = 2 + sizeof(app_header_t);
            Reply.Slot        = slot;
            Reply.Status      = status;
            memcpy(Reply.Hdr, &hdr, sizeof(hdr));
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0732: // app slot erase: wipe the whole 8 KiB slot region
        {
            if (pUART_Command->Header.Size < 6u) break;   // needs Data[0] slot + Data[2..5] timestamp
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t  slot = pUART_Command->Data[0];
            uint32_t ts   = (uint32_t)pUART_Command->Data[2]
                          | ((uint32_t)pUART_Command->Data[3] << 8)
                          | ((uint32_t)pUART_Command->Data[4] << 16)
                          | ((uint32_t)pUART_Command->Data[5] << 24);
            uint8_t status = (ts != mb_port_timestamp(Port))
                           ? APP_ERR_AUTH : APP_SlotErase(slot);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t  Slot;
                uint8_t  Status;
            } Reply;
            Reply.Header.ID   = 0x0733;
            Reply.Header.Size = 2;
            Reply.Slot        = slot;
            Reply.Status      = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0734: // app slot write: program bytes at slot+offset (pre-erased)
        {
            if (pUART_Command->Header.Size < 12u)
                break;
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t  slot   = pUART_Command->Data[0];
            uint32_t offset = (uint32_t)pUART_Command->Data[2]
                            | ((uint32_t)pUART_Command->Data[3] << 8)
                            | ((uint32_t)pUART_Command->Data[4] << 16)
                            | ((uint32_t)pUART_Command->Data[5] << 24);
            uint16_t len    = (uint16_t)(pUART_Command->Data[6]
                            | ((uint16_t)pUART_Command->Data[7] << 8));
            uint32_t ts     = (uint32_t)pUART_Command->Data[8]
                            | ((uint32_t)pUART_Command->Data[9] << 8)
                            | ((uint32_t)pUART_Command->Data[10] << 16)
                            | ((uint32_t)pUART_Command->Data[11] << 24);
            uint8_t status;
            if (ts != mb_port_timestamp(Port))
                status = APP_ERR_AUTH;
            else if (len > pUART_Command->Header.Size - 12u)
                status = APP_ERR_SIZE;
            else
                status = APP_SlotWrite(slot, offset, &pUART_Command->Data[12], len);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t  Slot;
                uint8_t  Status;
            } Reply;
            Reply.Header.ID   = 0x0735;
            Reply.Header.Size = 2;
            Reply.Slot        = slot;
            Reply.Status      = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0736: // app slot validate: header only (code CRC is checked at launch)
        {
            if (pUART_Command->Header.Size < 1u) break;   // needs Data[0] (slot)
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t  slot = pUART_Command->Data[0];
            uint8_t  status = APP_ValidateSlot(slot, NULL);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t  Slot;
                uint8_t  Status;
            } Reply;
            Reply.Header.ID   = 0x0737;
            Reply.Header.Size = 2;
            Reply.Slot        = slot;
            Reply.Status      = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }
#endif

#ifdef ENABLE_UART_RW_BK_REGS
        case 0x0601:
            CMD_0601_ReadBK4819Reg(Port, pUART_Command->Buffer);
            break;
        
        case 0x0602:
            CMD_0602_WriteBK4819Reg(pUART_Command->Buffer);
            break;
#endif
    } // switch

    #ifdef ENABLE_FEAT_F4HWN_K5VIEWER
        gUART_LockK5Viewer = 20; // lock the K5Viewer stream
    #endif
}

void UART_ServiceCommands(void)
{
#ifdef ENABLE_USB
    if (UART_IsCommandAvailable(UART_PORT_VCP))
        UART_HandleCommand(UART_PORT_VCP);
#endif

#ifdef ENABLE_UART
    if (UART_IsCommandAvailable(UART_PORT_UART))
        UART_HandleCommand(UART_PORT_UART);
#endif
}
