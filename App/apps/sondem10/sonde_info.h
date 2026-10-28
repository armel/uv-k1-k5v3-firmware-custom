/* Decoded fields used by the Sonde M10 overlay. */
#ifndef SONDE_INFO_H
#define SONDE_INFO_H
#include <stdint.h>

/* The first APP_HANDOFF_MAX (32) bytes go to GeoNav and come back on EXIT:
 * every field the screen shows lies there, the decoder inputs after them. */
typedef struct {
    char     id[12];
    int32_t  latS, lonS;    /* centi-arcseconds, + north / east */
    int32_t  alt_m;
    int16_t  vs_dm;     /* vertical speed, dm/s (with SONDE_HAS_VEL)         */
    int16_t  temp_c10;  /* (with SONDE_HAS_TEMP) */
    uint8_t  hs_ms;     /* horizontal speed, whole m/s (with SONDE_HAS_VEL): 232 at most */
    uint8_t  hdg;       /* heading from north, bits 0..7 (bit 8: SONDE_HDG_256) */
    uint8_t  flags;
    uint8_t  rh;        /* whole %RH, temperature terms applied (with SONDE_HAS_RH) */
    /* Past APP_HANDOFF_MAX: the decoder's raw inputs to temp_c10 and rh. */
    uint16_t tRaw;      /* thermistor ADC incl. range bits */
    int16_t  rh10;      /* humidity, 0.1 %RH, before the temperature terms (SONDE_HAS_RH) */
} sonde_info_t;

enum {
    SONDE_HAS_POS  = 1u,
    SONDE_HAS_ALT  = 2u,
    SONDE_HAS_ID   = 4u,
    SONDE_HAS_TEMP = 8u,
    SONDE_HAS_VEL  = 16u,
    SONDE_HAS_RH   = 32u,
    SONDE_HDG_256  = 64u    /* heading bit 8, so hdg fits a byte */
};
#endif
