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

#ifdef ENABLE_FEAT_F4HWN_BEAM

#include <assert.h>
#include <string.h>

#include "app/aircopy.h"
#include "app/beam.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/crc.h"
#include "driver/st7565.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/main.h"
#include "ui/ui.h"

#define BEAM_PACKET_MAGIC   0xBEA5u
#define BEAM_PACKET_VERSION 2u

// One byte per field instead of bit-packing — payload still fits in 64 bytes,
// and avoids costly shift/mask/store sequences on Cortex-M.
typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  _pad;
    uint32_t rx_frequency;
    uint32_t tx_offset_frequency;
    uint8_t  rx_code;
    uint8_t  tx_code;
    uint8_t  rx_codetype;
    uint8_t  tx_codetype;
    uint8_t  modulation;
    uint8_t  tx_offset_direction;
    uint8_t  tx_lock;
    uint8_t  busy_channel_lock;
    uint8_t  output_power;
    uint8_t  channel_bandwidth;
    uint8_t  frequency_reverse;
    uint8_t  dtmf_ptt_id_mode;
    uint8_t  dtmf_decoding_enable;
    uint8_t  step_setting;
    uint8_t  scrambling_type;
    uint8_t  band;
    uint8_t  scanlist;
    uint8_t  compander;
    char     name[16];
} BEAM_Payload_t;

static_assert(sizeof(BEAM_Payload_t) <= 64);

BEAM_Mode_t   gBeamMode = BEAM_MODE_TX;
BEAM_Status_t gBeamStatus = BEAM_STATUS_READY;
uint16_t      gBeamCopiedChannel = 0xFFFFu;
uint8_t       gBeamRxWordCount;
bool          gBeamActive;

static VFO_Info_t gBeamRadioVfo;

static void BEAM_SetRadioToBeamFrequency(void)
{
    const uint16_t channel = FREQ_CHANNEL_FIRST + BAND6_400MHz;

    RADIO_InitInfo(&gBeamRadioVfo, channel, DEFAULT_FREQ);
    gBeamRadioVfo.CHANNEL_BANDWIDTH = BANDWIDTH_NARROW;
    gBeamRadioVfo.OUTPUT_POWER = OUTPUT_POWER_LOW1;
    RADIO_ConfigureSquelchAndOutputPower(&gBeamRadioVfo);

    gRxVfo = &gBeamRadioVfo;
    gTxVfo = &gBeamRadioVfo;
    gCurrentVfo = gRxVfo;
    RADIO_SetupRegisters(true);
    BK4819_SetupAircopy();
    BK4819_ResetFSK();
}

static void BEAM_SendPacket(void)
{
    memset(g_FSK_Buffer, 0, sizeof(g_FSK_Buffer));
    g_FSK_Buffer[0] = 0xABCDu;
    
    BEAM_Payload_t * const payload = (BEAM_Payload_t *)&g_FSK_Buffer[2];
    
    const VFO_Info_t *vfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];

    payload->magic = BEAM_PACKET_MAGIC;
    payload->version = BEAM_PACKET_VERSION;
    payload->rx_frequency = vfo->freq_config_RX.Frequency;
    payload->tx_offset_frequency = vfo->TX_OFFSET_FREQUENCY;
    payload->rx_code = vfo->freq_config_RX.Code;
    payload->tx_code = vfo->freq_config_TX.Code;
    payload->rx_codetype = vfo->freq_config_RX.CodeType;
    payload->tx_codetype = vfo->freq_config_TX.CodeType;
    payload->modulation = vfo->Modulation;
    payload->tx_offset_direction = vfo->TX_OFFSET_FREQUENCY_DIRECTION;
    payload->tx_lock = vfo->TX_LOCK;
    payload->busy_channel_lock = vfo->BUSY_CHANNEL_LOCK;
    payload->output_power = vfo->OUTPUT_POWER;
    payload->channel_bandwidth = vfo->CHANNEL_BANDWIDTH;
    payload->frequency_reverse = vfo->FrequencyReverse;
    payload->dtmf_ptt_id_mode = vfo->DTMF_PTT_ID_TX_MODE;
