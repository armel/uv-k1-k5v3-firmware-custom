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
    #include "frequencies.h"
    #include "radio.h"
    #include "ui/ui.h"
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

// --- Zmienne S-Metra / auto-raportowanie RSSI ---
static bool    g_AutoReportRSSI     = false;
static int16_t g_LastReportedRSSI   = 0;
static uint16_t g_UartRssiTimer_10ms = 0;

// --- Zmienne skanera SC (lista) ---
#define MAX_UART_SCAN_LIST 25
static uint32_t g_UartScanList[MAX_UART_SCAN_LIST];
static uint8_t  g_UartScanCount        = 0;
static uint8_t  g_UartScanIndex        = 0;
static uint8_t  g_UartScanDelay_10ms   = 0;
static char     g_UartScanResponse[256];
static bool     g_UartScanActive       = false;
static uint32_t g_UartScanOriginalFreq = 0;
static uint8_t  g_UartScanOriginalBand = 0;

// --- Zmienne skanera SCF (pojedynczy kanał) ---
static uint8_t  g_SingleScanState        = 0;
static uint8_t  g_SingleScanDelay_10ms   = 0;
static uint32_t g_SingleScanTargetFreq   = 0;
static uint32_t g_SingleScanOriginalFreq = 0;
static uint8_t  g_SingleScanOriginalBand = 0;
static uint32_t g_SingleScanPort         = 0;

// --- Bufor ASCII CAT ---
static char    cat_buffer[64];
static uint8_t cat_pos = 0;

// --- Forward declarations ---
static void UART_HardwareScanner_Periodic(void);
static void UART_SingleScan_Periodic(void);
static void Process_Kenwood_CAT(uint32_t Port);

// ---------------------------------------------------------------------------
// UART_SendText — wysyłanie tekstu ASCII (asynchronicznie, bez blokowania)
// ---------------------------------------------------------------------------
void UART_SendText(uint32_t Port, const char *str)
{
#if defined(ENABLE_USB)
    if (Port == UART_PORT_VCP) {
        VCP_SendAsync((uint8_t *)str, strlen(str));
        return;
    }
#endif
#if defined(ENABLE_UART)
    if (Port == UART_PORT_UART) {
        UART_Send((const uint8_t *)str, strlen(str));
    }
#endif
}

// ---------------------------------------------------------------------------
// Konwertery liczba ↔ tekst (bez biblioteki stdio do wypisywania)
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
// Helperów UI/radia
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
    if (bIsGlobal) {
        SETTINGS_SaveSettings();
        RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE);
    } else {
        SETTINGS_SaveChannel(gEeprom.VfoInfo[gEeprom.TX_VFO].CHANNEL_SAVE,
                             gEeprom.TX_VFO,
                             &gEeprom.VfoInfo[gEeprom.TX_VFO], 1);
        RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE_RELOAD);
    }

    if (gRxIdleMode) {
        BK4819_RX_TurnOn();
        gRxIdleMode = false;
    }

    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);
    ForceScreenUpdate();
}

static void Apply_Tone_To_Active_VFO(uint8_t code_type, uint8_t code_index)
{
    gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->CodeType = code_type;
    gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->Code     = code_index;
    gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->CodeType = code_type;
    gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->Code     = code_index;

    if (gTxVfo) {
        gTxVfo->pTX->CodeType = code_type;
        gTxVfo->pTX->Code     = code_index;
        gTxVfo->pRX->CodeType = code_type;
        gTxVfo->pRX->Code     = code_index;
    }
    CAT_ApplyAndSave(false);
}

// ---------------------------------------------------------------------------
// UART_GetRSSI_dBm — odczyt RSSI z BK4819 w dBm
// ---------------------------------------------------------------------------
static int16_t UART_GetRSSI_dBm(void)
{
    if (gRxIdleMode)
        return -160;
    uint16_t rssi_raw = BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
    return (rssi_raw / 2) - 160;
}

