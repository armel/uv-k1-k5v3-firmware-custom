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

#include "app/dtmf.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/py25q16.h"
#include "misc.h"
#include "settings.h"
#include "ui/menu.h"

EEPROM_Config_t gEeprom = { 0 };

void SETTINGS_InitEEPROM(void)
{
    uint8_t Data[16] = {0};

    //
    // Version check
    // Read stored version from EEPROM and compare with VERSION_STRING_2
    // 
    {
        char storedVersion[16] = {0};
        PY25Q16_ReadBuffer(0x00A160, storedVersion, sizeof(storedVersion));

        // Compare with current version
        if (strncmp(storedVersion, VERSION_STRING_2, sizeof(storedVersion)) != 0)
        {
            // Different version: new install or firmware update

            // 1. Write new version to EEPROM
            char newVersion[16] = {0};
            strncpy(newVersion, VERSION_STRING_2, sizeof(newVersion));
            PY25Q16_WriteBuffer(0x00A160, newVersion, sizeof(newVersion), false);

            // 2. Reset sensitive parameters (MENU_LOCK, etc.)
            uint8_t configByte[8] = {0};
            PY25Q16_ReadBuffer(0x00A000, configByte, sizeof(configByte));

            configByte[4] &= (uint8_t)~0x01;  // KEY_LOCK = 0
            configByte[4] &= (uint8_t)~0x02;  // MENU_LOCK = 0
            configByte[4] &= (uint8_t)~0x3C;  // SET_KEY = 0
            configByte[4] &= (uint8_t)~0x40;  // SET_NAV = 0

            PY25Q16_WriteBuffer(0x00A000, configByte, sizeof(configByte), false);

            // 3. Reset display inversion (SET_INV = 0)
            uint8_t displayByte[8] = {0};
            PY25Q16_ReadBuffer(0x00A158, displayByte, sizeof(displayByte));

            displayByte[5] &= (uint8_t)~0x10;  // Clear bit 4 (SET_INV)

            PY25Q16_WriteBuffer(0x00A158, displayByte, sizeof(displayByte), false);

            // 4. Reset logo lines (clear to null for strlen() == 0)

            char logoLines[32];
            PY25Q16_ReadBuffer(0x00A0C8, logoLines, sizeof(logoLines));

            bool needsWrite = false;

            for (int line = 0; line < 2; line++) {
                int offset = line * 16;
                
                for (int i = 0; i < 16; i++) {
                    char c = logoLines[offset + i];
                    if (c == 0) {
                        break;
                    }
                    if (c < 0x20 || c > 0x7E) {
                        memset(logoLines + offset, 0, 16);
                        needsWrite = true;
                        break;
                    }
                }
            }

            if (needsWrite) {
                PY25Q16_WriteBuffer(0x00A0C8, logoLines, sizeof(logoLines), false);
            }
        }
    }

    // 0E70..0E77
    PY25Q16_ReadBuffer(0x00A000, Data, 8);
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        gSetting_set_audio = (Data[0] < 5) ? Data[0] : 0;
    #endif
    gEeprom.SQUELCH_LEVEL        = (Data[1] < 10) ? Data[1] : 1;
    gEeprom.TX_TIMEOUT_TIMER     = (Data[2] > 4 && Data[2] < 180) ? Data[2] : 11;
    #ifdef ENABLE_NOAA
        gEeprom.NOAA_AUTO_SCAN   = (Data[3] <  2) ? Data[3] : false;
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        gEeprom.KEY_LOCK = (Data[4] & 0x01) != 0;
        gEeprom.MENU_LOCK = (Data[4] & 0x02) != 0;
        gEeprom.SET_KEY = ((Data[4] >> 2) & 0x0F) > 4 ? 0 : (Data[4] >> 2) & 0x0F;
        gEeprom.SET_NAV = (Data[4] & 0x40) != 0;
    #else
        gEeprom.KEY_LOCK             = (Data[4] <  2) ? Data[4] : false;
    #endif
    #ifdef ENABLE_VOX
        gEeprom.VOX_SWITCH       = (Data[5] <  2) ? Data[5] : false;
        gEeprom.VOX_LEVEL        = (Data[6] < 10) ? Data[6] : 1;
    #endif
    gEeprom.MIC_SENSITIVITY      = (Data[7] <  5) ? Data[7] : 4;

    // 0E78..0E7F
    PY25Q16_ReadBuffer(0x00A008, Data, 8);
    gEeprom.BACKLIGHT_MAX         = (Data[0] & 0xF) <= 10 ? (Data[0] & 0xF) : 10;
    gEeprom.BACKLIGHT_MIN         = (Data[0] >> 4) < gEeprom.BACKLIGHT_MAX ? (Data[0] >> 4) : 0;
#ifdef ENABLE_BLMIN_TMP_OFF
    gEeprom.BACKLIGHT_MIN_STAT    = BLMIN_STAT_ON;
#endif
    gEeprom.CHANNEL_DISPLAY_MODE  = (Data[1] < 4) ? Data[1] : MDF_FREQUENCY;    // 4 instead of 3 - extra display mode
    gEeprom.CROSS_BAND_RX_TX      = (Data[2] < 3) ? Data[2] : CROSS_BAND_OFF;
    gEeprom.BATTERY_SAVE          = (Data[3] < 6) ? Data[3] : 4;
    gEeprom.DUAL_WATCH            = (Data[4] < 3) ? Data[4] : DUAL_WATCH_CHAN_A;
    gEeprom.BACKLIGHT_TIME        = (Data[5] < 62) ? Data[5] : 12;
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        gEeprom.TAIL_TONE_ELIMINATION = Data[6] & 0x01;
        gSetting_set_nfm = (Data[6] >> 1) & 0x01;
        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
            gEeprom.VFO_OPEN = ((Data[6] >> 2) & 0x01) != 0 ? true : true;
        #endif
    #else
        gEeprom.TAIL_TONE_ELIMINATION = (Data[6] < 2) ? Data[6] : false;
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.CURRENT_STATE =  Data[7]        & 0x07;   // bits 0..2
        gEeprom.CURRENT_LIST  = (Data[7] >> 3)  & 0x1F;   // bits 3..7
    #else
        gEeprom.VFO_OPEN              = (Data[7] < 2) ? Data[7] : true;
    #endif

    // 0E80..0E87
    /*    
    PY25Q16_ReadBuffer(0x00A010, Data, 8);
    gEeprom.ScreenChannel[0]   = IS_VALID_CHANNEL(Data[0]) ? Data[0] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.ScreenChannel[1]   = IS_VALID_CHANNEL(Data[3]) ? Data[3] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.MrChannel[0]       = IS_MR_CHANNEL(Data[1])    ? Data[1] : MR_CHANNEL_FIRST;
    gEeprom.MrChannel[1]       = IS_MR_CHANNEL(Data[4])    ? Data[4] : MR_CHANNEL_FIRST;
    gEeprom.FreqChannel[0]     = IS_FREQ_CHANNEL(Data[2])  ? Data[2] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.FreqChannel[1]     = IS_FREQ_CHANNEL(Data[5])  ? Data[5] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
#ifdef ENABLE_NOAA
    gEeprom.NoaaChannel[0] = IS_NOAA_CHANNEL(Data[6])  ? Data[6] : NOAA_CHANNEL_FIRST;
    gEeprom.NoaaChannel[1] = IS_NOAA_CHANNEL(Data[7])  ? Data[7] : NOAA_CHANNEL_FIRST;
#endif
    */

// 0x00A010 .. 0x00A01F
uint16_t Data16[8];

PY25Q16_ReadBuffer(0x00A010, Data16, sizeof(Data16));

gEeprom.ScreenChannel[0] = IS_VALID_CHANNEL(Data16[0]) ? Data16[0] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
gEeprom.MrChannel[0]     = IS_MR_CHANNEL(Data16[1]) ? Data16[1] : MR_CHANNEL_FIRST;
gEeprom.FreqChannel[0]   = IS_FREQ_CHANNEL(Data16[2]) ? Data16[2] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
gEeprom.ScreenChannel[1] = IS_VALID_CHANNEL(Data16[3]) ? Data16[3] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
gEeprom.MrChannel[1]     = IS_MR_CHANNEL(Data16[4]) ? Data16[4] : MR_CHANNEL_FIRST;
gEeprom.FreqChannel[1]   = IS_FREQ_CHANNEL(Data16[5]) ? Data16[5] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);

