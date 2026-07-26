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

#ifdef ENABLE_DOCK
#include "app/dock.h"
#include "driver/system.h"
#include "radio.h"
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

    if (gEeprom.BACKLIGHT_TIME < 61) // backlight is set to be always on
        BACKLIGHT_TurnOff();         // turn the LCD backlight off

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

    if (gEeprom.BACKLIGHT_TIME < 61) // backlight is set to be always on
        BACKLIGHT_TurnOff();         // turn the LCD backlight off

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

#ifdef ENABLE_DOCK
// radio-server "dock control mode": wire the pure app/dock.c protocol core to
// the real BK4819 registers and UART. The port surface is deliberately tiny —
// 0x0850/0x0851 register R/W and the 0x0870/0x0871 full-control loop — because
// radio-server drives everything through BK4819 registers.
static uint16_t Dock_HalRead(void *user, uint16_t reg)
{
    UNUSED(user);
    return BK4819_ReadRegister((BK4819_REGISTER_t)reg);
}
static void Dock_HalWrite(void *user, uint16_t reg, uint16_t value)
{
    UNUSED(user);
    BK4819_WriteRegister((BK4819_REGISTER_t)reg, value);
}
static void Dock_HalSend(void *user, const uint8_t *buf, uint16_t len)
{
    UNUSED(user);
    UART_Send(buf, len);
}
// F5: engage/disengage the physical PA chain on a REG_30 TX-enable edge (defined
// below, after the F3a RX helper it reuses on un-key).
static void Dock_TxSet(void *user, bool on);
// F6: apply a whole repeater channel to the radio's OWN VFO (0x0873, below),
// reporting back through `out` what the radio actually ended up on (0x0874).
static void Dock_SetVfo(void *user, const dock_vfo_t *want, dock_vfo_applied_t *out);
static const dock_hal_t Dock_Hal = {
    Dock_HalRead, Dock_HalWrite, Dock_HalSend, NULL, Dock_TxSet, Dock_SetVfo
};
static dock_ctx_t Dock_Ctx;
static bool       Dock_Inited = false;

static void Dock_EnsureInit(void)
{
    if (!Dock_Inited) { dock_init(&Dock_Ctx, &Dock_Hal); Dock_Inited = true; }
}

// Dispatch one decoded, CRC-validated top-level dock command
// (0x0850/0x0851/0x0871). pUART_Command->Buffer holds the de-obfuscated payload
// [ID][Size][params]; the frame size is 4 + inner Header.Size.
static void Dock_HandleCommand(const UART_Command_t *pCmd)
{
    Dock_EnsureInit();
    dock_dispatch(&Dock_Ctx, pCmd->Buffer, (uint16_t)(4 + pCmd->Header.Size));
}

// Bring the RX audio path fully alive for the whole full-control session.
// radio-server reads raw audio off the AIOC continuously and gates in software,
// so it needs audio flowing the entire time it holds full-control. But while we
// block below, the firmware's APP_StartListening() never runs — so nothing raises
// GPIOA8 (the external AF-amp enable, an MCU GPIO the dock CANNOT reach) or flips
// REG_47 out of mute. Force both up-front, plus the normal RX AF/DAC gain.
//
// F3a proved this empirically: over the dock we forced REG_30=0xBFF1 (RX chain),
// REG_47=0x6142 (FM/unmute) and REG_48 loud, confirmed all three by read-back —
// yet the AIOC still read the noise floor (~109 RMS). BK4819 fully RX-alive but
// silent ⇒ the dead gate is outside the BK4819 = GPIOA8. Only the firmware can
// raise it. Undone on 0x0871 exit by RADIO_SetupRegisters(true), which calls
// AUDIO_AudioPathOff() and leaves REG_47 at MUTE (bench-confirmed by the F3a
// baseline→post-exit register diff).
static void Dock_ForceRxAudioAlive(void)
{
    GPIO_EnableAudioPath();             // GPIOA8 high — the un-dockable audio-amp gate
    gEnableSpeaker = true;
    BK4819_SetAF(BK4819_AF_FM);         // REG_47 = 0x6142 (unmute)
    BK4819_SetRxAudioGain();            // REG_48 — normal RX AF/DAC gain from EEPROM
}

