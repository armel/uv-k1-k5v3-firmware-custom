/* Copyright 2026
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

#include <stdint.h>

#include "driver/rtc.h"

#include "py32f071_ll_bus.h"
#include "py32f071_ll_exti.h"
#include "py32f071_ll_pwr.h"
#include "py32f071_ll_rcc.h"
#include "py32f071_ll_rtc.h"

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

volatile bool gRtcSecondTick = false;

static bool gRtcLse = false;

void RTC_Init(void)
{
    uint32_t i;

    // 0. The RTC clock selection lives in the backup domain (RCC->BDCR);
    //    writes are ignored unless backup access is enabled first.
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_PWR_EnableBkUpAccess();
    // RTC register access requires the APB1 RTC gate (RCC_APBENR1_RTCAPBEN);
    // without it any RTC register poke triggers a bus fault (HardFault).
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_RTC);

    // 1. Backup-domain clock source: prefer the LSE crystal, fall back to LSI
    LL_RCC_LSE_Enable();
    for (i = 0; i < 2000000u; i++)
    {
        if (LL_RCC_LSE_IsReady())
        {
            break;
        }
    }
    if (LL_RCC_LSE_IsReady())
    {
        LL_RCC_SetRTCClockSource(LL_RCC_RTC_CLKSOURCE_LSE);
        gRtcLse = true;
    }
    else
    {
        LL_RCC_LSE_Disable();
        LL_RCC_LSI_Enable();
        for (i = 0; i < 2000000u; i++)
        {
            if (LL_RCC_LSI_IsReady())
            {
                break;
            }
        }
        if (!LL_RCC_LSI_IsReady())
        {
            return; // no RTC clock source available; leave the RTC off
        }
        LL_RCC_SetRTCClockSource(LL_RCC_RTC_CLKSOURCE_LSI);
        gRtcLse = false;
    }
    LL_RCC_EnableRTC();

    // 2. 1 Hz prescaler (32768 / 32768 = 1 Hz; LSI ~32.8 kHz, close enough)
    LL_RTC_DisableWriteProtection(RTC);
    LL_RTC_EnterInitMode(RTC);
    LL_RTC_SetAsynchPrescaler(RTC, 32767u);
    LL_RTC_ExitInitMode(RTC);
    LL_RTC_EnableWriteProtection(RTC);

    // 3. Second interrupt through EXTI line 19
    LL_RTC_ClearFlag_SEC(RTC);
    LL_RTC_EnableIT_SEC(RTC);
    LL_EXTI_EnableRisingTrig(LL_EXTI_LINE_19);
    LL_EXTI_EnableIT(LL_EXTI_LINE_19);
    LL_EXTI_ClearFlag(LL_EXTI_LINE_19);
    NVIC_ClearPendingIRQ(RTC_IRQn);
    NVIC_SetPriority(RTC_IRQn, 3);
    NVIC_EnableIRQ(RTC_IRQn);
}

void RTC_SetUnix32(uint32_t Seconds)
{
    LL_RTC_DisableWriteProtection(RTC);
    LL_RTC_EnterInitMode(RTC);
    LL_RTC_TIME_Set(RTC, Seconds);
    LL_RTC_ExitInitMode(RTC);
    LL_RTC_EnableWriteProtection(RTC);
}

uint32_t RTC_GetUnix32(void)
{
    // The counter is two 16-bit words; re-read until the high word is stable
    uint16_t high1 = (uint16_t)(LL_RTC_TIME_Get(RTC) >> 16);
    uint16_t low   = (uint16_t)(LL_RTC_TIME_Get(RTC));
    uint16_t high2 = (uint16_t)(LL_RTC_TIME_Get(RTC) >> 16);

    while (high1 != high2)
    {
        high1 = high2;
        low = (uint16_t)(LL_RTC_TIME_Get(RTC));
        high2 = (uint16_t)(LL_RTC_TIME_Get(RTC) >> 16);
    }
    return ((uint32_t)high2 << 16) | low;
}

bool RTC_IsLse(void)
{
    return gRtcLse;
}

void RTC_EnableSecondIT(bool Enable)
{
    if (Enable)
    {
        LL_RTC_ClearFlag_SEC(RTC);
        LL_RTC_EnableIT_SEC(RTC);
        NVIC_ClearPendingIRQ(RTC_IRQn);
        NVIC_EnableIRQ(RTC_IRQn);
    }
    else
    {
        LL_RTC_DisableIT_SEC(RTC);
        NVIC_DisableIRQ(RTC_IRQn);
    }
}

#endif // ENABLE_FEAT_F4HWN_DOPPLER
