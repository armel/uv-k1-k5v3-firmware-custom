/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_AFSK_CORE_H
#define APP_AFSK_CORE_H
#include "apps/app_api.h"

typedef struct {
    uint16_t byte, flags_left;
    uint8_t bit, ones, stage;
    bool stuffed_zero;
} afsk_hdlc_t;

bool AFSK_ValidateFrame(const uint8_t *frame, uint16_t length);
bool AFSK_ValidateOptions(const app_afsk_opts_t *opts);
void AFSK_HDLC_Init(afsk_hdlc_t *s, const app_afsk_opts_t *opts);
bool AFSK_HDLC_Next(afsk_hdlc_t *s, const uint8_t *frame, uint16_t length,
                    uint8_t tail_flags, uint8_t *bit);
#endif
