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

#include "driver/backlight.h"
#include "py32f071_ll_system.h"
#include "py32f071_ll_dma.h"
#include "py32f071_ll_bus.h"
#include "py32f071_ll_tim.h"
#include "driver/gpio.h"
#include "driver/systick.h"
#include "settings.h"
#include "external/printf/printf.h"

#ifdef ENABLE_FEAT_F4HWN
    #include "driver/system.h"
    #include "audio.h"
    #include "misc.h"
#endif

#define PWM_FREQ 4000
// 32 levels keep every step of the value[] table distinct after
// quantization while halving the DMA duty cycle buffer (128 bytes saved)
#define DUTY_CYCLE_LEVELS 32

#define DUTY_CYCLE_ON_VALUE GPIO_PIN_MASK(GPIO_PIN_BACKLIGHT)
#define DUTY_CYCLE_OFF_VALUE (DUTY_CYCLE_ON_VALUE << 16)

#define TIMx TIM7
#define DMA_CHANNEL LL_DMA_CHANNEL_7

static uint32_t dutyCycle[DUTY_CYCLE_LEVELS];

// this is decremented once every 500ms
uint16_t gBacklightCountdown_500ms = 0;
bool gUpdateBacklight = false;
bool backlightOn;

static uint8_t currentIndex = 0;
static int16_t currentBrightness = 0;
static int16_t targetBrightness = 0;
static int16_t fadeStep = 0;

static void BACKLIGHT_SetHardwareBrightness(uint8_t brightness);

#ifdef ENABLE_FEAT_F4HWN
    // Power-on fade-in tuning.
    // STEPS  = number of intermediate brightness stops (smoothness).
    // STEP_MS = pacing between stops.
    // STEPS * STEP_MS ~= total fade duration in ms.
    #define BL_STARTUP_FADE_STEPS   40
    #define BL_STARTUP_FADE_STEP_MS 12
    // First PWM value that produces a non-zero duty cycle with 32 levels.
    #define BL_STARTUP_VISIBLE_MIN  ((255 + DUTY_CYCLE_LEVELS - 1) / DUTY_CYCLE_LEVELS)
#endif

#ifdef ENABLE_FEAT_F4HWN
    const uint8_t value[] = {
        0,    // 0 off
        8,    // 1 visible in the dark
        16,   // 2
        24,   // 3
        32,   // 4
        48,   // 5
        72,   // 6
        104,  // 7
        150,  // 8
        200,  // 9
        255   // 10 max
    };
#endif

#ifdef ENABLE_FEAT_F4HWN_SLEEP
    uint16_t gSleepModeCountdown_500ms = 0;
#endif

void BACKLIGHT_InitHardware()
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM7);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_TIM7);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_TIM7);

    // 48 MHz / ((1 + PSC) * (1 + ARR)) == PWM_freq * levels
    LL_TIM_SetPrescaler(TIMx, 0);
    LL_TIM_SetAutoReload(TIMx, SystemCoreClock / PWM_FREQ / DUTY_CYCLE_LEVELS - 1);
    LL_TIM_EnableARRPreload(TIMx);
    LL_TIM_EnableDMAReq_UPDATE(TIMx);
    LL_TIM_EnableUpdateEvent(TIMx);

    LL_DMA_DisableChannel(DMA1, DMA_CHANNEL) ;
    LL_SYSCFG_SetDMARemap(DMA1, DMA_CHANNEL, LL_SYSCFG_DMA_MAP_TIM7_UP);

    LL_DMA_ConfigTransfer(DMA1, DMA_CHANNEL,                //
                          LL_DMA_DIRECTION_MEMORY_TO_PERIPH //
                              | LL_DMA_MODE_CIRCULAR        //
                              | LL_DMA_PERIPH_NOINCREMENT   //
                              | LL_DMA_MEMORY_INCREMENT     //
                              | LL_DMA_PDATAALIGN_WORD      //
                              | LL_DMA_MDATAALIGN_WORD      //
                              | LL_DMA_PRIORITY_HIGH        //
    );

    LL_DMA_SetMemoryAddress(DMA1, DMA_CHANNEL, (uint32_t)dutyCycle);
    LL_DMA_SetPeriphAddress(DMA1, DMA_CHANNEL, (uint32_t)(&GPIO_PORT(GPIO_PIN_BACKLIGHT)->BSRR));
    LL_DMA_SetDataLength(DMA1, DMA_CHANNEL, sizeof(dutyCycle) / sizeof(uint32_t));
}

static void BACKLIGHT_Sound(void)
{
    if (gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_SOUND ||
        gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_ALL
#ifdef ENABLE_FEAT_F4HWN_LOGO
        || gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_LOGO
#endif
    )
    {
        AUDIO_PlayBeep(BEEP_880HZ_60MS_TRIPLE_BEEP);
        AUDIO_PlayBeep(BEEP_880HZ_60MS_TRIPLE_BEEP);
    }

    gK5startup = false;
}

void BACKLIGHT_UpdateTickless(void) {
    while(gUpdateBacklight) {
        BACKLIGHT_Update();
        SYSTEM_DelayMs(10);
    }
}