// ---------------------------------------------------------------------------
// UART_HardwareScanner_Periodic — maszyna stanów SC (lista kanałów)
// Wywoływana co 10ms z APP_TimeSlice10ms
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
                gUpdateDisplay = true;

                strcat(g_UartScanResponse, ";");
#if defined(ENABLE_UART)
                UART_SendText(UART_PORT_UART, g_UartScanResponse);
#endif
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

    g_UartScanDelay_10ms = 9;   // ~90ms na kanał
}

// ---------------------------------------------------------------------------
// UART_SingleScan_Periodic — maszyna stanów SCF (pojedynczy pomiar)
// Wywoływana co 10ms z APP_TimeSlice10ms
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
                gUpdateDisplay = true;

                g_SingleScanState = 0;
                UART_SendText(g_SingleScanPort, resp);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// UART_ReportRSSI_Periodic — główna pętla 10ms: S-metr + dispatch skanerów
// ---------------------------------------------------------------------------
void UART_ReportRSSI_Periodic(void)
{
    // Dispatcher maszyn stanów (zawsze aktywny, nawet przy wyłączonym auto-raporcie)
    UART_HardwareScanner_Periodic();
    UART_SingleScan_Periodic();

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
        g_LastReportedRSSI    = current_dbm;
        g_LastReportedVFO     = active_vfo;
        g_UartRssiTimer_10ms  = 0;
    }
}

