/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APRSTX_UI_H
#define APRSTX_UI_H
#include "../app_api.h"
#include "protocol.h"
typedef struct {
    aprs_model_t model;
    char edit[8];
    uint8_t setup, editing, field, length, negative, numeric;
    uint8_t last_digit, tap, position_set, dirty;
    uint16_t tap_ms;
} aprs_ui_t;
void aprs_ui_init(aprs_ui_t *u);
void aprs_ui_tick(aprs_ui_t *u); /* 20 ms, outside TX */
bool aprs_ui_key(const app_api_t *a, aprs_ui_t *u, uint8_t key);
void aprs_ui_draw(const app_api_t *a, const aprs_ui_t *u,
                   const char *status, uint32_t tx_freq);
#endif
