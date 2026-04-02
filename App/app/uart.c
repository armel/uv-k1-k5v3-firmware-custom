/* Copyright 2025 muzkr https://github.com/muzkr
 * Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// FORCE CAT COMPILATION
#define ENABLE_CAT 1

#include <string.h>

#if !defined(ENABLE_OVERLAY)
    #include "py32f0xx.h"
#endif
#ifdef ENABLE_FMRADIO
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
    #include "dcs.h"        // Access to CTCSS/DCS tables
    #include "app/action.h" // Access to ACTION_Monitor()
    #include "frequencies.h"
    #include "radio.h"
    #include "ui/ui.h" 
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

#ifdef ENABLE_CAT
// --- LINKER ERROR FIX ---
void *_sbrk(int incr) { return (void *)-1; }
#endif

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


#ifdef ENABLE_CAT

// --- CAT PROTOCOL VARIABLES ---
static char cat_buffer[64];
static uint8_t cat_pos = 0;

// --- CAT HELPER FUNCTIONS ---

void UART_SendText(uint32_t Port, const char *str) {
#if defined(ENABLE_USB)
    if (Port == UART_PORT_VCP)
    {
        VCP_SendAsync((uint8_t *)str, strlen(str));
        return;
    }
#endif
#if defined(ENABLE_UART)
    if (Port == UART_PORT_UART)
    {
        UART_Send((const uint8_t *)str, strlen(str));
    }
#endif
}

void UIntToText(char* buffer, uint32_t value, int digits, int offset) {
    for (int i = offset + digits - 1; i >= offset; i--) {
        buffer[i] = (value % 10) + '0';
        value /= 10;
    }
}

uint32_t TextToUInt(const char* buffer, int start, int len) {
    uint32_t val = 0;
    for (int i = start; i < start + len; i++) {
        if (buffer[i] >= '0' && buffer[i] <= '9') {
            val = (val * 10) + (buffer[i] - '0');
        }
    }
    return val;
}

uint16_t ParseDCSCode(const char* buffer, int start) {
    uint16_t val = 0;
    for (int i = 0; i < 3; i++) {
        char c = buffer[start + i];
        if (c < '0' || c > '7') return 0xFFFF;
        val = (val * 8) + (c - '0');
    }
    return val;
}

void ForceScreenUpdate(void) {
    gUpdateStatus = true;
    gUpdateDisplay = true;
    #ifdef ENABLE_FEAT_F4HWN
        gRequestDisplayScreen = DISPLAY_MAIN;
    #endif
}

// Bypasses the keypad ProcessKey() loop to apply and permanently save settings immediately
void CAT_ApplyAndSave(bool bIsGlobal) {
    if (bIsGlobal) {
        // Global settings like Squelch
        SETTINGS_SaveSettings();
        RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE);
    } else {
        // VFO specific settings like Offset, Tone, Power
        SETTINGS_SaveChannel(gEeprom.VfoInfo[gEeprom.TX_VFO].CHANNEL_SAVE, gEeprom.TX_VFO, &gEeprom.VfoInfo[gEeprom.TX_VFO], 1);
        RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE_RELOAD);
    }
    
    if (gRxIdleMode) {
        BK4819_RX_TurnOn();
        gRxIdleMode = false;
    }

    // Apply changes to hardware immediately
    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);
    ForceScreenUpdate();
}

void Apply_Tone_To_Active_VFO(uint8_t code_type, uint8_t code_index) {
    gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->CodeType = code_type;
    gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->Code = code_index;
    gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->CodeType = code_type;
    gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->Code = code_index;

    if (gTxVfo) {
        gTxVfo->pTX->CodeType = code_type;
        gTxVfo->pTX->Code = code_index;
        gTxVfo->pRX->CodeType = code_type;
        gTxVfo->pRX->Code = code_index;
    }
    CAT_ApplyAndSave(false);
}

// --- KENWOOD CAT COMMAND PARSER ---
static void Process_Kenwood_CAT(uint32_t Port) {
    
    // 1. ID (Identify)
    if (strncmp(cat_buffer, "ID;", 3) == 0) {
        UART_SendText(Port, "ID020;");
        return;
    }

    // 2. FA/FB (VFO Frequency Control)
    bool is_fa = (strncmp(cat_buffer, "FA", 2) == 0);
    bool is_fb = (strncmp(cat_buffer, "FB", 2) == 0);
    if (is_fa || is_fb) {
        int vfo_idx = is_fa ? 0 : 1; 
        if (cat_buffer[2] == ';') { // GET Frequency
            uint32_t freqHz = gEeprom.VfoInfo[vfo_idx].pRX->Frequency * 10;
            char resp[16];
            resp[0] = 'F'; resp[1] = is_fa ? 'A' : 'B';
            UIntToText(resp, freqHz, 11, 2);
            resp[13] = ';'; resp[14] = 0;
            UART_SendText(Port, resp);
        } else if (cat_pos >= 13) { // SET Frequency
            uint32_t freqHz = TextToUInt(cat_buffer, 2, 11);
            if (gEeprom.VfoInfo[vfo_idx].pRX->Frequency != freqHz / 10) {
                gEeprom.VfoInfo[vfo_idx].pRX->Frequency = freqHz / 10;
                gEeprom.VfoInfo[vfo_idx].pTX->Frequency = freqHz / 10;
                if (gEeprom.TX_VFO == vfo_idx) {
                    CAT_ApplyAndSave(false);
                } else {
                    SETTINGS_SaveChannel(gEeprom.VfoInfo[vfo_idx].CHANNEL_SAVE, vfo_idx, &gEeprom.VfoInfo[vfo_idx], 1);
                }
            }
        }
        return;
    }

    // 3. FR (Switch Active VFO)
    if (strncmp(cat_buffer, "FR", 2) == 0 && cat_buffer[3] == ';') {
        char vfo_char = cat_buffer[2];
        uint8_t target_vfo = (vfo_char == '1') ? 1 : 0;
        if (gEeprom.TX_VFO != target_vfo) {
            gEeprom.TX_VFO = target_vfo;
            gEeprom.RX_VFO = target_vfo;
            CAT_ApplyAndSave(true);
        }
        return;
    }

    // 4. TX / RX (PTT Control)
    if (strncmp(cat_buffer, "TX", 2) == 0) {
        if (gCurrentFunction != FUNCTION_TRANSMIT) { 
            FUNCTION_Select(FUNCTION_TRANSMIT); 
            ForceScreenUpdate(); 
        }
        return;
    }
    if (strncmp(cat_buffer, "RX", 2) == 0) {
        if (gCurrentFunction == FUNCTION_TRANSMIT) { 
            FUNCTION_Select(FUNCTION_FOREGROUND); 
            ForceScreenUpdate(); 
        }
        return;
    }

    // 5. MO (Hardware Monitor ON/OFF)
    if (strncmp(cat_buffer, "MO", 2) == 0 && cat_buffer[3] == ';') {
        char m = cat_buffer[2];
        if (m == '1') { // Monitor ON
            if (gCurrentFunction != FUNCTION_MONITOR && gCurrentFunction != FUNCTION_TRANSMIT) {
                ACTION_Monitor(); // Fully unmutes hardware audio amp
            }
        } else if (m == '0') { // Monitor OFF
            if (gCurrentFunction == FUNCTION_MONITOR) {
                ACTION_Monitor(); // Toggles monitor off
            }
        }
        return;
    }

    // 6. MD (Modulation Mode)
    if (strncmp(cat_buffer, "MD", 2) == 0) {
        if (cat_buffer[2] == ';') { // GET Mode
            uint8_t mod = gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation; 
            char resp[6] = "MD4;"; 
            if (mod == 1) resp[2] = '5'; // AM
            else if (mod == 2) resp[2] = '2'; // USB
            UART_SendText(Port, resp);
        } else { // SET Mode
            char m = cat_buffer[2];
            uint8_t target_mod = 0; 
            if (m == '5') target_mod = 1; // AM 
            else if (m == '2') target_mod = 2; // USB
            
            if (gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation != target_mod) {
                gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation = target_mod;
                if (gTxVfo) gTxVfo->Modulation = target_mod;
                CAT_ApplyAndSave(false);
            }
        }
        return;
    }

    // 7. PC (Power Level)
    if (strncmp(cat_buffer, "PC", 2) == 0 && cat_buffer[2] != ';') {
         char p = cat_buffer[2];
         if (p >= '0' && p <= '7') {
             uint8_t cat_level = p - '0';
             uint8_t radio_level = OUTPUT_POWER_HIGH;
             
             // Używamy natywnego enuma z settings.h
             switch(cat_level) {
                 case 0: radio_level = OUTPUT_POWER_LOW1; break; // Najsłabsza
                 case 1: radio_level = OUTPUT_POWER_LOW2; break; 
                 case 2: radio_level = OUTPUT_POWER_LOW3; break; 
                 case 3: radio_level = OUTPUT_POWER_LOW4; break; 
                 case 4: radio_level = OUTPUT_POWER_LOW5; break; 
                 case 5: radio_level = OUTPUT_POWER_MID;  break; 
                 case 6: radio_level = OUTPUT_POWER_HIGH; break; // Najmocniejsza
                 case 7: radio_level = OUTPUT_POWER_USER; break; // Własna (USER - 0)
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

    // 8. OF (Turn OFF CTCSS/DCS)
    if (strncmp(cat_buffer, "OF;", 3) == 0) {
        Apply_Tone_To_Active_VFO(0, 0); 
        return;
    }

    // 9. CT (Set CTCSS Tone)
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

    // 10. DT (Set DCS Code)
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

    // 11. SQ (Set Squelch Threshold)
    if (strncmp(cat_buffer, "SQ", 2) == 0 && cat_buffer[3] == ';') {
        char s = cat_buffer[2];
        if (s >= '0' && s <= '9') {
            uint8_t new_sq = s - '0';
            if (gEeprom.SQUELCH_LEVEL != new_sq) {
                gEeprom.SQUELCH_LEVEL = new_sq;
                CAT_ApplyAndSave(true); // Save to EEPROM and force hardware recalculation
            }
        }
        return;
    }

    // 12. OS (Offset Shift Direction)
    if (strncmp(cat_buffer, "OS", 2) == 0 && cat_buffer[3] == ';') {
        char d = cat_buffer[2];
        uint8_t dir = 0; 
        if (d == '1') dir = 1; // Add (+)
        if (d == '2') dir = 2; // Sub (-)
        
        if (gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY_DIRECTION != dir) {
            gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY_DIRECTION = dir;
            if (gTxVfo) gTxVfo->TX_OFFSET_FREQUENCY_DIRECTION = dir;
            CAT_ApplyAndSave(false);
        }
        return;
    }

    // 13. OV (Offset Value)
    if (strncmp(cat_buffer, "OV", 2) == 0 && cat_pos >= 13) {
        uint32_t offsetHz = TextToUInt(cat_buffer, 2, 11);
        if (gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY != offsetHz / 10) {
            gEeprom.VfoInfo[gEeprom.TX_VFO].TX_OFFSET_FREQUENCY = offsetHz / 10;
            if (gTxVfo) gTxVfo->TX_OFFSET_FREQUENCY = offsetHz / 10;
            CAT_ApplyAndSave(false);
        }
        return;
    }

    // 14. IF (Global Status Request)
    if (strncmp(cat_buffer, "IF;", 3) == 0) {
        char resp[40]; memset(resp, ' ', 38);
        resp[0] = 'I'; resp[1] = 'F';
        
        uint32_t freqHz = 0;
        if(gTxVfo) freqHz = gTxVfo->pRX->Frequency * 10;
        else freqHz = gEeprom.VfoInfo[gEeprom.TX_VFO].pRX->Frequency * 10;

        UIntToText(resp, freqHz, 11, 2);
        resp[13]='0'; resp[14]='0'; resp[15]='0'; resp[16]='0'; resp[17]='0';
        
        uint8_t mod = gEeprom.VfoInfo[gEeprom.TX_VFO].Modulation;
        resp[29] = (mod == 1) ? '5' : '4'; 
        bool is_tx = (gCurrentFunction == FUNCTION_TRANSMIT);
        resp[30] = is_tx ? '1' : '0';
        resp[31] = ';'; resp[32] = 0;
        UART_SendText(Port, resp);
        return;
    }
}
#endif // ENABLE_CAT


#ifdef ENABLE_USB
static void SendReply_VCP(void *pReply, uint16_t Size)
{
    static uint8_t VCP_ReplyBuf[MAX_REPLY_SIZE + sizeof(Header_t) + sizeof(Footer_t)];

    // !!
    if (Size > MAX_REPLY_SIZE)
    {
        return;
    }

    memcpy(VCP_ReplyBuf + sizeof(Header_t), pReply, Size);

    Header_t *pHeader = (Header_t *)VCP_ReplyBuf;
    Footer_t *pFooter = (Footer_t *)(VCP_ReplyBuf + sizeof(Header_t) + Size);
    pReply = VCP_ReplyBuf + sizeof(Header_t);

    if (bIsEncrypted)
    {
        uint8_t     *pBytes = (uint8_t *)pReply;
        unsigned int i;
        for (i = 0; i < Size; i++)
            pBytes[i] ^= Obfuscation[i % 16];
    }

    pHeader->ID = 0xCDAB;
    pHeader->Size = Size;

    // VCP_Send((uint8_t *)&Header, sizeof(Header));
    // VCP_Send(pReply, Size);
   
    if (bIsEncrypted)
    {
        pFooter->Padding[0] = Obfuscation[(Size + 0) % 16] ^ 0xFF;
        pFooter->Padding[1] = Obfuscation[(Size + 1) % 16] ^ 0xFF;
    }
    else
    {
        pFooter->Padding[0] = 0xFF;
        pFooter->Padding[1] = 0xFF;
    }
    pFooter->ID = 0xBADC;

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
}

static void SendVersion(uint32_t Port)
{
    REPLY_0514_t Reply;

    Reply.Header.ID = 0x0515;
    Reply.Header.Size = sizeof(Reply.Data);
    strcpy(Reply.Data.Version, Version);
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

#ifdef ENABLE_FMRADIO
    gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
#endif

    gSerialConfigCountDown_500ms = 12; // 6 sec
    
    // turn the LCD backlight off
    BACKLIGHT_TurnOff();

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

    #ifdef ENABLE_FMRADIO
        gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
    #endif

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

    #ifdef ENABLE_FMRADIO
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
                EEPROM_WriteBuffer(Offset, &pCmd->Data[i * 8U]);
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

    #ifdef ENABLE_FMRADIO
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

    // turn the LCD backlight off
    BACKLIGHT_TurnOff();

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
            uint8_t byte = ReadBuf[*pReadPointer];
            // Intercept CAT ASCII Commands
            if ((byte >= 32 && byte <= 126) || byte == '\r' || byte == '\n') {
                if (cat_pos < sizeof(cat_buffer) - 1) {
                    if (byte != '\r' && byte != '\n') {
                        cat_buffer[cat_pos++] = byte;
                        cat_buffer[cat_pos] = 0;
                    }
                }
                if (byte == ';') { 
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

    return CRC_Calculate(pUART_Command->Buffer, Size) == Crc;
}

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

#ifdef ENABLE_UART_RW_BK_REGS
        case 0x0601:
            CMD_0601_ReadBK4819Reg(Port, pUART_Command->Buffer);
            break;
        
        case 0x0602:
            CMD_0602_WriteBK4819Reg(pUART_Command->Buffer);
            break;
#endif
    } // switch

    #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
        gUART_LockScreenshot = 20; // lock screenshot
    #endif
}