// ---------------------------------------------------------------------------
// UART_ReportDTMF — asynchroniczne raportowanie odebranego tonu DTMF
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
// Process_Kenwood_CAT — parser ASCII komend Kenwood CAT
// ---------------------------------------------------------------------------
static void Process_Kenwood_CAT(uint32_t Port)
{
    if (gRxIdleMode) {
        BK4819_RX_TurnOn();
        gRxIdleMode = false;
        SYSTEM_DelayMs(20);
    }

    // FA/FB — częstotliwość VFO A lub B
    bool is_fa = (strncmp(cat_buffer, "FA", 2) == 0);
    bool is_fb = (strncmp(cat_buffer, "FB", 2) == 0);
    if (is_fa || is_fb) {
        int vfo_idx = is_fa ? 0 : 1;
        if (cat_buffer[2] == ';') {
            uint32_t freqHz = gEeprom.VfoInfo[vfo_idx].pRX->Frequency * 10;
            char resp[16];
            resp[0] = 'F'; resp[1] = is_fa ? 'A' : 'B';
            UIntToText(resp, freqHz, 11, 2);
            resp[13] = ';'; resp[14] = 0;
            UART_SendText(Port, resp);
        } else if (cat_pos >= 13) {
            uint32_t freqHz  = TextToUInt(cat_buffer, 2, 11);
            uint32_t newFreq = freqHz / 10;
            if (gEeprom.VfoInfo[vfo_idx].pRX->Frequency != newFreq) {
                gEeprom.VfoInfo[vfo_idx].pRX->Frequency = newFreq;
                gEeprom.VfoInfo[vfo_idx].pTX->Frequency = newFreq;
                gEeprom.VfoInfo[vfo_idx].Band = FREQUENCY_GetBand(newFreq);
                if (gEeprom.TX_VFO == vfo_idx)
                    CAT_ApplyAndSave(false);
                else
                    SETTINGS_SaveChannel(gEeprom.VfoInfo[vfo_idx].CHANNEL_SAVE, vfo_idx,
                                         &gEeprom.VfoInfo[vfo_idx], 1);
            }
        }
        return;
    }

    // FR — przełączenie aktywnego VFO
    if (strncmp(cat_buffer, "FR", 2) == 0 && cat_buffer[3] == ';') {
        char    vfo_char   = cat_buffer[2];
        uint8_t target_vfo = (vfo_char == '1') ? 1 : 0;
        if (gEeprom.TX_VFO != target_vfo) {
            gEeprom.TX_VFO = target_vfo;
            gEeprom.RX_VFO = target_vfo;
            CAT_ApplyAndSave(true);
        }
        return;
    }

    // TX — wymuszenie nadawania
    if (strncmp(cat_buffer, "TX", 2) == 0) {
        if (gCurrentFunction != FUNCTION_TRANSMIT) {
            FUNCTION_Select(FUNCTION_TRANSMIT);
            ForceScreenUpdate();
        }
        return;
    }

    // RX — powrót do odbioru
    if (strncmp(cat_buffer, "RX", 2) == 0) {
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            FUNCTION_Select(FUNCTION_FOREGROUND);
            ForceScreenUpdate();
        }
        return;
    }

    // MO — monitor (otwórz squelch)
    if (strncmp(cat_buffer, "MO", 2) == 0 && cat_buffer[3] == ';') {
        char m = cat_buffer[2];
        if (m == '1') {
            if (gCurrentFunction != FUNCTION_MONITOR && gCurrentFunction != FUNCTION_TRANSMIT)
                ACTION_Monitor();
        } else if (m == '0') {
            if (gCurrentFunction == FUNCTION_MONITOR)
                ACTION_Monitor();
        }
        return;
    }

    // MD — modulacja (4=FM, 5=AM, 2=USB)
    if (strncmp(cat_buffer, "MD", 2) == 0) {
        if (cat_buffer[2] == ';') {
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

    // PC — moc nadajnika (0-7)
    if (strncmp(cat_buffer, "PC", 2) == 0 && cat_buffer[2] != ';') {
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
        return;
    }

    // OF — wyłącz subton
    if (strncmp(cat_buffer, "OF;", 3) == 0) {
        Apply_Tone_To_Active_VFO(0, 0);
        return;
    }

    // CT — CTCSS (4 cyfry, np. CT08850;)
    if (strncmp(cat_buffer, "CT", 2) == 0 && cat_pos >= 7) {
        uint32_t ct_freq = TextToUInt(cat_buffer, 2, 4);
        for (uint8_t i = 0; i < 50; i++) {
            if (CTCSS_Options[i] == ct_freq) {
                Apply_Tone_To_Active_VFO(1, i);
                return;
            }
        }
        return;
    }

    // DT — DCS (3 cyfry ósemkowe, np. DT023;)
    if (strncmp(cat_buffer, "DT", 2) == 0 && cat_pos >= 6) {
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

    // SQ — poziom squelch (0-9)
    if (strncmp(cat_buffer, "SQ", 2) == 0 && cat_buffer[3] == ';') {
        char s = cat_buffer[2];
        if (s >= '0' && s <= '9') {
            uint8_t new_sq = s - '0';
            if (gEeprom.SQUELCH_LEVEL != new_sq) {
                gEeprom.SQUELCH_LEVEL = new_sq;
                CAT_ApplyAndSave(true);
            }
        }
        return;
    }

    // OS — kierunek offsetu (0=off, 1=+, 2=-)
    if (strncmp(cat_buffer, "OS", 2) == 0 && cat_buffer[3] == ';') {
        char    d   = cat_buffer[2];
        uint8_t dir = 0;
        if (d == '1') dir = 1;
        if (d == '2') dir = 2;
        if (gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY_DIRECTION != dir) {
            gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY_DIRECTION = dir;
            if (gTxVfo) gTxVfo->TX_OFFSET_FREQUENCY_DIRECTION = dir;
            CAT_ApplyAndSave(false);
        }
        return;
    }

    // OV — wartość offsetu (11 cyfr Hz)
    if (strncmp(cat_buffer, "OV", 2) == 0 && cat_pos >= 13) {
        uint32_t offsetHz = TextToUInt(cat_buffer, 2, 11);
        if (gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY != offsetHz / 10) {
            gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY = offsetHz / 10;
            if (gTxVfo) gTxVfo->TX_OFFSET_FREQUENCY = offsetHz / 10;
            CAT_ApplyAndSave(false);
        }
        return;
    }

    // IF — informacja o bieżącym VFO (status transceivera)
    if (strncmp(cat_buffer, "IF;", 3) == 0) {
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

    // SM — szybki pomiar sygnału na wskazanej częstotliwości
    if (strncmp(cat_buffer, "SM", 2) == 0 && cat_pos >= 13) {
        if (gCurrentFunction == FUNCTION_TRANSMIT) {
            UART_SendText(Port, "SM,busy;");
            return;
        }
        uint32_t freqHz      = TextToUInt(cat_buffer, 2, 11);
        uint32_t target_freq = freqHz / 10;
        uint32_t current_freq = gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->Frequency;
        uint8_t  current_band = gEeprom.VfoInfo[gEeprom.TX_VFO].Band;

        if (current_freq != target_freq) {
            BK4819_SetFrequency(target_freq);
            BK4819_RX_TurnOn();
            SYSTEM_DelayMs(60);
        } else {
            if (gRxIdleMode) {
                BK4819_RX_TurnOn();
                gRxIdleMode = false;
                SYSTEM_DelayMs(10);
            }
        }

        int16_t dbm = BK4819_GetRSSI_dBm() + dBmCorrTable[current_band];
        uint8_t sq  = g_SquelchLost ? 1 : 0;

        if (current_freq != target_freq) {
            BK4819_SetFrequency(current_freq);
            BK4819_RX_TurnOn();
        }

        char resp[32];
        sprintf(resp, "SM%011u,%+04d,%d;", freqHz, dbm, sq);
        UART_SendText(Port, resp);
        return;
    }

    // S1 — odczyt RSSI bieżącego VFO
    if (strncmp(cat_buffer, "S1;", 3) == 0) {
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

    // SL — załaduj jeden kanał do listy skanowania (SL<idx2><freq11>;)
    if (strncmp(cat_buffer, "SL", 2) == 0 && cat_pos >= 16) {
        uint8_t  idx    = TextToUInt(cat_buffer, 2, 2);
        uint32_t freqHz = TextToUInt(cat_buffer, 5, 11);
        if (idx < MAX_UART_SCAN_LIST)
            g_UartScanList[idx] = freqHz / 10;
        UART_SendText(Port, "SL_OK;");
        return;
    }

    // SCF — szybki pomiar sprzętowy na żądanie (asynchroniczny)
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
            uint8_t ticks_len = cat_pos - 16;
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

    // SC — start ultraszybkiego skanowania grupowego (SC<count2>;)
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

        strcpy(g_UartScanResponse, "SR");
        g_UartScanIndex      = 0;
        g_UartScanDelay_10ms = 0;
        g_UartScanActive     = true;
        return;
    }

    // RD — włącz/wyłącz auto-raportowanie RSSI
    if (strncmp(cat_buffer, "RD", 2) == 0 && cat_buffer[3] == ';') {
        char d = cat_buffer[2];
        if (d == '1')      g_AutoReportRSSI = true;
        else if (d == '0') g_AutoReportRSSI = false;
        return;
    }
}

#endif // ENABLE_CAT
// ============================================================
// === Koniec bloku CAT
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
        DmaLength = sizeof(UART_DMA_Buffer) - LL_DMA_GetDataLength(DMA1, DMA_CHANNEL);
        ReadBuf = UART_DMA_Buffer;
        ReadBufSize = sizeof(UART_DMA_Buffer);
        pReadPointer = &gUART_WriteIndex;
        pUART_Command = &UART_Command;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP)
    {
        DmaLength = VCP_RxBufPointer;
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
            uint8_t cat_byte = ReadBuf[*pReadPointer];
            if ((cat_byte >= 32 && cat_byte <= 126) || cat_byte == '\r' || cat_byte == '\n') {
                if (cat_pos < sizeof(cat_buffer) - 1) {
                    if (cat_byte != '\r' && cat_byte != '\n') {
                        cat_buffer[cat_pos++] = cat_byte;
                        cat_buffer[cat_pos]   = 0;
                    }
                }
                if (cat_byte == ';') {
                    Process_Kenwood_CAT(Port);
                    cat_pos = 0;
                }
            } else {
                cat_pos = 0;
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