#ifdef ENABLE_FEAT_F4HWN
// Soft, progressive power-on fade-in.
// Starts at the first visible PWM level, then ramps to targetBrightness using
// a smoothstep (3x^2 - 2x^3) easing curve. Avoiding sub-PWM values removes the
// apparent pause between drawing the welcome screen and lighting it.
static void BACKLIGHT_FadeInStartup(void)
{
    const int16_t from = targetBrightness < BL_STARTUP_VISIBLE_MIN
        ? targetBrightness
        : BL_STARTUP_VISIBLE_MIN;
    const int16_t span = targetBrightness - from;

    currentBrightness = from;
    BACKLIGHT_SetHardwareBrightness((uint8_t)currentBrightness);

    if (span <= 0) {
        gUpdateBacklight = false;
        return;
    }

    for (uint16_t s = 1; s < BL_STARTUP_FADE_STEPS; s++) {
        // x in Q8 (0..256), e = smoothstep(x) in Q16 (0..65536)
        uint32_t x = (uint32_t)s * 256 / BL_STARTUP_FADE_STEPS;
        uint32_t e = (x * x * (768 - 2 * x)) >> 8;
        currentBrightness = from + (int16_t)(((uint32_t)span * e) >> 16);
        BACKLIGHT_SetHardwareBrightness((uint8_t)currentBrightness);
        SYSTEM_DelayMs(BL_STARTUP_FADE_STEP_MS);
    }

    currentBrightness = targetBrightness;
    BACKLIGHT_SetHardwareBrightness((uint8_t)currentBrightness);
    gUpdateBacklight = false;
}
#endif

void BACKLIGHT_TurnOn(void)
{
    #ifdef ENABLE_FEAT_F4HWN_SLEEP
        gSleepModeCountdown_500ms = gSetting_set_off * 120;
    #endif

    #ifdef ENABLE_FEAT_F4HWN
        gBacklightBrightnessOld = BACKLIGHT_GetBrightness();
    #endif

    if (gEeprom.BACKLIGHT_TIME == 0) {
        BACKLIGHT_TurnOff();
        #ifdef ENABLE_FEAT_F4HWN
            if(gK5startup == true) 
            {
                BACKLIGHT_Sound();
            }
        #endif
        return;
    }

    backlightOn = true;

    BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MAX);

#ifdef ENABLE_FEAT_F4HWN
    if(gK5startup == true)
#else
    static bool startup = true;
    
    if(startup)
#endif
    {
#ifdef ENABLE_FEAT_F4HWN
        BACKLIGHT_FadeInStartup();
        BACKLIGHT_Sound();
#else
        BACKLIGHT_UpdateTickless();
        startup = false;
#endif
    }

    switch (gEeprom.BACKLIGHT_TIME) {
        default:
        case 1 ... 60:  // 5 sec * value
            gBacklightCountdown_500ms = 1 + (gEeprom.BACKLIGHT_TIME * 5) * 2;
            break;
        case 61:    // always on
            gBacklightCountdown_500ms = 0;
            break;
    }
}

void BACKLIGHT_TurnOff()
{
    BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MIN);
    gBacklightCountdown_500ms = 0;
    backlightOn = false;
}

bool BACKLIGHT_IsOn()
{
    return backlightOn;
}

static void BACKLIGHT_SetHardwareBrightness(uint8_t brightness)
{
    // printf("BL: %d\n", brigtness);

    if (0 == brightness)
    {
        LL_TIM_DisableCounter(TIMx);
        LL_DMA_DisableChannel(DMA1, DMA_CHANNEL);
        SYSTICK_DelayUs(1);
        GPIO_TurnOffBacklight();
    }
    else
    {
        const uint32_t level = (uint32_t)(brightness) * DUTY_CYCLE_LEVELS / 255;
        if (level >= DUTY_CYCLE_LEVELS)
        {
            LL_TIM_DisableCounter(TIMx);
            LL_DMA_DisableChannel(DMA1, DMA_CHANNEL);
            GPIO_TurnOnBacklight();
        }
        else
        {
            for (uint32_t i = 0; i < DUTY_CYCLE_LEVELS; i++)
            {
                dutyCycle[i] = i < level ? DUTY_CYCLE_ON_VALUE : DUTY_CYCLE_OFF_VALUE;
            }

            if (!LL_TIM_IsEnabledCounter(TIMx))
            {
                LL_DMA_EnableChannel(DMA1, DMA_CHANNEL);
                LL_TIM_EnableCounter(TIMx);
            }
        }
    }
}

void BACKLIGHT_Update(void)
{
    if (gUpdateBacklight) {
        currentBrightness += fadeStep;

        if ((fadeStep > 0 && currentBrightness >= targetBrightness) || 
            (fadeStep < 0 && currentBrightness <= targetBrightness)) {
            currentBrightness = targetBrightness;
            gUpdateBacklight = false;
        }

        BACKLIGHT_SetHardwareBrightness((uint8_t)currentBrightness);
    }
}

void BACKLIGHT_SetBrightness(uint8_t targetIndex)
{
    if (currentIndex == targetIndex) {
        return;
    }

    currentIndex = targetIndex;
    targetBrightness = value[targetIndex];

    int16_t diff = targetBrightness - currentBrightness;

    if (diff == 0) {
        gUpdateBacklight = false;
        return;
    }
    
    fadeStep = diff > 0 ? -(-diff >> 4) : diff >> 4;

    gUpdateBacklight = true;
}

uint8_t BACKLIGHT_GetBrightness(void)
{
    return currentIndex;
}
