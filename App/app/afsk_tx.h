/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_AFSK_TX_H
#define APP_AFSK_TX_H
#include "apps/app_api.h"
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_AFSK_TX
uint8_t AFSK_TX_Info(app_tx_info_t *out);
uint8_t AFSK_TX_Submit(const uint8_t *frame, uint16_t length,
                     const app_afsk_opts_t *opts);
void AFSK_TX_Poll(app_afsk_status_t *out);
void AFSK_TX_Cancel(void);
bool AFSK_TX_OwnsRF(void);
#else
static inline bool AFSK_TX_OwnsRF(void) { return false; }
static inline void AFSK_TX_Cancel(void) {}
#endif
#endif
