#ifndef APPS_GEONAV_HANDOFF_H
#define APPS_GEONAV_HANDOFF_H

#include <stdint.h>
#include "app_api.h"

#define GEONAV_APP_NAME      "GeoNav"
/* The three radiosonde structures share this exact 24-byte prefix. Sending it
 * in place avoids format-conversion code in their space-constrained overlays.
 * Coordinates use centi-arcseconds: 360000 units per degree.
 * A sonde app hands off the first APP_HANDOFF_MAX bytes of its sonde_info_t
 * and GeoNav hands the same bytes back on EXIT, so the sonde app can show its
 * last frame again: every field its screen shows must lie within those
 * bytes. */
typedef struct {
    char label[12];
    int32_t lat_cas;
    int32_t lon_cas;
    int32_t alt_m;
} geonav_handoff_t;

_Static_assert(sizeof(geonav_handoff_t) == 24u &&
               sizeof(geonav_handoff_t) <= APP_HANDOFF_MAX,
               "GeoNav sonde prefix must fit the handoff mailbox");

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
                   offsetof(type, flags) < APP_HANDOFF_MAX, \
                   "sonde_info_t prefix must match the GeoNav handoff")

#endif