#ifdef ENABLE_NOAA
    gEeprom.NoaaChannel[0]   = IS_NOAA_CHANNEL(Data16[6]) ? Data16[6] : NOAA_CHANNEL_FIRST;
    gEeprom.NoaaChannel[1]   = IS_NOAA_CHANNEL(Data16[7]) ? Data16[7] : NOAA_CHANNEL_FIRST;
#endif

#ifdef ENABLE_FMRADIO
    {   // 0E88..0E8F
        struct
        {
            uint16_t selFreq;
            uint8_t  selChn;
            uint8_t  isMrMode:1;
            uint8_t  band:2;
            //uint8_t  space:2;
        } __attribute__((packed)) fmCfg;
        PY25Q16_ReadBuffer(0x00A020, &fmCfg, 4);

        gEeprom.FM_Band = fmCfg.band;
        //gEeprom.FM_Space = fmCfg.space;
        gEeprom.FM_SelectedFrequency = 
            (fmCfg.selFreq >= BK1080_GetFreqLoLimit(gEeprom.FM_Band) && fmCfg.selFreq <= BK1080_GetFreqHiLimit(gEeprom.FM_Band)) ? 
                fmCfg.selFreq : BK1080_GetFreqLoLimit(gEeprom.FM_Band);
            
        gEeprom.FM_SelectedChannel = fmCfg.selChn;
        gEeprom.FM_IsMrMode        = fmCfg.isMrMode;
    }

    // 0E40..0E67
    PY25Q16_ReadBuffer(0x00A028, gFM_Channels, sizeof(gFM_Channels));
    FM_ConfigureChannelState();
#endif

    // 0E90..0E97
    PY25Q16_ReadBuffer(0x00A0A8, Data, 8);
    gEeprom.BEEP_CONTROL                 = Data[0] & 1;
    gEeprom.KEY_M_LONG_PRESS_ACTION      = ((Data[0] >> 1) < ACTION_OPT_LEN) ? (Data[0] >> 1) : ACTION_OPT_NONE;
    gEeprom.KEY_1_SHORT_PRESS_ACTION     = (Data[1] < ACTION_OPT_LEN) ? Data[1] : ACTION_OPT_MONITOR;
    gEeprom.KEY_1_LONG_PRESS_ACTION      = (Data[2] < ACTION_OPT_LEN) ? Data[2] : ACTION_OPT_NONE;
    gEeprom.KEY_2_SHORT_PRESS_ACTION     = (Data[3] < ACTION_OPT_LEN) ? Data[3] : ACTION_OPT_SCAN;
    gEeprom.KEY_2_LONG_PRESS_ACTION      = (Data[4] < ACTION_OPT_LEN) ? Data[4] : ACTION_OPT_NONE;
    gEeprom.SCAN_RESUME_MODE             = (Data[5] < 105)            ? Data[5] : 14;
    gEeprom.AUTO_KEYPAD_LOCK             = (Data[6] < 41)             ? Data[6] : 0;
#ifdef ENABLE_FEAT_F4HWN
    gEeprom.POWER_ON_DISPLAY_MODE        = (Data[7] < 6)              ? Data[7] : POWER_ON_DISPLAY_MODE_VOLTAGE;
#else
    gEeprom.POWER_ON_DISPLAY_MODE        = (Data[7] < 4)              ? Data[7] : POWER_ON_DISPLAY_MODE_VOLTAGE;
#endif

    // 0E98..0E9F
    #ifdef ENABLE_PWRON_PASSWORD
        PY25Q16_ReadBuffer(0x00A0A8 + 0x8, Data, 8);
        memcpy(&gEeprom.POWER_ON_PASSWORD, Data, 4);
    #endif

    // 0EA0..0EA7
    PY25Q16_ReadBuffer(0x00A0A8 + 0x10, Data, 8);
    #ifdef ENABLE_VOICE
    gEeprom.VOICE_PROMPT = (Data[0] < 3) ? Data[0] : VOICE_PROMPT_ENGLISH;
    #endif
    #ifdef ENABLE_RSSI_BAR
        if((Data[1] < 200 && Data[1] > 90) && (Data[2] < Data[1]-9 && Data[1] < 160  && Data[2] > 50)) {
            gEeprom.S0_LEVEL = Data[1];
            gEeprom.S9_LEVEL = Data[2];
        }
        else {
            gEeprom.S0_LEVEL = 130;
            gEeprom.S9_LEVEL = 76;
        }
    #endif

    // 0EA8..0EAF
    PY25Q16_ReadBuffer(0x00A0A8 + 0x18, Data, 8);
    #ifdef ENABLE_ALARM
        gEeprom.ALARM_MODE                 = (Data[0] <  2) ? Data[0] : true;
    #endif
    gEeprom.ROGER                          = (Data[1] <  3) ? Data[1] : ROGER_MODE_OFF;
    gEeprom.REPEATER_TAIL_TONE_ELIMINATION = (Data[2] < 11) ? Data[2] : 0;
    gEeprom.TX_VFO                         = (Data[3] <  2) ? Data[3] : 0;
    gEeprom.BATTERY_TYPE                   = (Data[4] < BATTERY_TYPE_UNKNOWN) ? Data[4] : BATTERY_TYPE_1600_MAH;

    // 0ED0..0ED7
    PY25Q16_ReadBuffer(0x00A0A8 + 0x40, Data, 8);
    gEeprom.DTMF_SIDE_TONE               = (Data[0] <   2) ? Data[0] : true;

