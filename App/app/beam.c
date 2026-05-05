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

static VFO_Info_t gBeamBackupVfo;
static VFO_Info_t gBeamRadioVfo;
static uint16_t   gBeamBackupScreenChannel;
static uint8_t    gBeamVfoIndex;
static uint8_t    gBeamBackupRxVfoIndex;
static bool       gBeamHasBackup;

static void BEAM_SaveBackup(void)
{
    gBeamVfoIndex = gEeprom.TX_VFO;
    memcpy(&gBeamBackupVfo, &gEeprom.VfoInfo[gBeamVfoIndex], sizeof(gBeamBackupVfo));
    gBeamBackupScreenChannel = gEeprom.ScreenChannel[gBeamVfoIndex];
    gBeamBackupRxVfoIndex = gEeprom.RX_VFO;
    gBeamHasBackup = true;
}

static void BEAM_RestoreBackup(void)
{
    if (!gBeamHasBackup)
        return;

    memcpy(&gEeprom.VfoInfo[gBeamVfoIndex], &gBeamBackupVfo, sizeof(gBeamBackupVfo));
    gEeprom.ScreenChannel[gBeamVfoIndex] = gBeamBackupScreenChannel;
    gEeprom.RX_VFO = gBeamBackupRxVfoIndex;
    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);
    gBeamHasBackup = false;
}

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

static void BEAM_CopyNameFromActiveVfo(char *name)
{
    memset(name, 0, 16);

    if (IS_MR_CHANNEL(gBeamBackupVfo.CHANNEL_SAVE)) {
        SETTINGS_FetchChannelName(name, gBeamBackupVfo.CHANNEL_SAVE);
    } else {
        memcpy(name, gBeamBackupVfo.Name, sizeof(gBeamBackupVfo.Name));
    }
}

static void BEAM_FillPayload(BEAM_Payload_t *payload)
{
    memset(payload, 0, sizeof(*payload));

    payload->magic = BEAM_PACKET_MAGIC;
    payload->version = BEAM_PACKET_VERSION;
    payload->rx_frequency = gBeamBackupVfo.freq_config_RX.Frequency;
    payload->tx_offset_frequency = gBeamBackupVfo.TX_OFFSET_FREQUENCY;
    payload->rx_code = gBeamBackupVfo.freq_config_RX.Code;
    payload->tx_code = gBeamBackupVfo.freq_config_TX.Code;
    payload->rx_codetype = gBeamBackupVfo.freq_config_RX.CodeType;
    payload->tx_codetype = gBeamBackupVfo.freq_config_TX.CodeType;
    payload->modulation = gBeamBackupVfo.Modulation;
    payload->tx_offset_direction = gBeamBackupVfo.TX_OFFSET_FREQUENCY_DIRECTION;
    payload->tx_lock = gBeamBackupVfo.TX_LOCK;
    payload->busy_channel_lock = gBeamBackupVfo.BUSY_CHANNEL_LOCK;
    payload->output_power = gBeamBackupVfo.OUTPUT_POWER;
    payload->channel_bandwidth = gBeamBackupVfo.CHANNEL_BANDWIDTH;
    payload->frequency_reverse = gBeamBackupVfo.FrequencyReverse;
    payload->dtmf_ptt_id_mode = gBeamBackupVfo.DTMF_PTT_ID_TX_MODE;
#ifdef ENABLE_DTMF_CALLING
    payload->dtmf_decoding_enable = gBeamBackupVfo.DTMF_DECODING_ENABLE;
#endif
    payload->step_setting = gBeamBackupVfo.STEP_SETTING;
    payload->scrambling_type = gBeamBackupVfo.SCRAMBLING_TYPE;
    payload->band = gBeamBackupVfo.Band;
    payload->scanlist = gBeamBackupVfo.SCANLIST_PARTICIPATION;
    payload->compander = gBeamBackupVfo.Compander;

    BEAM_CopyNameFromActiveVfo(payload->name);
}

static void BEAM_SendPacket(void)
{
    BEAM_Payload_t payload;

    BEAM_FillPayload(&payload);

    memset(g_FSK_Buffer, 0, sizeof(g_FSK_Buffer));
    g_FSK_Buffer[0] = 0xABCDu;
    memcpy(&g_FSK_Buffer[2], &payload, sizeof(payload));
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

static uint16_t BEAM_FindFirstFreeChannel(void)
{
    for (uint16_t channel = MR_CHANNEL_FIRST; IS_MR_CHANNEL(channel); channel++) {
        if (!RADIO_CheckValidChannel(channel, false, 0))
            return channel;
    }

    return 0xFFFFu;
}

static bool BEAM_SavePayloadToFirstFreeChannel(const BEAM_Payload_t *payload)
{
    uint16_t channel = BEAM_FindFirstFreeChannel();
    char name[16];

    if (channel == 0xFFFFu) {
        gBeamStatus = BEAM_STATUS_RX_FULL;
        gUpdateDisplay = true;
        return false;
    }

    memcpy(name, payload->name, sizeof(name));
    name[sizeof(name) - 1] = 0;

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
    memcpy(vfo.Name, name, sizeof(vfo.Name));
    RADIO_ApplyOffset(&vfo);
    RADIO_ConfigureSquelchAndOutputPower(&vfo);

    SETTINGS_SaveChannel(channel, gBeamVfoIndex, &vfo, 3);
    SETTINGS_SaveChannelName(channel, name);

    gBeamCopiedChannel = channel;
    gBeamStatus = BEAM_STATUS_RX_SAVED;
    gUpdateDisplay = true;
    return true;
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
        gEeprom.MrChannel[gBeamVfoIndex] = gBeamCopiedChannel;
        gEeprom.ScreenChannel[gBeamVfoIndex] = gBeamCopiedChannel;
        RADIO_ConfigureChannel(gBeamVfoIndex, VFO_CONFIGURE_RELOAD);
        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);
        gBeamHasBackup = false;
    } else {
        BEAM_RestoreBackup();
    }

    GUI_SelectNextDisplay(DISPLAY_MAIN);
    gRequestDisplayScreen = DISPLAY_MAIN;
    gBeamActive = false;
    gUpdateDisplay = true;
}

void ACTION_Beam(void)
{
    BEAM_SaveBackup();
    gBeamMode = BEAM_MODE_TX;
    gBeamStatus = BEAM_STATUS_READY;
    gBeamCopiedChannel = 0xFFFFu;
    gBeamRxWordCount = 0;
    gBeamActive = true;
    GUI_SelectNextDisplay(DISPLAY_MAIN);
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;
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
        gBeamMode = (gBeamMode == BEAM_MODE_TX) ? BEAM_MODE_RX : BEAM_MODE_TX;
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

    BEAM_Payload_t payload;
    memcpy(&payload, &g_FSK_Buffer[2], sizeof(payload));

    if (payload.magic != BEAM_PACKET_MAGIC || payload.version != BEAM_PACKET_VERSION)
        goto error;

    BEAM_SavePayloadToFirstFreeChannel(&payload);
    BK4819_ResetFSK();
    return;

error:
    gBeamStatus = BEAM_STATUS_ERROR;
    gBeamRxWordCount = 0;
    gUpdateDisplay = true;
}

#endif // ENABLE_FEAT_F4HWN_BEAM