// F5 — engage the physical PA on a dock-mode key. radio-server keys TX by writing
// BK4819 REG_30 (TX_DSP), which lights the modulator but never the external PA
// rail (REG_33 GPIO1 PA_ENABLE) or the PA bias (REG_36) — those pins are simply
// not in radio-server's register writes, so the chip modulates and nothing
// radiates (F4 Chain B: keyed, 0 radiated carrier; near-field chip RF only). This
// is the TX mirror of the F3a RX force-open. dock.c detects the REG_30 TX-enable
// edge and calls Dock_TxSet(); we add exactly the steps stock RADIO_SetTxParameters
// (radio.c:972) does that a bare REG_30 write skips, in the same order.
//
// Frequency/bias come from gCurrentVfo (the boot VFO), same source stock uses.
// In dock mode the host tunes via REG_38/39, which may differ from gCurrentVfo;
// on the UHF bench (both radios 445.800) the band matches, and a VHF/UHF mismatch
// would only mis-scale power, not prevent keying. ⚠ verify-on-bench: which
// OUTPUT_POWER level dock TX radiates, and confirm the gCurrentVfo freq source.
static void Dock_ForceTx(void)
{
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
    BK4819_PrepareTransmit();           // REG_50/37/52 un-mute + REG_30 TX word
    SYSTEM_DelayMs(10);
    BK4819_PickRXFilterPathBasedOnFrequency(gCurrentVfo->pTX->Frequency);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, true);   // the missing PA/antenna enable
    SYSTEM_DelayMs(5);
    BK4819_SetupPowerAmplifier(gCurrentVfo->TXP_CalculatedSetting, gCurrentVfo->pTX->Frequency);
    SYSTEM_DelayMs(10);
}

// Drop the PA in the stock un-key order — bias to 0 (REG_36) BEFORE the PA-enable
// GPIO (radio.c:782->784) — then restore RX: re-assert RX-enable and re-open the
// F3a RX-audio path (PrepareTransmit's ExitBypass left REG_47 at MUTE), so the
// receiver hears again after a service announcement.
static void Dock_EndTx(void)
{
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);
    Dock_ForceRxAudioAlive();
}

static void Dock_TxSet(void *user, bool on)
{
    UNUSED(user);
    if (on)
        Dock_ForceTx();
    else
        Dock_EndTx();
}

// F6 — 0x0873 set-VFO. The one dock command that is meant to OUTLIVE the dock
// session, and the reason it has to exist:
//
// Everything else here writes BK4819 registers, and none of it survives. 0x0870
// backs the registers up and the exit below ends in RADIO_SetupRegisters(true),
// which retunes the synthesiser from the radio's own VFO. So a host that tunes
// by register can never hand this radio a channel and walk away — the moment it
// lets go, the radio goes back to whatever its front panel said. That is why
// radio-server's 37 repeater presets could be applied, read back correctly, and
// still never key a machine.
//
// So set the VFO the radio actually transmits from, then let the firmware's own
// chain do the work: RADIO_ApplyOffset computes the TX leg from the offset and
// direction, and RADIO_ConfigureSquelchAndOutputPower derives the PA setting
// from the per-band calibration in flash — the calibration the host cannot read
// and must not invent (the mistake ADR 0128 removed). Afterwards the radio is
// genuinely on the channel, screen and all, exactly as if a thumb had dialled it.

// Resolve a CTCSS tone in tenths of a Hz against the firmware's OWN table.
// Exact match only: "nearest" would silently transmit a tone the operator did
// not ask for, and a repeater with a different tone simply will not open — a
// quiet wrong answer where a refusal is recoverable.
static bool Dock_CtcssIndex(uint16_t tenths, uint8_t *out)
{
    for (uint8_t i = 0; i < ARRAY_SIZE(CTCSS_Options); i++) {
        if (CTCSS_Options[i] == tenths) { *out = i; return true; }
    }
    return false;
}