#ifdef ENABLE_DTMF_CALLING
    gEeprom.DTMF_SEPARATE_CODE           = DTMF_ValidateCodes((char *)(Data + 1), 1) ? Data[1] : '*';
    gEeprom.DTMF_GROUP_CALL_CODE         = DTMF_ValidateCodes((char *)(Data + 2), 1) ? Data[2] : '#';
    gEeprom.DTMF_DECODE_RESPONSE         = (Data[3] <   4) ? Data[3] : 0;
    gEeprom.DTMF_auto_reset_time         = (Data[4] <  61) ? Data[4] : (Data[4] >= 5) ? Data[4] : 10;
#endif
    gEeprom.DTMF_PRELOAD_TIME            = (Data[5] < 101) ? Data[5] * 10 : 300;
    gEeprom.DTMF_FIRST_CODE_PERSIST_TIME = (Data[6] < 101) ? Data[6] * 10 : 100;
    gEeprom.DTMF_HASH_CODE_PERSIST_TIME  = (Data[7] < 101) ? Data[7] * 10 : 100;

    // 0ED8..0EDF
    PY25Q16_ReadBuffer(0x00A0A8 + 0x48, Data, 8);
    gEeprom.DTMF_CODE_PERSIST_TIME  = (Data[0] < 101) ? Data[0] * 10 : 100;
    gEeprom.DTMF_CODE_INTERVAL_TIME = (Data[1] < 101) ? Data[1] * 10 : 100;
#ifdef ENABLE_DTMF_CALLING
    gEeprom.PERMIT_REMOTE_KILL      = (Data[2] <   2) ? Data[2] : true;

    // 0EE0..0EE7

    PY25Q16_ReadBuffer(0x00A0F8, Data, sizeof(gEeprom.ANI_DTMF_ID));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.ANI_DTMF_ID))) {
        memcpy(gEeprom.ANI_DTMF_ID, Data, sizeof(gEeprom.ANI_DTMF_ID));
    } else {
        strcpy(gEeprom.ANI_DTMF_ID, "123");
    }


    // 0EE8..0EEF
    PY25Q16_ReadBuffer(0x00A0F8 + 0x8, Data, sizeof(gEeprom.KILL_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.KILL_CODE))) {
        memcpy(gEeprom.KILL_CODE, Data, sizeof(gEeprom.KILL_CODE));
    } else {
        strcpy(gEeprom.KILL_CODE, "ABCD9");
    }

    // 0EF0..0EF7
    PY25Q16_ReadBuffer(0x00A0F8 + 0x10, Data, sizeof(gEeprom.REVIVE_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.REVIVE_CODE))) {
        memcpy(gEeprom.REVIVE_CODE, Data, sizeof(gEeprom.REVIVE_CODE));
    } else {
        strcpy(gEeprom.REVIVE_CODE, "9DCBA");
    }
#endif

    // 0EF8..0F07
    PY25Q16_ReadBuffer(0x00A0F8 + 0x18, Data, sizeof(gEeprom.DTMF_UP_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.DTMF_UP_CODE))) {
        memcpy(gEeprom.DTMF_UP_CODE, Data, sizeof(gEeprom.DTMF_UP_CODE));
    } else {
        strcpy(gEeprom.DTMF_UP_CODE, "12345");
    }

    // 0F08..0F17
    PY25Q16_ReadBuffer(0x00A0F8 + 0x28, Data, sizeof(gEeprom.DTMF_DOWN_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.DTMF_DOWN_CODE))) {
        memcpy(gEeprom.DTMF_DOWN_CODE, Data, sizeof(gEeprom.DTMF_DOWN_CODE));
    } else {
        strcpy(gEeprom.DTMF_DOWN_CODE, "54321");
    }

    // 0F18..0F1F
    PY25Q16_ReadBuffer(0x00A130, Data, 8);

    gEeprom.SCAN_LIST_DEFAULT =
            (((Data[0] & 0x7F) >= 1) && ((Data[0] & 0x7F) <= (MR_CHANNELS_LIST + 1)))
                ? (Data[0] & 0x7F)
                : 1;
    gEeprom.SCAN_LIST_ENABLED = (Data[0] >> 7) & 0x01;

    gEeprom.SCANLIST_PRIORITY_CH[0] =
            (uint16_t)Data[1] |
            ((uint16_t)Data[2] << 8);

    gEeprom.SCANLIST_PRIORITY_CH[1] =
            (uint16_t)Data[3] |
            ((uint16_t)Data[4] << 8);

    gEeprom.CHAN_1_CALL =
            (uint16_t)Data[5] |
            ((uint16_t)Data[6] << 8);

    // 0F40..0F47
    PY25Q16_ReadBuffer(0x00A150, Data, 8);
    gSetting_F_LOCK            = (Data[0] < F_LOCK_LEN) ? Data[0] : F_LOCK_DEF;
#ifndef ENABLE_FEAT_F4HWN
    gSetting_350TX             = (Data[1] < 2) ? Data[1] : false;  // was true
#endif
#ifdef ENABLE_DTMF_CALLING
    gSetting_KILLED            = (Data[2] < 2) ? Data[2] : false;
#endif
#ifndef ENABLE_FEAT_F4HWN
    gSetting_200TX             = (Data[3] < 2) ? Data[3] : false;
    gSetting_500TX             = (Data[4] < 2) ? Data[4] : false;
#endif
    gSetting_350EN             = (Data[5] < 2) ? Data[5] : true;
#ifdef ENABLE_FEAT_F4HWN
    gSetting_ScrambleEnable    = false;
#else
    gSetting_ScrambleEnable    = (Data[6] < 2) ? Data[6] : true;
