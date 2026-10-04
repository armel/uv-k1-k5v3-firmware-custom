/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APRSTX_TX_RESIDENT_H
#define APRSTX_TX_RESIDENT_H
#include "resident_api.h"

/* The caller consumes its fresh-PTT latch before entering this operation.
 * Every return cancels the resident service, including failed submissions.
 * No LCD, battery, audio or BK API is called while AFSK owns the radio. */
uint8_t aprs_resident_send(const app_api_t *api, const uint8_t *frame,
                           uint16_t length, const app_tx_info_t *selected);
const char *aprs_resident_result(uint8_t error);
#endif