// Is this frequency actually inside a band this radio has, in the radio's own
// 10 Hz units? FREQUENCY_GetBand() cannot answer that on its own: it CLAMPS,
// returning BAND1_50MHz for anything below the bottom and BAND7_470MHz for
// anything above the top, so it never reports a miss. Every out-of-range tune
// therefore looks like a valid one and lands on a band whose PA calibration has
// nothing to do with the requested frequency (radio-server ADR 0132/0134). So
// take its answer and check the frequency really falls inside that band.
static bool Dock_FreqInBand(uint32_t freq10, uint8_t *band_out)
{
    const FREQUENCY_Band_t band = FREQUENCY_GetBand(freq10);
    if (freq10 < frequencyBandTable[band].lower || freq10 > frequencyBandTable[band].upper)
        return false;
    *band_out = (uint8_t)band;
    return true;
}

static void Dock_ApplyVfo(VFO_Info_t *vfo, const dock_vfo_t *want,
                          uint32_t rx10, uint32_t off10, uint8_t band,
                          const uint8_t *ctcss_idx)
{
    // Point the RX/TX views at their own storage. Normally already true, but a
    // VFO left in FrequencyReverse would otherwise have us fill in the leg the
    // radio is not going to use.
    vfo->FrequencyReverse = false;
    vfo->pRX = &vfo->freq_config_RX;
    vfo->pTX = &vfo->freq_config_TX;

    vfo->freq_config_RX.Frequency          = rx10;
    vfo->TX_OFFSET_FREQUENCY               = off10;
    vfo->TX_OFFSET_FREQUENCY_DIRECTION     = want->direction;
    RADIO_ApplyOffset(vfo);                 // fills freq_config_TX.Frequency

    // CTCSS is transmit-only, matching radio-server's preset model (rx_tone is
    // carried but never honoured there either), so an unexpected tone on the
    // repeater's output can never mute our receiver. The index was resolved and
    // any failure refused before we got here — this only writes it.
    if (ctcss_idx != NULL) {
        vfo->freq_config_TX.CodeType = CODE_TYPE_CONTINUOUS_TONE;
        vfo->freq_config_TX.Code     = *ctcss_idx;
    } else {
        vfo->freq_config_TX.CodeType = CODE_TYPE_OFF;
        vfo->freq_config_TX.Code     = 0;
    }
    vfo->freq_config_RX.CodeType = CODE_TYPE_OFF;
    vfo->freq_config_RX.Code     = 0;

    vfo->CHANNEL_BANDWIDTH = want->narrow ? BANDWIDTH_NARROW : BANDWIDTH_WIDE;
    vfo->Modulation        = MODULATION_FM;
    vfo->OUTPUT_POWER      = want->power;
    vfo->Band              = band;

    RADIO_ConfigureSquelchAndOutputPower(vfo);   // TXP_CalculatedSetting, per band
}

