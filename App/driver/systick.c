/* Copyright 2023 Dual Tachyon
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
#include "py32f0xx.h"
#include "systick.h"
#include "misc.h"
// 0x20000324
static uint32_t gTickMultiplier;
void SYSTICK_Init(void)
{
    SysTick_Config(720000);
    gTickMultiplier = 72;
    NVIC_SetPriority(SysTick_IRQn, 0);
}
void SYSTICK_DelayUs(uint32_t Delay)
{
    const uint32_t ticks = Delay * gTickMultiplier;
    uint32_t elapsed_ticks = 0;
    uint32_t Start = SysTick->LOAD;
    uint32_t Previous = SysTick->VAL;
    
    // FIX: Simplified loop - removed inner "wait for change" loop
    while (elapsed_ticks < ticks)
    {
        uint32_t Current = SysTick->VAL;
        
        // FIX: Corrected wraparound detection (was: "Current < Previous - 10")
        if (Current < Previous)
        {
            // Normal countdown case
            uint32_t Delta = Previous - Current;
            elapsed_ticks += Delta;
        }
        else if (Current > Previous)
        {
            // Wraparound case: SysTick went 0 → LOAD → Current
            uint32_t Delta = Previous + (Start - Current);
            elapsed_ticks += Delta;
        }
        
        Previous = Current;
    }
}