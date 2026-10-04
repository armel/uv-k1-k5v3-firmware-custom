/* Copyright 2026 Armel F4HWN contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Append-only API level 2 SDK bridge. This lets the app build before the
 * resident AFSK extension merges, without changing the firmware's API header.
 * The resident extension is required at run time; API1 firmware cannot run it.
 */
#ifndef APRSTX_RESIDENT_API_H
#define APRSTX_RESIDENT_API_H

#include "../app_api.h"
#include <stddef.h>

#define APRS_CAP_AFSK_TX 0x00000008u

#if APP_API_LEVEL < 2
enum {
    APP_AFSK_OK = 0, APP_AFSK_BUSY, APP_AFSK_BAD_ARGUMENT,
    APP_AFSK_TX_DENIED, APP_AFSK_LOW_BATTERY, APP_AFSK_HIGH_VOLTAGE,
    APP_AFSK_CHANNEL_CHANGED, APP_AFSK_PTT_RELEASED, APP_AFSK_TIMING,
    APP_AFSK_TIMEOUT, APP_AFSK_CANCELED, APP_AFSK_INTERNAL
};
enum {
    APP_AFSK_IDLE = 0, APP_AFSK_ACTIVE, APP_AFSK_DONE,
    APP_AFSK_ABORTED, APP_AFSK_ERROR
};
enum { APP_AFSK_REQUIRE_PHYSICAL_PTT = 1u };
enum { APP_TX_INFO_BUSY = 1u, APP_TX_INFO_SIMPLEX = 2u };
enum { APP_TX_MOD_FM = 0u, APP_TX_MOD_AM = 1u,
       APP_TX_MOD_USB = 2u, APP_TX_MOD_OTHER = 255u };

typedef struct {
    uint16_t size;
    uint8_t denial, flags;
    uint32_t tx_freq_10hz, rx_freq_10hz, channel_token;
    uint8_t modulation, power, bandwidth, reserved;
} app_tx_info_t;

typedef struct {
    uint16_t size, flags;
    uint32_t channel_token;
    uint16_t preamble_flags;
    uint8_t tail_flags, tone_gain;
    uint16_t max_on_ms, reserved;
} app_afsk_opts_t;

typedef struct {
    uint16_t size;
    uint8_t state, error;
    uint32_t bits_sent, pa_on_us, max_lateness_cycles;
} app_afsk_status_t;

typedef struct {
    app_api_t v1;
    uint8_t (*tx_info)(app_tx_info_t *out);
    uint8_t (*afsk_submit)(const uint8_t *frame, uint16_t length,
                           const app_afsk_opts_t *opts);
    void (*afsk_poll)(app_afsk_status_t *out);
    void (*afsk_cancel)(void);
} aprs_resident_api_t;
#else
typedef app_api_t aprs_resident_api_t;
#endif

_Static_assert(sizeof(app_tx_info_t) == 20u, "TX info ABI changed");
_Static_assert(sizeof(app_afsk_opts_t) == 16u, "AFSK options ABI changed");
_Static_assert(sizeof(app_afsk_status_t) == 16u, "AFSK status ABI changed");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(aprs_resident_api_t, tx_info) == 284u,
               "AFSK API must append to API1");
_Static_assert(offsetof(aprs_resident_api_t, afsk_cancel) == 296u,
               "AFSK API callback layout changed");
#endif

static inline bool aprs_resident_available(const app_api_t *api)
{
    /* Check the prefix before reading any extension function pointers. */
    if (!api || api->abi_major != 1u || api->api_level < 2u ||
        api->api_size < offsetof(aprs_resident_api_t, afsk_cancel) +
                        sizeof(((aprs_resident_api_t *)0)->afsk_cancel))
        return false;
    const aprs_resident_api_t *r = (const aprs_resident_api_t *)api;
    return r->tx_info && r->afsk_submit && r->afsk_poll && r->afsk_cancel;
}
#endif