static void Dock_SetVfo(void *user, const dock_vfo_t *want, dock_vfo_applied_t *out)
{
    UNUSED(user);

    // The wire carries Hz; this radio's VFO stores units of 10 Hz (see dock.h).
    // Nothing downstream would complain about the mistake — it would just tune
    // ten times too high, get clamped into band 7, and transmit nowhere.
    const uint32_t rx10  = want->rx_hz / 10u;
    const uint32_t off10 = want->offset_hz / 10u;

    uint8_t band;
    if (!Dock_FreqInBand(rx10, &band)) {
        out->status = DOCK_VFO_ERR_BAND;
        return;
    }

    // Validate the leg that actually radiates at least as hard as the one that
    // only listens. Mirrors RADIO_ApplyOffset, including refusing an offset
    // larger than the frequency rather than letting the subtraction wrap.
    uint32_t tx10 = rx10;
    if (want->direction == DOCK_OFFSET_ADD) {
        tx10 = rx10 + off10;
    } else if (want->direction == DOCK_OFFSET_SUB) {
        if (off10 >= rx10) {
            out->status = DOCK_VFO_ERR_BAND;
            return;
        }
        tx10 = rx10 - off10;
    }
    uint8_t tx_band;
    if (!Dock_FreqInBand(tx10, &tx_band)) {
        out->status = DOCK_VFO_ERR_BAND;
        return;
    }

    // Resolve the tone BEFORE anything is applied. Dropping an unresolvable
    // tone and tuning anyway would leave the radio on the repeater's input
    // transmitting no tone: the machine stays shut, and nothing anywhere says
    // why.
    uint8_t idx = 0;
    const bool want_tone = (want->ctcss_tenths != 0);
    if (want_tone && !Dock_CtcssIndex(want->ctcss_tenths, &idx)) {
        out->status = DOCK_VFO_ERR_TONE;
        return;
    }

    // BOTH VFOs, deliberately. gCurrentVfo follows gRxVfo/gTxVfo and dual watch
    // alternates between them (RADIO_SelectCurrentVfo, radio.c), so setting only
    // one leaves which frequency we transmit on up to a timer. Setting both makes
    // the answer the same either way.
    for (unsigned i = 0; i < ARRAY_SIZE(gEeprom.VfoInfo); i++)
        Dock_ApplyVfo(&gEeprom.VfoInfo[i], want, rx10, off10, band,
                      want_tone ? &idx : NULL);

    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);

    // Report the channel the radio is on, read back out of its own struct after
    // RADIO_ApplyOffset — not the values that were asked for.
    const VFO_Info_t *v = &gEeprom.VfoInfo[0];
    out->rx_hz        = v->freq_config_RX.Frequency * 10u;
    out->tx_hz        = v->freq_config_TX.Frequency * 10u;
    out->ctcss_tenths = (v->freq_config_TX.CodeType == CODE_TYPE_CONTINUOUS_TONE)
                        ? CTCSS_Options[v->freq_config_TX.Code] : 0;
    out->status       = DOCK_VFO_APPLIED;
}

// 0x0870 enter full-control: force RX audio alive (above), then block here,
// re-entrantly servicing register R/W until 0x0871 clears the flag. While blocked,
// the 10 ms timeslice is starved, so CheckRadioInterrupts() — the only path that
// reprograms the BK4819 — cannot run and fight us, and there is no hardware
// watchdog on this tree to feed (both derived from the V3 tree, not assumed).
static void Dock_EnterFullControl(uint32_t Port)
{
    Dock_EnsureInit();
    if (Dock_Ctx.full_control)
        return;                         // already inside — prevent recursion
    Dock_Ctx.full_control = true;
    Dock_ForceRxAudioAlive();           // F3a: RX audio path was dead without this
    while (Dock_Ctx.full_control)
    {
        if (UART_IsCommandAvailable(Port))
            UART_HandleCommand(Port);   // routes 0x0850/0x0851/0x0871 to dock
    }
    RADIO_SetupRegisters(true);         // resume normal (muted) RX on exit
}
#endif // ENABLE_DOCK

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
            *pReadPointer = DMA_INDEX((*pReadPointer), 1, ReadBufSize);

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

#ifdef ENABLE_DOCK
        case 0x0850:   // write BK4819 registers (no reply)
        case 0x0851:   // read BK4819 registers -> one 0x0951 reply each
        case 0x0871:   // exit full-control (clears the loop flag)
        case 0x0873:   // set the radio's own VFO -> one 0x0874 reply — F6
            // 0x0873 sits HERE, in the ordinary non-blocking dispatch, and not
            // with 0x0870 below. That is the point of it: the main loop keeps
            // running, so the radio keeps sampling its own PTT pin and stays a
            // radio. Entering full-control to tune would starve the very loop
            // whose output we are trying to set up.
            Dock_HandleCommand(pUART_Command);
            break;

        case 0x0870:   // enter full-control (blocking register-servicing loop)
            Dock_EnterFullControl(Port);
            break;
#endif
    } // switch

    #ifdef ENABLE_FEAT_F4HWN_K5VIEWER
        gUART_LockK5Viewer = 20; // lock the K5Viewer stream
    #endif
}