#endif

    //gSetting_TX_EN             = (Data[7] & (1u << 0)) ? true : false;
    gSetting_live_DTMF_decoder = !!(Data[7] & (1u << 1));
    gSetting_battery_text      = (((Data[7] >> 2) & 3u) <= 2) ? (Data[7] >> 2) & 3 : 2;
    #ifdef ENABLE_AUDIO_BAR
        gSetting_mic_bar       = !!(Data[7] & (1u << 4));
    #endif
    #ifndef ENABLE_FEAT_F4HWN
        #ifdef ENABLE_AM_FIX
            gSetting_AM_fix        = !!(Data[7] & (1u << 5));
        #endif
    #endif
    gSetting_backlight_on_tx_rx = (Data[7] >> 6) & 3u;

    if (!gEeprom.VFO_OPEN)
    {
        gEeprom.ScreenChannel[0] = gEeprom.MrChannel[0];
        gEeprom.ScreenChannel[1] = gEeprom.MrChannel[1];
    }

    // 0D60..0E27
    /*
    PY25Q16_ReadBuffer(0x008000, gMR_ChannelAttributes, sizeof(gMR_ChannelAttributes));
    uint16_t count = ARRAY_SIZE(gMR_ChannelAttributes);

    for (uint16_t i = 0; i < count; i++) {
        ChannelAttributes_t *att = MR_GetChannelAttributes(i);

        if (att->__val == 0xFFFF) {
            att->__val = 0;
            att->band = 0x7;
        }
        else
        {
            att->exclude = 0;
        }
    }
    */

    // Init attr cache
    MR_InitChannelAttributesCache();

    // Load and check channel
    for (uint16_t i = 0; i < MR_CHANNELS_MAX + 7; i++) {
        ChannelAttributes_t *att = MR_GetChannelAttributes(i);
        
        if (att != NULL) {
            if (att->__val == 0xFFFF) {
                att->__val = 0;
                att->band = 0x7;
                MR_SetChannelAttributes(i, att);  // ⭐ IMPORTANT: Sauvegarder!
            }
            else {
                att->exclude = 0;
                MR_SetChannelAttributes(i, att);  // ⭐ IMPORTANT: Sauvegarder!
            }
        }
    }

    // 0F30..0F3F
    PY25Q16_ReadBuffer(0x00A138, gCustomAesKey, sizeof(gCustomAesKey));
    bHasCustomAesKey = false;
    #ifndef ENABLE_FEAT_F4HWN
        for (unsigned int i = 0; i < ARRAY_SIZE(gCustomAesKey); i++)
        {
            if (gCustomAesKey[i] != 0xFFFFFFFFu)
            {
                bHasCustomAesKey = true;
                return;
            }
        }
    #endif

    #ifdef ENABLE_FEAT_F4HWN
        // 1FF0..0x1FF7
        // TODO: address TBD
        PY25Q16_ReadBuffer(0x00A158, Data, 8);
        gSetting_set_pwr = (((Data[7] & 0xF0) >> 4) < 7) ? ((Data[7] & 0xF0) >> 4) : 0;
        gSetting_set_ptt = (((Data[7] & 0x0F)) < 2) ? ((Data[7] & 0x0F)) : 0;

        gSetting_set_tot = (((Data[6] & 0xF0) >> 4) < 4) ? ((Data[6] & 0xF0) >> 4) : 0;
        gSetting_set_eot = (((Data[6] & 0x0F)) < 4) ? ((Data[6] & 0x0F)) : 0;

        /*
        int tmp = ((Data[5] & 0xF0) >> 4);

        gSetting_set_inv = (((tmp >> 0) & 0x01) < 2) ? ((tmp >> 0) & 0x01): 0;
        gSetting_set_lck = (((tmp >> 1) & 0x01) < 2) ? ((tmp >> 1) & 0x01): 0;
        gSetting_set_met = (((tmp >> 2) & 0x01) < 2) ? ((tmp >> 2) & 0x01): 0;
        gSetting_set_gui = (((tmp >> 3) & 0x01) < 2) ? ((tmp >> 3) & 0x01): 0;
        gSetting_set_ctr = (((Data[5] & 0x0F)) > 00 && ((Data[5] & 0x0F)) < 16) ? ((Data[5] & 0x0F)) : 10;

        gSetting_set_tmr = ((Data[4] & 1) < 2) ? (Data[4] & 1): 0;
        */

        int tmp = (Data[5] & 0xF0) >> 4;

#ifdef ENABLE_FEAT_F4HWN_INV
        gSetting_set_inv = (tmp >> 0) & 0x01;
#else
        gSetting_set_inv = 0;
#endif
        gSetting_set_lck = (tmp >> 1) & 0x01;
        gSetting_set_met = (tmp >> 2) & 0x01;
        gSetting_set_gui = (tmp >> 3) & 0x01;

#ifdef ENABLE_FEAT_F4HWN_CTR
        int ctr_value = Data[5] & 0x0F;
        gSetting_set_ctr = (ctr_value > 0 && ctr_value < 16) ? ctr_value : 10;
#else
        gSetting_set_ctr = 10;
#endif

        gSetting_set_tmr = Data[4] & 0x01;
#ifdef ENABLE_FEAT_F4HWN_SLEEP
        gSetting_set_off = (Data[4] >> 1) > 120 ? 60 : (Data[4] >> 1); 
#endif

        // Warning
        // Be aware, Data[3] is use by Spectrum
        // Warning

        // And set special session settings for actions
        gSetting_set_ptt_session = gSetting_set_ptt;
        gEeprom.KEY_LOCK_PTT = gSetting_set_lck;
    #endif
}

void SETTINGS_LoadCalibration(void)
{
//  uint8_t Mic;

    // 0x1EC0
    PY25Q16_ReadBuffer(0x010000 + 0xc0, gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[4], gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[5], gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[6], gEEPROM_RSSI_CALIB[3], 8);

    // 0x1EC8
    PY25Q16_ReadBuffer(0x010000 + 0xc8, gEEPROM_RSSI_CALIB[0], 8);
    memcpy(gEEPROM_RSSI_CALIB[1], gEEPROM_RSSI_CALIB[0], 8);
    memcpy(gEEPROM_RSSI_CALIB[2], gEEPROM_RSSI_CALIB[0], 8);

    // 0x1F40
    PY25Q16_ReadBuffer(0x010000 + 0x140, gBatteryCalibration, 12);
    if (gBatteryCalibration[0] >= 5000)
    {
        gBatteryCalibration[0] = 1900;
        gBatteryCalibration[1] = 2000;
    }
    gBatteryCalibration[5] = 2300;

    #ifdef ENABLE_VOX
        // 0x1F50
        PY25Q16_ReadBuffer(0x010000 + 0x150 + (gEeprom.VOX_LEVEL * 2), &gEeprom.VOX1_THRESHOLD, 2);
        // 0x1F68
        PY25Q16_ReadBuffer(0x010000 + 0x168 + (gEeprom.VOX_LEVEL * 2), &gEeprom.VOX0_THRESHOLD, 2);
    #endif

    //PY25Q16_ReadBuffer(0x1F80 + gEeprom.MIC_SENSITIVITY, &Mic, 1);
    //gEeprom.MIC_SENSITIVITY_TUNING = (Mic < 32) ? Mic : 15;
    gEeprom.MIC_SENSITIVITY_TUNING = gMicGain_dB2[gEeprom.MIC_SENSITIVITY];

    {
        struct
        {
            int16_t  BK4819_XtalFreqLow;
            uint16_t EEPROM_1F8A;
            uint16_t EEPROM_1F8C;
            uint8_t  VOLUME_GAIN;
            uint8_t  DAC_GAIN;
        } __attribute__((packed)) Misc;

        // radio 1 .. 04 00 46 00 50 00 2C 0E
        // radio 2 .. 05 00 46 00 50 00 2C 0E
        // 0x1F88
        PY25Q16_ReadBuffer(0x010000 + 0x188, &Misc, 8);

        gEeprom.BK4819_XTAL_FREQ_LOW = (Misc.BK4819_XtalFreqLow >= -1000 && Misc.BK4819_XtalFreqLow <= 1000) ? Misc.BK4819_XtalFreqLow : 0;
        gEEPROM_1F8A                 = Misc.EEPROM_1F8A & 0x01FF;
        gEEPROM_1F8C                 = Misc.EEPROM_1F8C & 0x01FF;
        gEeprom.VOLUME_GAIN          = (Misc.VOLUME_GAIN < 64) ? Misc.VOLUME_GAIN : 58;
        gEeprom.DAC_GAIN             = (Misc.DAC_GAIN    < 16) ? Misc.DAC_GAIN    : 8;

        #ifdef ENABLE_FEAT_F4HWN
            gEeprom.VOLUME_GAIN_BACKUP   = gEeprom.VOLUME_GAIN;
        #endif

        BK4819_WriteRegister(BK4819_REG_3B, 22656 + gEeprom.BK4819_XTAL_FREQ_LOW);
//      BK4819_WriteRegister(BK4819_REG_3C, gEeprom.BK4819_XTAL_FREQ_HIGH);
    }
}