#ifdef ENABLE_DTMF_CALLING
    payload->dtmf_decoding_enable = vfo->DTMF_DECODING_ENABLE;
#endif
    payload->step_setting = vfo->STEP_SETTING;
    payload->scrambling_type = vfo->SCRAMBLING_TYPE;
    payload->band = vfo->Band;
    payload->scanlist = vfo->SCANLIST_PARTICIPATION;
    payload->compander = vfo->Compander;

    if (IS_MR_CHANNEL(vfo->CHANNEL_SAVE)) {
        SETTINGS_FetchChannelName(payload->name, vfo->CHANNEL_SAVE);
    } else {
        memcpy(payload->name, vfo->Name, sizeof(vfo->Name));
    }

    g_FSK_Buffer[34] = CRC_Calculate(&g_FSK_Buffer[1], 2 + 64);
    g_FSK_Buffer[35] = 0xDCBAu;

    AIRCOPY_Obfuscate(32);

    // Show "SENDING" before the blocking FSK transmission so the user gets
    // immediate visual feedback instead of a frozen "BEAM TX" screen.
    gBeamStatus = BEAM_STATUS_TX_WAIT;
    UI_DisplayMain();
    ST7565_BlitFullScreen();

    RADIO_SetTxParameters();
    BK4819_SendFSKData(g_FSK_Buffer);
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);
    gBeamStatus = BEAM_STATUS_TX_DONE;
    gBeepToPlay = BEEP_880HZ_60MS_TRIPLE_BEEP;
    gUpdateDisplay = true;
}

static void BEAM_SavePayloadToFirstFreeChannel(const BEAM_Payload_t *payload)
{
    uint16_t channel = 0xFFFFu;
    
    for (uint16_t c = MR_CHANNEL_FIRST; IS_MR_CHANNEL(c); c++) {
        if (!RADIO_CheckValidChannel(c, false, 0)) {
            channel = c;
            break;
        }
    }

    if (channel == 0xFFFFu) {
        gBeamStatus = BEAM_STATUS_RX_FULL;
        gUpdateDisplay = true;
        return;
    }

    VFO_Info_t vfo;
    RADIO_InitInfo(&vfo, channel, payload->rx_frequency);

    // CRC + magic + version already validate the payload — no need to clamp fields.
    vfo.TX_OFFSET_FREQUENCY = payload->tx_offset_frequency;
    vfo.freq_config_RX.Code = payload->rx_code;
    vfo.freq_config_TX.Code = payload->tx_code;
    vfo.freq_config_RX.CodeType = payload->rx_codetype;
    vfo.freq_config_TX.CodeType = payload->tx_codetype;
    vfo.TX_OFFSET_FREQUENCY_DIRECTION = payload->tx_offset_direction;
    vfo.Modulation = payload->modulation;
    vfo.TX_LOCK = payload->tx_lock;
    vfo.BUSY_CHANNEL_LOCK = payload->busy_channel_lock;
    vfo.OUTPUT_POWER = payload->output_power;
    vfo.CHANNEL_BANDWIDTH = payload->channel_bandwidth;
    vfo.FrequencyReverse = payload->frequency_reverse;
    vfo.DTMF_PTT_ID_TX_MODE = payload->dtmf_ptt_id_mode;
#ifdef ENABLE_DTMF_CALLING
    vfo.DTMF_DECODING_ENABLE = payload->dtmf_decoding_enable;
#endif
    vfo.STEP_SETTING = payload->step_setting;
    vfo.StepFrequency = gStepFrequencyTable[vfo.STEP_SETTING];
    vfo.SCRAMBLING_TYPE = payload->scrambling_type;
    vfo.Band = payload->band;
    vfo.SCANLIST_PARTICIPATION = payload->scanlist;
    vfo.Compander = payload->compander;
    
    memcpy(vfo.Name, payload->name, sizeof(vfo.Name));
    vfo.Name[sizeof(vfo.Name) - 1] = '\0';
    
    RADIO_ApplyOffset(&vfo);
    RADIO_ConfigureSquelchAndOutputPower(&vfo);

    SETTINGS_SaveChannel(channel, gEeprom.TX_VFO, &vfo, 3);
    SETTINGS_SaveChannelName(channel, vfo.Name);

    gBeamCopiedChannel = channel;
    gBeamStatus = BEAM_STATUS_RX_SAVED;
    gUpdateDisplay = true;
    BACKLIGHT_TurnOn();
    return;
}

