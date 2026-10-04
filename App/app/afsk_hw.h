/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_AFSK_HW_H
#define APP_AFSK_HW_H
#include "apps/app_api.h"
/* TIM2 runs at 120 kHz: exact 100-tick symbols, 400 input cycles per tick. */
#define AFSK_TIMER_HZ 120000u
#define AFSK_SYMBOL_TICKS 100u
#define AFSK_LATENESS_TICKS 2u
uint8_t AFSK_HW_Info(app_tx_info_t *out);
bool AFSK_HW_AcquireTimer(void);
void AFSK_HW_ReleaseTimer(void);
uint32_t AFSK_HW_Now(void);
void AFSK_HW_Arm(uint32_t deadline);
void AFSK_HW_Stop(void);
bool AFSK_HW_PTT(void);
uint8_t AFSK_HW_Prepare(const app_tx_info_t *info, uint8_t gain);
void AFSK_HW_PA(bool on);
void AFSK_HW_Tone(bool space);
void AFSK_HW_Restore(void);
void AFSK_TX_Tick(uint32_t now);
#endif