uint32_t SETTINGS_FetchChannelFrequency(const uint16_t channel)
{
    struct
    {
        uint32_t frequency;
        uint32_t offset;
    } __attribute__((packed)) info;

    PY25Q16_ReadBuffer(channel * 16, &info, sizeof(info));

    return info.frequency;
}

void SETTINGS_FetchChannelName(char *s, const uint16_t channel)
{
    if (s == NULL)
        return;

    s[0] = 0;

    if (channel < 0)
        return;

    if (!RADIO_CheckValidChannel(channel, false, 0))
        return;

    // 0x0F50
    PY25Q16_ReadBuffer(0x004000 + (channel * 16), s, 10);

    int i;
    for (i = 0; i < 10; i++)
        if (s[i] < 32 || s[i] > 127)
            break;                // invalid char

    s[i--] = 0;                   // null term

    while (i >= 0 && s[i] == 32)  // trim trailing spaces
        s[i--] = 0;               // null term
}

void SETTINGS_FactoryReset(bool bIsAll)
{
    PY25Q16_SectorErase(0x000000);
    PY25Q16_SectorErase(0x001000);
    PY25Q16_SectorErase(0x003000);
    PY25Q16_SectorErase(0x004000);
    PY25Q16_SectorErase(0x005000);
    PY25Q16_SectorErase(0x006000);
    PY25Q16_SectorErase(0x007000);
    PY25Q16_SectorErase(0x008000);
    PY25Q16_SectorErase(0x009000);
    
    // 0d60 - 0e30
    if (bIsAll)
    {
        PY25Q16_SectorErase(0x00A000);
    }

    // Prevent reset to restart in RO mode...
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        // Bloc 0x0E70..0x0E7F -> offset 0x00A000
        uint8_t Data8[0x10];
        PY25Q16_ReadBuffer(0x00A000, Data8, sizeof(Data8));

        // MENU_LOCK & KEY_LOCK to 0

        Data8[4] &= (uint8_t)~0x01;
        Data8[4] &= (uint8_t)~0x02;

        // SET_KEY to 0
        Data8[4] &= (uint8_t)~0x3C;  // Clear bits 2-5 (SET_KEY)

        // SET_NAV to false
        Data8[4] &= (uint8_t)~0x40;  // Clear bit 6 (SET_NAV) for UV-K1 by default

        #ifdef ENABLE_FEAT_F4HWN_RESET_VFO
            Data8[7] = (1 & 0x01);
        #endif

        PY25Q16_WriteBuffer(0x00A000, Data8, sizeof(Data8), false);

        // cohérence RAM
        gEeprom.MENU_LOCK = 0;
    #endif

    // Reset VFO for the first time...
    #ifdef ENABLE_FEAT_F4HWN_RESET_VFO
        RADIO_InitInfo(&gEeprom.VfoInfo[0], FREQ_CHANNEL_FIRST + BAND3_137MHz, 14550000);
        RADIO_InitInfo(&gEeprom.VfoInfo[1], FREQ_CHANNEL_FIRST + BAND6_400MHz, 43350000);

        gEeprom.ScreenChannel[0] = FREQ_CHANNEL_FIRST + BAND3_137MHz;
        gEeprom.ScreenChannel[1] = FREQ_CHANNEL_FIRST + BAND6_400MHz;
        gEeprom.MrChannel[0]     = MR_CHANNEL_FIRST;
        gEeprom.MrChannel[1]     = MR_CHANNEL_FIRST;
        gEeprom.FreqChannel[0]   = FREQ_CHANNEL_FIRST + BAND3_137MHz;
        gEeprom.FreqChannel[1]   = FREQ_CHANNEL_FIRST + BAND6_400MHz;
        
        SETTINGS_SaveChannel(FREQ_CHANNEL_FIRST + BAND3_137MHz, 0, &gEeprom.VfoInfo[0], 2);
        SETTINGS_SaveChannel(FREQ_CHANNEL_FIRST + BAND6_400MHz, 1, &gEeprom.VfoInfo[1], 2);

        gVfoStateChanged = true;
        gScheduleVfoSave = true;
        SETTINGS_SaveVfoIndicesFlush();
    #endif
}

#ifdef ENABLE_FMRADIO
void SETTINGS_SaveFM(void)
    {
        union {
            struct {
                uint16_t selFreq;
                uint8_t  selChn;
                uint8_t  isMrMode:1;
                uint8_t  band:2;
                //uint8_t  space:2;
            };
            uint8_t __raw[8];
        } __attribute__((packed)) fmCfg;

        memset(fmCfg.__raw, 0xFF, sizeof(fmCfg.__raw));
        fmCfg.selChn   = gEeprom.FM_SelectedChannel;
        fmCfg.selFreq  = gEeprom.FM_SelectedFrequency;
        fmCfg.isMrMode = gEeprom.FM_IsMrMode;
        fmCfg.band     = gEeprom.FM_Band;
        // fmCfg.space    = gEeprom.FM_Space;
        // 0E88
        PY25Q16_WriteBuffer(0x00A020, fmCfg.__raw, 8, false);

        // 0E40
        PY25Q16_WriteBuffer(0x00A028, gFM_Channels, sizeof(gFM_Channels), false);
    }
#endif

void SETTINGS_SaveVfoIndices(void)
{
    gVfoStateChanged = true;
    gVfoSaveCountdown_10ms = 2;
}

void SETTINGS_SaveVfoIndicesFlush(void)
{
    if (gScheduleVfoSave) {
        gScheduleVfoSave = false;
        
        if (gVfoStateChanged) {
            gVfoStateChanged = false;
            uint16_t Data16[8];

            #ifndef ENABLE_NOAA
                PY25Q16_ReadBuffer(0x00A010, Data16, sizeof(Data16));
            #endif

            Data16[0] = gEeprom.ScreenChannel[0];
            Data16[1] = gEeprom.MrChannel[0];
            Data16[2] = gEeprom.FreqChannel[0];
            Data16[3] = gEeprom.ScreenChannel[1];
            Data16[4] = gEeprom.MrChannel[1];
            Data16[5] = gEeprom.FreqChannel[1];

        #ifdef ENABLE_NOAA
            Data16[6] = gEeprom.NoaaChannel[0];
            Data16[7] = gEeprom.NoaaChannel[1];
        #endif

            PY25Q16_WriteBuffer(0x00A010, Data16, sizeof(Data16), false);
        }
    }
}

