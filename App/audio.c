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

#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "audio.h"
#ifdef ENABLE_FMRADIO
    #include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "driver/systick.h"
#include "driver/voice.h"
#include "driver/py25q16.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/ui.h"

static const uint16_t BEEP_Classic_array[][3] = { /* Tone    Duration    Repeats  */                                     
    [BEEP_NONE]                                   = {0,      0,          0      },
    [BEEP_1KHZ_60MS_OPTIONAL]                     = {1000,   60,         1      },
    [BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL]        = {500,    60,         2      },
#ifdef ENABLE_DTMF_CALLING
    [BEEP_880HZ_200MS]                            = {880,    200,        1      },
    [BEEP_880HZ_500MS]                            = {880,    500,        1      },
#endif
    [BEEP_500HZ_60MS_DOUBLE_BEEP]                 = {500,    60,         2      },
#ifdef ENABLE_FEAT_F4HWN
    [BEEP_400HZ_30MS]                             = {400,    30,         1      },
    [BEEP_500HZ_30MS]                             = {500,    30,         1      },
    [BEEP_600HZ_30MS]                             = {600,    30,         1      },
#endif
    [BEEP_880HZ_60MS_TRIPLE_BEEP]                 = {880,    60,         3      }
};

BEEP_Type_t gBeepToPlay = BEEP_NONE;

