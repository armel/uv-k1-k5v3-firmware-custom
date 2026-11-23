#ifndef APPS_GEONAV_HANDOFF_H
#define APPS_GEONAV_HANDOFF_H

#include <stdint.h>
#include "app_api.h"

#define GEONAV_APP_NAME      "GeoNav"
/* The four radiosonde structures (RS41, M10, M20, MRZ) share this exact
 * 32-byte layout. Sending it in place avoids format-conversion code in their
 * space-constrained overlays; APRS RX fills this layout itself.
 * Coordinates use centi-arcseconds: 360000 units per degree. Bytes 24..29 and
 * 31 remain sender-specific; byte 30 carries the common validity flags.
 * GeoNav hands the same bytes back on EXIT, so a sonde app can show its last
 * frame again. */
typedef struct {
    char label[12];
    int32_t lat_cas;
    int32_t lon_cas;
    int32_t alt_m;
    uint8_t sender_data[6];
    uint8_t flags;
    uint8_t sender_tail;
} geonav_handoff_t;

enum {
    GEONAV_HAS_POS = 1u,
    GEONAV_HAS_ALT = 2u
};

_Static_assert(sizeof(geonav_handoff_t) == APP_HANDOFF_MAX &&
               offsetof(geonav_handoff_t, flags) == 30u,
               "GeoNav message must match the handoff mailbox");

#define GEONAV_ASSERT_SONDE_PREFIX(type) \
    _Static_assert(offsetof(type, id) == offsetof(geonav_handoff_t, label) && \
                   sizeof(((type *)0)->id) == sizeof(((geonav_handoff_t *)0)->label) && \
                   offsetof(type, latS) == offsetof(geonav_handoff_t, lat_cas) && \
                   offsetof(type, lonS) == offsetof(geonav_handoff_t, lon_cas) && \
                   offsetof(type, alt_m) == offsetof(geonav_handoff_t, alt_m) && \
                   sizeof(type) >= APP_HANDOFF_MAX && \
                   offsetof(type, vs_dm) + sizeof(((type *)0)->vs_dm) <= APP_HANDOFF_MAX && \
                   offsetof(type, hs_ms) + sizeof(((type *)0)->hs_ms) <= APP_HANDOFF_MAX && \
                   offsetof(type, hdg) + sizeof(((type *)0)->hdg) <= APP_HANDOFF_MAX && \
                   offsetof(type, flags) == offsetof(geonav_handoff_t, flags) && \
                   SONDE_HAS_POS == GEONAV_HAS_POS && SONDE_HAS_ALT == GEONAV_HAS_ALT, \
                   "sonde_info_t prefix must match the GeoNav handoff")

#endif
