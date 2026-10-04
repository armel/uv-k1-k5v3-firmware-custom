/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APRSTX_TX_H
#define APRSTX_TX_H
#include "../app_api.h"
#include "protocol.h"
enum { APRS_SENT, APRS_ABORTED, APRS_BUSY, APRS_DENIED, APRS_TIMING, APRS_VFO };
bool aprs_stock_ptt(void);
bool aprs_stock_clock_valid(void);
uint8_t aprs_stock_cca(const app_api_t *api);
uint8_t aprs_stock_send(const app_api_t *api, const uint8_t *frame, uint16_t length,
                        uint32_t expected_frequency);
void aprs_stock_cleanup(const app_api_t *api);
#endif