static void BEAM_KeyMenu(void)
{
    BEAM_SetRadioToBeamFrequency();

    if (gBeamMode == BEAM_MODE_TX) {
        BEAM_SendPacket();
    } else {
        gBeamStatus = BEAM_STATUS_RX_WAIT;
        gBeamCopiedChannel = 0xFFFFu;
        gBeamRxWordCount = 0;
        gFSKWriteIndex = 0;
        BK4819_PrepareFSKReceive();
    }
}

static void BEAM_KeyExit(void)
{
    BK4819_ResetFSK();

    if (gBeamMode == BEAM_MODE_RX && gBeamCopiedChannel != 0xFFFFu) {
        gEeprom.MrChannel[gEeprom.TX_VFO] = gBeamCopiedChannel;
        gEeprom.ScreenChannel[gEeprom.TX_VFO] = gBeamCopiedChannel;
        RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE_RELOAD);
    }
    
    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);

    GUI_SelectNextDisplay(DISPLAY_MAIN);
    gBeamActive = false;
}

void ACTION_Beam(void)
{
    gBeamMode = BEAM_MODE_TX;
    gBeamStatus = BEAM_STATUS_READY;
    gBeamCopiedChannel = 0xFFFFu;
    gBeamRxWordCount = 0;
    gBeamActive = true;
    GUI_SelectNextDisplay(DISPLAY_MAIN);
}

void BEAM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed)
        return;

    if (Key != KEY_PTT)
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

    switch (Key) {
    case KEY_UP:
    case KEY_DOWN:
        gBeamMode ^= 1; // (gBeamMode == BEAM_MODE_TX) ? BEAM_MODE_RX : BEAM_MODE_TX
        gBeamStatus = BEAM_STATUS_READY;
        gBeamRxWordCount = 0;
        break;
    case KEY_MENU:
        BEAM_KeyMenu();
        break;
    case KEY_EXIT:
        BEAM_KeyExit();
        return;
    case KEY_PTT:
        break;
    default:
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        break;
    }

    gUpdateDisplay = true;
}

void BEAM_StorePacket(void)
{
    if (gFSKWriteIndex < 36)
        return;

    gBeamRxWordCount = gFSKWriteIndex;
    gFSKWriteIndex = 0;

    const uint16_t Status = BK4819_ReadRegister(BK4819_REG_0B);
    BK4819_PrepareFSKReceive();

    if ((Status & 0x0010U) != 0 || g_FSK_Buffer[0] != 0xABCDu || g_FSK_Buffer[35] != 0xDCBAu)
        goto error;

    AIRCOPY_Obfuscate(32);

    if (g_FSK_Buffer[34] != CRC_Calculate(&g_FSK_Buffer[1], 2 + 64))
        goto error;

    BEAM_Payload_t * const payload = (BEAM_Payload_t *)&g_FSK_Buffer[2];

    if (payload->magic != BEAM_PACKET_MAGIC || payload->version != BEAM_PACKET_VERSION)
        goto error;

    BEAM_SavePayloadToFirstFreeChannel(payload);
    BK4819_ResetFSK();
    return;

error:
    gBeamStatus = BEAM_STATUS_ERROR;
    gBeamRxWordCount = 0;
    gUpdateDisplay = true;
    BACKLIGHT_TurnOn();
}

#endif // ENABLE_FEAT_F4HWN_BEAM
