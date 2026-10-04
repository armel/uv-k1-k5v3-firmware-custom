/* Copyright 2026 Armel F4HWN contributors
 * SPDX-License-Identifier: Apache-2.0 */
#include "tx_resident.h"

#ifndef APRS_CCA_DBM
#define APRS_CCA_DBM (-110)
#endif

const char *aprs_resident_result(uint8_t error)
{
    switch (error) {
    case APP_AFSK_OK: return "SENT";
    case APP_AFSK_BUSY: return "BUSY";
    case APP_AFSK_PTT_RELEASED:
    case APP_AFSK_CANCELED: return "ABORTED";
    case APP_AFSK_TIMING: return "TIMING ERROR";
    case APP_AFSK_TIMEOUT: return "TX TIMEOUT";
    case APP_AFSK_CHANNEL_CHANGED: return "CHANNEL CHANGED";
    case APP_AFSK_LOW_BATTERY: return "LOW BATTERY";
    case APP_AFSK_HIGH_VOLTAGE: return "HIGH VOLTAGE";
    default: return "TX DENIED";
    }
}

uint8_t aprs_resident_send(const app_api_t *api, const uint8_t *frame,
                           uint16_t length, const app_tx_info_t *selected)
{
    const aprs_resident_api_t *r = (const aprs_resident_api_t *)api;
    uint8_t error = APP_AFSK_BUSY;
    uint16_t quiet = 0, backoff = 0;
    uint32_t random = selected->channel_token ^ selected->tx_freq_10hz;
    static app_afsk_opts_t opts = {
        .size = sizeof(opts), .flags = APP_AFSK_REQUIRE_PHYSICAL_PTT,
        .preamble_flags = 45,
        .tail_flags = 3, .tone_gain = 66, .max_on_ms = 2000
    };
    static app_afsk_status_t status;
    status.size = sizeof(status);
    opts.channel_token = selected->channel_token;

    if (selected->modulation != APP_TX_MOD_FM ||
        !(selected->flags & APP_TX_INFO_SIMPLEX) ||
        selected->tx_freq_10hz != selected->rx_freq_10hz) {
        error = APP_AFSK_TX_DENIED;
        goto cleanup;
    }
    /* A bounded CCA wait in 20 ms foreground slices. The resident timer owns
     * the actual symbol clock, physical-PTT cancellation and PA-on deadline. */
    for (uint16_t tick = 0; tick < 250; ++tick) {
        if (api->get_key() != APP_KEY_PTT) {
            error = APP_AFSK_PTT_RELEASED;
            goto cleanup;
        }
        if (backoff) {
            --backoff;
        } else if (api->rssi_dbm() >= APRS_CCA_DBM) {
            quiet = 0;
            random = random * 1664525u + 1013904223u;
            backoff = 5u + (random & 15u); /* 100..400 ms, bounded by 500 ms */
        } else if (++quiet == 11) {
            /* submit rechecks resident permission/BCL/PTT/token immediately
             * before PA-on. The caller's selected snapshot stays frozen. */
            error = r->afsk_submit(frame, length, &opts);
            if (error != APP_AFSK_OK) goto cleanup;
            for (uint16_t poll = 0; poll < 250; ++poll) {
                if (api->get_key() != APP_KEY_PTT) {
                    error = APP_AFSK_PTT_RELEASED;
                    goto cleanup;
                }
                r->afsk_poll(&status);
                if (status.state != APP_AFSK_ACTIVE) {
                    error = status.state == APP_AFSK_DONE ? APP_AFSK_OK :
                            status.error ? status.error : APP_AFSK_INTERNAL;
                    goto cleanup;
                }
                api->delay_ms(10);
            }
            error = APP_AFSK_TIMEOUT;
            goto cleanup;
        }
        api->delay_ms(20);
        error = APP_AFSK_BUSY;
    }
cleanup:
    r->afsk_cancel();
    return error;
}