void SETTINGS_SaveSettings(void)
{
    uint8_t *State;
    uint8_t tmp = 0;
    uint8_t SecBuf[0x50];

    // ----------------------
    // 0e70 - 0e80

    memset(SecBuf, 0xff, 0x10);

    // 0x0E70
    State = SecBuf;
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        State[0] = gSetting_set_audio;
    #endif
    State[1] = gEeprom.SQUELCH_LEVEL;
    State[2] = gEeprom.TX_TIMEOUT_TIMER;
    #ifdef ENABLE_NOAA
        State[3] = gEeprom.NOAA_AUTO_SCAN;
    #else
        State[3] = false;
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        State[4] =
            (gEeprom.KEY_LOCK        ? 0x01 : 0) |
            (gEeprom.MENU_LOCK       ? 0x02 : 0) |
            ((gEeprom.SET_KEY & 0x0F) << 2)      |
            (gEeprom.SET_NAV  ? 0x40 : 0);
    #else
        State[4] = gEeprom.KEY_LOCK;
    #endif

    #ifdef ENABLE_VOX
        State[5] = gEeprom.VOX_SWITCH;
        State[6] = gEeprom.VOX_LEVEL;
    #else
        State[5] = false;
        State[6] = 0;
    #endif
    State[7] = gEeprom.MIC_SENSITIVITY;

    // 0x0E78
    State = SecBuf + 0x8;
    State[0] = (gEeprom.BACKLIGHT_MIN << 4) + gEeprom.BACKLIGHT_MAX;
    State[1] = gEeprom.CHANNEL_DISPLAY_MODE;
    State[2] = gEeprom.CROSS_BAND_RX_TX;
    State[3] = gEeprom.BATTERY_SAVE;
    State[4] = gEeprom.DUAL_WATCH;

    #ifdef ENABLE_FEAT_F4HWN
        if(!gSaveRxMode)
        {
            State[2] = gCB;
            State[4] = gDW;
        }
        if(gBackLight)
        {
            State[5] = gBacklightTimeOriginal;
        }
        else
        {
            State[5] = gEeprom.BACKLIGHT_TIME;
        }
    #else
        State[5] = gEeprom.BACKLIGHT_TIME;
    #endif

    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        State[6] =
            (gEeprom.TAIL_TONE_ELIMINATION & 0x01) |
            ((gSetting_set_nfm & 0x01) << 1)
        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
          | ((gEeprom.VFO_OPEN & 0x01) << 2)
        #endif
    ;
    #else
        State[6] = gEeprom.TAIL_TONE_ELIMINATION;
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        State[7] = (gEeprom.CURRENT_STATE & 0x07) | ((gEeprom.SCAN_LIST_DEFAULT & 0x1F) << 3);
    #else
        State[7] = gEeprom.VFO_OPEN;
    #endif

    PY25Q16_WriteBuffer(0x00A000, SecBuf, 0x10, false);

    // -------------------------
    //  0e90 - 0ee0

    // memset(SecBuf, 0xff, 0x50);
    PY25Q16_ReadBuffer(0x00A0A8, SecBuf, 0x50);

    // 0x0E90
    State = SecBuf;
    State[0] = gEeprom.BEEP_CONTROL;
    State[0] |= gEeprom.KEY_M_LONG_PRESS_ACTION << 1;
    State[1] = gEeprom.KEY_1_SHORT_PRESS_ACTION;
    State[2] = gEeprom.KEY_1_LONG_PRESS_ACTION;
    State[3] = gEeprom.KEY_2_SHORT_PRESS_ACTION;
    State[4] = gEeprom.KEY_2_LONG_PRESS_ACTION;
    State[5] = gEeprom.SCAN_RESUME_MODE;
    State[6] = gEeprom.AUTO_KEYPAD_LOCK;
    State[7] = gEeprom.POWER_ON_DISPLAY_MODE;

    // 0x0E98
    #ifdef ENABLE_PWRON_PASSWORD
        State = SecBuf + 0x8;
        State[0] = gEeprom.POWER_ON_PASSWORD;
    #endif

    // 0x0EA0
    State = SecBuf + 0x10;
#ifdef ENABLE_VOICE
    State[0] = gEeprom.VOICE_PROMPT;
#endif
#ifdef ENABLE_RSSI_BAR
    State[1] = gEeprom.S0_LEVEL;
    State[2] = gEeprom.S9_LEVEL;
#endif

    // 0x0EA8
    State = SecBuf + 0x18;
    #if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
        State[0] = gEeprom.ALARM_MODE;
    #else
        State[0] = false;
    #endif
    State[1] = gEeprom.ROGER;
    State[2] = gEeprom.REPEATER_TAIL_TONE_ELIMINATION;
    State[3] = gEeprom.TX_VFO;
    State[4] = gEeprom.BATTERY_TYPE;

    // 0x0ED0
    State = SecBuf + 0x40;
    State[0] = gEeprom.DTMF_SIDE_TONE;
#ifdef ENABLE_DTMF_CALLING
    State[1] = gEeprom.DTMF_SEPARATE_CODE;
    State[2] = gEeprom.DTMF_GROUP_CALL_CODE;
    State[3] = gEeprom.DTMF_DECODE_RESPONSE;
    State[4] = gEeprom.DTMF_auto_reset_time;
#endif
    State[5] = gEeprom.DTMF_PRELOAD_TIME / 10U;
    State[6] = gEeprom.DTMF_FIRST_CODE_PERSIST_TIME / 10U;
    State[7] = gEeprom.DTMF_HASH_CODE_PERSIST_TIME / 10U;

    // 0x0ED8
    State = SecBuf + 0x48;
    State[0] = gEeprom.DTMF_CODE_PERSIST_TIME / 10U;
    State[1] = gEeprom.DTMF_CODE_INTERVAL_TIME / 10U;
#ifdef ENABLE_DTMF_CALLING
    State[2] = gEeprom.PERMIT_REMOTE_KILL;
#endif

    PY25Q16_WriteBuffer(0x00A0A8, SecBuf, 0x50, false);

    // -------------------------
    // 0f18 - 0f20

    memset(SecBuf, 0xff, 0x08);

    // 0x0F18
    State = SecBuf;

    State[0] = (gEeprom.SCAN_LIST_DEFAULT & 0x7F)
        | ((gEeprom.SCAN_LIST_ENABLED & 0x01) << 7);

    State[1] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[0] & 0xFF);
    State[2] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[0] >> 8);

    State[3] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[1] & 0xFF);
    State[4] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[1] >> 8);

    State[5] = (uint8_t)(gEeprom.CHAN_1_CALL & 0xFF);
    State[6] = (uint8_t)(gEeprom.CHAN_1_CALL >> 8);

    PY25Q16_WriteBuffer(0x00A130, SecBuf, 0x08, false);

    // ---------------------
    // 0f40 - 0f48

    memset(SecBuf, 0xff, 8);

    // 0x0F40
    State = SecBuf;
    State[0]  = gSetting_F_LOCK;