void AUDIO_PlayBeep(BEEP_Type_t Beep)
{
    if (Beep == BEEP_NONE)
        return;

    if ((Beep == BEEP_1KHZ_60MS_OPTIONAL ||
         Beep == BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL) &&
         !gEeprom.BEEP_CONTROL)
        return;
        
    if (gCurrentFunction == FUNCTION_RECEIVE)
        return;

    if (gCurrentFunction == FUNCTION_MONITOR)
        return;

    if (Beep >= ARRAY_SIZE(BEEP_Classic_array))
        return;

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        BK1080_Mute(true);
#endif

    AUDIO_AudioPathOff();

    if (gCurrentFunction == FUNCTION_POWER_SAVE && gRxIdleMode)
        BK4819_RX_TurnOn();

    SYSTEM_DelayMs(20);

    uint16_t ToneConfig = BK4819_ReadRegister(BK4819_REG_71);

#ifdef ENABLE_FEAT_F4HWN
    if(Beep == BEEP_400HZ_30MS || Beep == BEEP_500HZ_30MS || Beep == BEEP_600HZ_30MS)
    {
        BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_ENABLE_TONE1 | ((1 & 0x7f) << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
    }
#endif

    BK4819_PrepareToPlayTone(true);

    SYSTEM_DelayMs(2);

    AUDIO_AudioPathOn();

    SYSTEM_DelayMs(60);

    for (uint8_t i = 0; i < BEEP_Classic_array[Beep][BEEP_REPEATS]; i++) {
        BK4819_PlayToneRaw( BEEP_Classic_array[Beep][BEEP_TONE], 
                            BEEP_Classic_array[Beep][BEEP_DURATION]);
        SYSTEM_DelayMs(20);
    }

    AUDIO_AudioPathOff();

    SYSTEM_DelayMs(5);
    BK4819_TurnsOffTones_TurnsOnRX();
    SYSTEM_DelayMs(5);
    BK4819_WriteRegister(BK4819_REG_71, ToneConfig);

#ifdef ENABLE_FMRADIO
    const bool isFmRadio = gFmRadioMode;
    
    if (isFmRadio)
        SYSTEM_DelayMs(10);
#endif

    if (gEnableSpeaker)
        AUDIO_AudioPathOn();

#ifdef ENABLE_FMRADIO
    if (isFmRadio)
        BK1080_Mute(false);
#endif

    if (gCurrentFunction == FUNCTION_POWER_SAVE && gRxIdleMode)
        BK4819_Sleep();

#ifdef ENABLE_VOX
    gVoxResumeCountdown = 80;
#endif
}

#ifdef ENABLE_VOICE

uint16_t gVoiceBuf[VOICE_BUF_CAP][VOICE_BUF_LEN];
uint8_t gVoiceBufReadIndex = 0;
uint8_t gVoiceBufWriteIndex = 0;
uint8_t gVoiceBufLen = 0;

VOICE_ID_t        gVoiceID[8];
uint8_t           gVoiceReadIndex;
uint8_t           gVoiceWriteIndex;
volatile uint16_t gCountdownToPlayNextVoice_10ms;
volatile bool     gFlagPlayQueuedVoice;
VOICE_ID_t        gAnotherVoiceID = VOICE_ID_INVALID;

static const uint16_t VOICE_SAMPLES[256] = 
{
    0x06a8, 0x06b8, 0x0688, 0x0698, 0x06e8, 0x06f8, 0x06c8, 0x06d8, //
    0x0628, 0x0638, 0x0608, 0x0618, 0x0668, 0x0678, 0x0648, 0x0658, //
    0x0754, 0x075c, 0x0744, 0x074c, 0x0774, 0x077c, 0x0764, 0x076c, //
    0x0714, 0x071c, 0x0704, 0x070c, 0x0734, 0x073c, 0x0724, 0x072c, //
    0x02a0, 0x02e0, 0x0220, 0x0260, 0x03a0, 0x03e0, 0x0320, 0x0360, //
    0x00a0, 0x00e0, 0x0020, 0x0060, 0x01a0, 0x01e0, 0x0120, 0x0160, //
    0x0550, 0x0570, 0x0510, 0x0530, 0x05d0, 0x05f0, 0x0590, 0x05b0, //
    0x0450, 0x0470, 0x0410, 0x0430, 0x04d0, 0x04f0, 0x0490, 0x04b0, //
    0x07ea, 0x07eb, 0x07e8, 0x07e9, 0x07ee, 0x07ef, 0x07ec, 0x07ed, //
    0x07e2, 0x07e3, 0x07e0, 0x07e1, 0x07e6, 0x07e7, 0x07e4, 0x07e5, //
    0x07fa, 0x07fb, 0x07f8, 0x07f9, 0x07fe, 0x07ff, 0x07fc, 0x07fd, //
    0x07f2, 0x07f3, 0x07f0, 0x07f1, 0x07f6, 0x07f7, 0x07f4, 0x07f5, //
    0x07aa, 0x07ae, 0x07a2, 0x07a6, 0x07ba, 0x07be, 0x07b2, 0x07b6, //
    0x078a, 0x078e, 0x0782, 0x0786, 0x079a, 0x079e, 0x0792, 0x0796, //
    0x07d5, 0x07d7, 0x07d1, 0x07d3, 0x07dd, 0x07df, 0x07d9, 0x07db, //
    0x07c5, 0x07c7, 0x07c1, 0x07c3, 0x07cd, 0x07cf, 0x07c9, 0x07cb, //
    0x0958, 0x0948, 0x0978, 0x0968, 0x0918, 0x0908, 0x0938, 0x0928, //
    0x09d8, 0x09c8, 0x09f8, 0x09e8, 0x0998, 0x0988, 0x09b8, 0x09a8, //
    0x08ac, 0x08a4, 0x08bc, 0x08b4, 0x088c, 0x0884, 0x089c, 0x0894, //
    0x08ec, 0x08e4, 0x08fc, 0x08f4, 0x08cc, 0x08c4, 0x08dc, 0x08d4, //
    0x0d60, 0x0d20, 0x0de0, 0x0da0, 0x0c60, 0x0c20, 0x0ce0, 0x0ca0, //
    0x0f60, 0x0f20, 0x0fe0, 0x0fa0, 0x0e60, 0x0e20, 0x0ee0, 0x0ea0, //
    0x0ab0, 0x0a90, 0x0af0, 0x0ad0, 0x0a30, 0x0a10, 0x0a70, 0x0a50, //
    0x0bb0, 0x0b90, 0x0bf0, 0x0bd0, 0x0b30, 0x0b10, 0x0b70, 0x0b50, //
    0x0815, 0x0814, 0x0817, 0x0816, 0x0811, 0x0810, 0x0813, 0x0812, //
    0x081d, 0x081c, 0x081f, 0x081e, 0x0819, 0x0818, 0x081b, 0x081a, //
    0x0805, 0x0804, 0x0807, 0x0806, 0x0801, 0x0800, 0x0803, 0x0802, //
    0x080d, 0x080c, 0x080f, 0x080e, 0x0809, 0x0808, 0x080b, 0x080a, //
    0x0856, 0x0852, 0x085e, 0x085a, 0x0846, 0x0842, 0x084e, 0x084a, //
    0x0876, 0x0872, 0x087e, 0x087a, 0x0866, 0x0862, 0x086e, 0x086a, //
    0x082b, 0x0829, 0x082f, 0x082d, 0x0823, 0x0821, 0x0827, 0x0825, //
    0x083b, 0x0839, 0x083f, 0x083d, 0x0833, 0x0831, 0x0837, 0x0835 //
};

static struct
{
    uint32_t Addr;
    uint32_t Size;
} VoiceClipState = {0};

static bool LoadVoiceClip(uint8_t VoiceID)
{
    if (VoiceID >= VOICE_ID_END)
    {
        return false;
    }

    uint32_t Addr = gEeprom.VOICE_PROMPT == VOICE_PROMPT_CHINESE ? 0x14c000 : 0x14c800;
    struct
    {
        uint32_t Offset;
        uint32_t Size;
    } Info;
    PY25Q16_ReadBuffer(Addr + 8 * VoiceID, &Info, 8);

    if (Info.Offset > 0x0b0000 || Info.Size > 0x019000)
    {
        return false;
    }

    VoiceClipState.Addr = 0x14d000 + Info.Offset;
    VoiceClipState.Size = Info.Size;
    return true;
}

static inline uint32_t CalcDelay(uint32_t Size)
{
    return 2 * Size; // in ms!!
}

static void LoadVoiceSamples()
{
    if (0 == VoiceClipState.Addr || 0 == VoiceClipState.Size)
    {
        return;
    }
    if (gVoiceBufLen >= VOICE_BUF_CAP)
    {
        return;
    }

    extern uint8_t **gFrameBuffer;
    uint8_t *Buf = (uint8_t *)gFrameBuffer;
    PY25Q16_ReadBuffer(VoiceClipState.Addr, Buf, VOICE_BUF_LEN);
    VoiceClipState.Addr += VOICE_BUF_LEN;
    VoiceClipState.Size -= VOICE_BUF_LEN;

    for (uint32_t i = 0; i < VOICE_BUF_LEN; i++)
    {
        gVoiceBuf[gVoiceBufWriteIndex][i] = VOICE_SAMPLES[Buf[i]];
    }
    VOICE_BUF_ForwardWriteIndex();
    gVoiceBufLen++;
}

void AUDIO_PlaySingleVoice(bool bFlag)
{
    uint8_t VoiceID;
    uint8_t Delay;

    VoiceID = gVoiceID[0];

    if (gEeprom.VOICE_PROMPT != VOICE_PROMPT_OFF && gVoiceWriteIndex > 0)
    {
        if (gEeprom.VOICE_PROMPT == VOICE_PROMPT_CHINESE)
        {   // Chinese
            if (VoiceID >= ARRAY_SIZE(VoiceClipLengthChinese))
                goto Bailout;

            Delay    = VoiceClipLengthChinese[VoiceID];
            VoiceID += VOICE_ID_CHI_BASE;
        }
        else
        {   // English
            if (VoiceID >= ARRAY_SIZE(VoiceClipLengthEnglish))
                goto Bailout;

            Delay    = VoiceClipLengthEnglish[VoiceID];
            VoiceID += VOICE_ID_ENG_BASE;
        }

        if (FUNCTION_IsRx())   // 1of11
            BK4819_SetAF(BK4819_AF_MUTE);

        #ifdef ENABLE_FMRADIO
            if (gFmRadioMode)
                BK1080_Mute(true);
        #endif

        AUDIO_AudioPathOn();

        #ifdef ENABLE_VOX
            gVoxResumeCountdown = 2000;
        #endif

        SYSTEM_DelayMs(5);
        AUDIO_PlayVoice(VoiceID);

        if (gVoiceWriteIndex == 1)
            Delay += 3;

        if (bFlag)
        {
            SYSTEM_DelayMs(Delay * 10);

            if (FUNCTION_IsRx())    // 1of11
                RADIO_SetModulation(gRxVfo->Modulation);

            #ifdef ENABLE_FMRADIO
                if (gFmRadioMode)
                    BK1080_Mute(false);
            #endif

            if (!gEnableSpeaker)
                AUDIO_AudioPathOff();

            gVoiceWriteIndex    = 0;
            gVoiceReadIndex     = 0;

            #ifdef ENABLE_VOX
                gVoxResumeCountdown = 80;
            #endif

            return;
        }

        gVoiceReadIndex                = 1;
        gCountdownToPlayNextVoice_10ms = Delay;
        gFlagPlayQueuedVoice           = false;

        return;
    }

Bailout:
    gVoiceReadIndex  = 0;
    gVoiceWriteIndex = 0;
}

void AUDIO_SetVoiceID(uint8_t Index, VOICE_ID_t VoiceID)
{
    if (Index >= ARRAY_SIZE(gVoiceID))
        return;

    if (Index == 0)
    {
        gVoiceWriteIndex = 0;
        gVoiceReadIndex  = 0;
    }

    gVoiceID[Index] = VoiceID;

    gVoiceWriteIndex++;
}

uint8_t AUDIO_SetDigitVoice(uint8_t Index, uint16_t Value)
{
    uint16_t Remainder;
    uint8_t  Result;
    uint8_t  Count;

    if (Index == 0)
    {
        gVoiceWriteIndex = 0;
        gVoiceReadIndex  = 0;
    }

    Count     = 0;
    Result    = Value / 1000U;
    Remainder = Value % 1000U;
    if (Remainder < 100U)
    {
        if (Remainder < 10U)
            goto Skip;
    }
    else
    {
        Result = Remainder / 100U;
        gVoiceID[gVoiceWriteIndex++] = (VOICE_ID_t)Result;
        Count++;
        Remainder -= Result * 100U;
    }
    Result = Remainder / 10U;
    gVoiceID[gVoiceWriteIndex++] = (VOICE_ID_t)Result;
    Count++;
    Remainder -= Result * 10U;

Skip:
    gVoiceID[gVoiceWriteIndex++] = (VOICE_ID_t)Remainder;

    return Count + 1U;
}

void AUDIO_PlayQueuedVoice(void)
{
    uint8_t VoiceID;
    uint8_t Delay;
    bool    Skip;

    Skip = false;

    if (gVoiceReadIndex != gVoiceWriteIndex && gEeprom.VOICE_PROMPT != VOICE_PROMPT_OFF)
    {
        VoiceID = gVoiceID[gVoiceReadIndex];
        if (gEeprom.VOICE_PROMPT == VOICE_PROMPT_CHINESE)
        {
            if (VoiceID < ARRAY_SIZE(VoiceClipLengthChinese))
            {
                Delay = VoiceClipLengthChinese[VoiceID];
                VoiceID += VOICE_ID_CHI_BASE;
            }
            else
                Skip = true;
        }
        else
        {
            if (VoiceID < ARRAY_SIZE(VoiceClipLengthEnglish))
            {
                Delay = VoiceClipLengthEnglish[VoiceID];
                VoiceID += VOICE_ID_ENG_BASE;
            }
            else
                Skip = true;
        }

        gVoiceReadIndex++;

        if (!Skip)
        {
            if (gVoiceReadIndex == gVoiceWriteIndex)
                Delay += 3;

            AUDIO_PlayVoice(VoiceID);

            gCountdownToPlayNextVoice_10ms = Delay;
            gFlagPlayQueuedVoice           = false;

            #ifdef ENABLE_VOX
                gVoxResumeCountdown = 2000;
            #endif

            return;
        }
    }

    if (FUNCTION_IsRx())
    {
        RADIO_SetModulation(gRxVfo->Modulation); // 1of11
    }

    #ifdef ENABLE_FMRADIO
        if (gFmRadioMode)
            BK1080_Mute(false);
    #endif

    if (!gEnableSpeaker)
        AUDIO_AudioPathOff();

    #ifdef ENABLE_VOX
        gVoxResumeCountdown = 80;
    #endif

    gVoiceWriteIndex    = 0;
    gVoiceReadIndex     = 0;
}

#endif
