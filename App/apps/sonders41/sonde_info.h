/* Common decoded radiosonde fields (RS41 app). */
#ifndef SONDE_INFO_H
#define SONDE_INFO_H
#include <stdint.h>

typedef struct {
    char id[12];
    int32_t latS, lonS;     /* WGS84 latitude, longitude, centi-arcseconds, + north / east */
    int32_t alt_m;          /* height above the ellipsoid, m */
    int16_t vs_dm;          /* vertical speed, dm/s (with SONDE_HAS_POS) */
    uint16_t hs_ms;         /* horizontal speed, whole m/s (with SONDE_HAS_POS) */
    uint16_t hdg;           /* heading, degrees 0..359 from north (with SONDE_HAS_POS) */
    uint8_t flags;
    uint8_t batt_dv;        /* battery voltage, 0.1 V (STATUS, with SONDE_HAS_ID) */
} sonde_info_t;

enum {
    SONDE_HAS_POS = 1u,
    SONDE_HAS_ALT = 2u,
    SONDE_HAS_ID  = 32u
};
#endif
