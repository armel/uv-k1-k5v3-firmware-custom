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
 *
 * PY32F071 RTC used as a plain seconds counter (1 Hz), the epoch being
 * 2000-01-01 00:00:00 (counter reset value). Drives the Doppler mode:
 * the main loop polls gRtcSecondTick (set every second by RTC_IRQHandler).
 */

#ifndef DRIVER_RTC_H
#define DRIVER_RTC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_FEAT_F4HWN_DOPPLER

// Set once per second from RTC_IRQHandler, consumed by the main loop.
extern volatile bool gRtcSecondTick;

// Initializes the RTC: prefers the LSE crystal (32.768 kHz), falls back to
// the internal LSI (~32.8 kHz) when no crystal is present. Enables the
// second interrupt (SECF -> EXTI line 19).
void RTC_Init(void);

// Writes the seconds counter (epoch 2000-01-01).
void RTC_SetUnix32(uint32_t Seconds);

// Reads the seconds counter (epoch 2000-01-01).
uint32_t RTC_GetUnix32(void);

// True when the LSE crystal is running, false when LSI is used.
bool RTC_IsLse(void);

#endif // ENABLE_FEAT_F4HWN_DOPPLER

#endif // DRIVER_RTC_H
