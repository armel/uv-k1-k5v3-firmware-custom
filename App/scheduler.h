/* Copyright 2025 muzkr https://github.com/muzkr
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

#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "py32f0xx.h"

static void inline SCHEDULER_Enable()
{
    NVIC_EnableIRQ(SysTick_IRQn);
}

static void inline SCHEDULER_Disable()
{
    NVIC_DisableIRQ(SysTick_IRQn);
}

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
/* Free-running 10 ms SysTick counter (wraps after ~497 days). */
uint32_t SCHEDULER_GetTick10ms(void);
#endif

#endif