#ifndef ENABLE_FEAT_F4HWN
    State[1]  = gSetting_350TX;
#endif
#ifdef ENABLE_DTMF_CALLING
    State[2]  = gSetting_KILLED;
#endif
#ifndef ENABLE_FEAT_F4HWN
    State[3]  = gSetting_200TX;
    State[4]  = gSetting_500TX;
#endif
    State[5]  = gSetting_350EN;
#ifdef ENABLE_FEAT_F4HWN
    State[6]  = false;
#else
    State[6]  = gSetting_ScrambleEnable;
#endif

    //if (!gSetting_TX_EN)             State[7] &= ~(1u << 0);
    if (!gSetting_live_DTMF_decoder) State[7] &= ~(1u << 1);
    State[7] = (State[7] & ~(3u << 2)) | ((gSetting_battery_text & 3u) << 2);
    #ifdef ENABLE_AUDIO_BAR
        if (!gSetting_mic_bar)           State[7] &= ~(1u << 4);
    #endif
    #ifndef ENABLE_FEAT_F4HWN
        #ifdef ENABLE_AM_FIX
            if (!gSetting_AM_fix)            State[7] &= ~(1u << 5);
        #endif
    #endif
    State[7] = (State[7] & ~(3u << 6)) | ((gSetting_backlight_on_tx_rx & 3u) << 6);

    PY25Q16_WriteBuffer(0x00A150, SecBuf, 8, false);

    // ------------------

#ifdef ENABLE_FEAT_F4HWN
    // 0x1FF0
    State = SecBuf;
    // TODO: TBD
    PY25Q16_ReadBuffer(0x00A158, State, 8);

    //memset(State, 0xFF, sizeof(State));

    /*
    tmp = 0;

    if(gSetting_set_tmr == 1)
        tmp = tmp | (1 << 0);

    State[4] = tmp;

    tmp = 0;

    if(gSetting_set_inv == 1)
        tmp = tmp | (1 << 0);
    if (gSetting_set_lck == 1)
        tmp = tmp | (1 << 1);
    if (gSetting_set_met == 1)
        tmp = tmp | (1 << 2);
    if (gSetting_set_gui == 1)
        tmp = tmp | (1 << 3);
    */

#ifdef ENABLE_FEAT_F4HWN_SLEEP 
    State[4] = (gSetting_set_off << 1) | (gSetting_set_tmr & 0x01);
#else
    State[4] = gSetting_set_tmr ? (1 << 0) : 0;
#endif

    tmp =   (gSetting_set_inv << 0) |
            (gSetting_set_lck << 1) |
            (gSetting_set_met << 2) |
            (gSetting_set_gui << 3);

    State[5] = ((tmp << 4) | (gSetting_set_ctr & 0x0F));
    State[6] = ((gSetting_set_tot << 4) | (gSetting_set_eot & 0x0F));
    State[7] = ((gSetting_set_pwr << 4) | (gSetting_set_ptt & 0x0F));

    gEeprom.KEY_LOCK_PTT = gSetting_set_lck;

    PY25Q16_WriteBuffer(0x00A158, SecBuf, 8, false);
#endif

#ifdef ENABLE_FEAT_F4HWN_VOL
    SETTINGS_WriteCurrentVol();
#endif
}

void SETTINGS_SaveChannel(uint16_t Channel, uint8_t VFO, const VFO_Info_t *pVFO, uint8_t Mode)
{
#ifdef ENABLE_NOAA
    if (IS_NOAA_CHANNEL(Channel))
        return;
#endif

    // 0
    uint16_t OffsetVFO = 0 + Channel * 16;

    if (IS_FREQ_CHANNEL(Channel)) { // it's a VFO, not a channel
        // 0x0C80
        OffsetVFO  = (VFO == 0) ? 0x009000 : 0x009010;
        OffsetVFO += (Channel - FREQ_CHANNEL_FIRST) * 32;
    }

    if (Mode >= 2 || IS_FREQ_CHANNEL(Channel)) { // copy VFO to a channel
        typedef union {
            uint8_t _8[8];
            uint32_t _32[2];
        } State_t;
        
        State_t *State;

        uint8_t Buf[0x10];

        State = (State_t *)Buf;
        State -> _32[0] = pVFO->freq_config_RX.Frequency;
        State -> _32[1] = pVFO->TX_OFFSET_FREQUENCY;

        State = (State_t *)(Buf + 0x8);
        State -> _8[0] =  pVFO->freq_config_RX.Code;
        State -> _8[1] =  pVFO->freq_config_TX.Code;
        State -> _8[2] = (pVFO->freq_config_TX.CodeType << 4) | pVFO->freq_config_RX.CodeType;
        State -> _8[3] = (pVFO->Modulation << 4) | pVFO->TX_OFFSET_FREQUENCY_DIRECTION;
        State -> _8[4] = 0
            | (pVFO->TX_LOCK << 6)
            | (pVFO->BUSY_CHANNEL_LOCK << 5)
            | (pVFO->OUTPUT_POWER      << 2)
            | (pVFO->CHANNEL_BANDWIDTH << 1)
            | (pVFO->FrequencyReverse  << 0);
        State -> _8[5] = ((pVFO->DTMF_PTT_ID_TX_MODE & 7u) << 1)
#ifdef ENABLE_DTMF_CALLING
            | ((pVFO->DTMF_DECODING_ENABLE & 1u) << 0)
#endif
        ;
        State -> _8[6] =  pVFO->STEP_SETTING;
#ifdef ENABLE_FEAT_F4HWN
        State -> _8[7] =  0;
#else
        State -> _8[7] =  pVFO->SCRAMBLING_TYPE;
#endif

        PY25Q16_WriteBuffer(OffsetVFO, Buf, 0x10, false);

        SETTINGS_UpdateChannel(Channel, pVFO, true, true, true);

        if (IS_MR_CHANNEL(Channel)) {
#ifndef ENABLE_KEEP_MEM_NAME
            // clear/reset the channel name
            SETTINGS_SaveChannelName(Channel, "");
#else
            if (Mode >= 3) {
                SETTINGS_SaveChannelName(Channel, pVFO->Name);
            }
#endif
        }
    }

}

void SETTINGS_SaveBatteryCalibration(const uint16_t * batteryCalibration)
{
    // 0x1F40
    PY25Q16_WriteBuffer(0x010000 + 0x140, batteryCalibration, 12, false);
}

