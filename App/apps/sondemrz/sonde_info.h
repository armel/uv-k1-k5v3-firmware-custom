/* Decoded fields used by the Sonde MRZ overlay. */
#ifndef SONDE_INFO_H
#define SONDE_INFO_H
#include <stdint.h>

/* The whole structure goes to GeoNav and comes back on EXIT. */
typedef struct {
    char     id[12];
    int32_t  latS, lonS;    /* centi-arcseconds, + north / east */
    int32_t  alt_m;
    int16_t  vs_dm;         /* vertical speed, dm/s */
    int16_t  temp_c10;      /* temperature, 0.1 C */
    uint8_t  hs_ms;         /* horizontal speed, whole m/s */
    uint8_t  hdg;           /* heading bits 0..7; bit 8 is SONDE_HDG_256 */
    uint8_t  flags;
    uint8_t  rh;            /* whole %RH */
} sonde_info_t;

enum {
    SONDE_HAS_POS  = 1u,
    SONDE_HAS_ALT  = 2u,
    SONDE_HAS_ID   = 4u,
    SONDE_HAS_TEMP = 8u,
    SONDE_HAS_VEL  = 16u,
    SONDE_HAS_RH   = 32u,
    SONDE_HDG_256  = 64u
};
#endif