void SETTINGS_SaveChannelName(uint16_t channel, const char * name)
{
    uint16_t offset = channel * 16;
    uint8_t buf[16] = {0};
    memcpy(buf, name, MIN(strlen(name), 10u));
    // 0x0F50
    PY25Q16_WriteBuffer(0x004000 + offset, buf, 0x10, false);
}

void SETTINGS_UpdateChannel(uint16_t channel, const VFO_Info_t *pVFO, bool keep, bool check, bool save)
{
#ifdef ENABLE_NOAA
    if (!IS_NOAA_CHANNEL(channel))
#endif
    {
        ChannelAttributes_t  state;
        ChannelAttributes_t  att = {
            .band = 0x7,
            .compander = 0,
            .unused_1 = 0,
            .unused_2 = 0,
            .exclude = 0,
            .scanlist = 0,
            };        // default attributes

        // 0x0D60
        PY25Q16_ReadBuffer(0x008000 + channel, &state, 1);

        if (keep) {
            att.band = pVFO->Band;
            att.compander = pVFO->Compander;
            att.unused_1 = 0;
            att.unused_2 = 0;
            att.exclude = 0;
            att.scanlist = pVFO->SCANLIST_PARTICIPATION;
            if (check && state.__val == att.__val)
                return; // no change in the attributes
        }

        state.__val = att.__val;

#ifndef ENABLE_FEAT_F4HWN
        save = true;
#endif
        if(save)
        {
            uint16_t buf[MR_CHANNELS_MAX + 24];
            PY25Q16_ReadBuffer(0x008000, buf, sizeof(buf));
            buf[channel] = state.__val;
            PY25Q16_WriteBuffer(0x008000, buf, sizeof(buf), false);
        }

        MR_SetChannelAttributes(channel, &att);

        if (IS_MR_CHANNEL(channel)) {   // it's a memory channel
            if (!keep) {
                // clear/reset the channel name
                SETTINGS_SaveChannelName(channel, "");
            }
        }
    }
}

void SETTINGS_WriteBuildOptions(void)
{
    uint8_t State[8];

#ifdef ENABLE_FEAT_F4HWN
    // 0x1FF0
    PY25Q16_ReadBuffer(0x00A158, State, sizeof(State));
#endif
    
State[0] = 0
#ifdef ENABLE_FMRADIO
    | (1 << 0)
#endif
#ifdef ENABLE_NOAA
    | (1 << 1)
#endif
#ifdef ENABLE_VOICE
    | (1 << 2)
#endif
#ifdef ENABLE_VOX
    | (1 << 3)
#endif
#ifdef ENABLE_ALARM
    | (1 << 4)
#endif
#ifdef ENABLE_TX1750
    | (1 << 5)
#endif
#ifdef ENABLE_PWRON_PASSWORD
    | (1 << 6)
#endif
#ifdef ENABLE_DTMF_CALLING
    | (1 << 7)
#endif
;

State[1] = 0
#ifdef ENABLE_FLASHLIGHT
    | (1 << 0)
#endif
#ifdef ENABLE_WIDE_RX
    | (1 << 1)
#endif
#ifdef ENABLE_BYP_RAW_DEMODULATORS
    | (1 << 2)
#endif
#ifdef ENABLE_FEAT_F4HWN_GAME
    | (1 << 3)
#endif
#ifdef ENABLE_AM_FIX
    | (1 << 4)
#endif
#ifdef ENABLE_SPECTRUM
    | (1 << 5)
#endif
#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    | (1 << 6)
#endif
;
    PY25Q16_WriteBuffer(0x00A158, State, sizeof(State), false);
}

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    void SETTINGS_WriteCurrentState(void)
    {
        uint8_t State[0x08];

        PY25Q16_ReadBuffer(0x00A008, State, sizeof(State));
        State[7] =
            (gEeprom.CURRENT_STATE & 0x07) |
            ((gEeprom.SCAN_LIST_DEFAULT & 0x1F) << 3);
        PY25Q16_WriteBuffer(0x00A008, State, sizeof(State), false);

        //

        PY25Q16_ReadBuffer(0x00A130, State, sizeof(State));

        State[0] = (gEeprom.SCAN_LIST_DEFAULT & 0x7F)
            | ((gEeprom.SCAN_LIST_ENABLED & 0x01) << 7);

        State[1] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[0] & 0xFF);
        State[2] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[0] >> 8);

        State[3] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[1] & 0xFF);
        State[4] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[1] >> 8);

        State[5] = (uint8_t)(gEeprom.CHAN_1_CALL & 0xFF);
        State[6] = (uint8_t)(gEeprom.CHAN_1_CALL >> 8);

        PY25Q16_WriteBuffer(0x00A130, State, sizeof(State), false);
    }
#endif

#ifdef ENABLE_FEAT_F4HWN_VOL
    void SETTINGS_WriteCurrentVol(void)
    {
        uint8_t State[8];
        // 0x1F88
        PY25Q16_ReadBuffer(0x010000 + 0x188, State, sizeof(State));
        State[6] = gEeprom.VOLUME_GAIN;
        PY25Q16_WriteBuffer(0x010000 + 0x188, State, sizeof(State), false);
    }
#endif

#ifdef ENABLE_FEAT_F4HWN

void SETTINGS_ResetTxLock(void)
{
    // This is an expensive operation: full scan of all MR channels

    #define CHANNEL_SIZE               16
    #define TXLOCK_BYTE_OFFSET         12
    #define TXLOCK_BIT                 6
    #define SETTINGS_ResetTxLock_BATCH 32

    const uint32_t TotalBytes  = MR_CHANNELS_MAX * CHANNEL_SIZE;   // 1024 * 16 = 16 384
    const uint32_t BatchSize   = TotalBytes / SETTINGS_ResetTxLock_BATCH; // 16 384 / 32 = 512
    const uint32_t BatchChCnt  = BatchSize / CHANNEL_SIZE;         // 32 channels per batch

    uint8_t Buf[BatchSize];

    for (uint32_t batch = 0; batch < SETTINGS_ResetTxLock_BATCH; batch++)
    {
        uint32_t Offset = batch * BatchSize;

        PY25Q16_ReadBuffer(Offset, Buf, BatchSize);

        for (uint32_t ch = 0; ch < BatchChCnt; ch++)
        {
            uint32_t off = ch * CHANNEL_SIZE;
            Buf[off + TXLOCK_BYTE_OFFSET] |= (1 << TXLOCK_BIT);
        }

        PY25Q16_WriteBuffer(Offset, Buf, BatchSize, false);
    }

    RADIO_ConfigureChannel(0, VFO_CONFIGURE_RELOAD);
    RADIO_ConfigureChannel(1, VFO_CONFIGURE_RELOAD);

    #undef SETTINGS_ResetTxLock_BATCH
    #undef CHANNEL_SIZE
    #undef TXLOCK_BYTE_OFFSET
    #undef TXLOCK_BIT
}

#endif
