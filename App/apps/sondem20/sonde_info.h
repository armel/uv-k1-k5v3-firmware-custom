/* Common decoded radiosonde fields (M20 app). */
#ifndef SONDE_INFO_H
#define SONDE_INFO_H
#include <stdint.h>

typedef struct {
    char     id[12];
    int32_t  latS, lonS;    /* centi-arcseconds, + north / east */
    int32_t  alt_m;
    uint16_t tRaw;      /* thermistor ADC incl. range bits (frame 0x04..0x05) */
    int16_t  temp_c10;
    uint16_t rhQ11;     /* humidity: 6.5 (cal + 80000) / (hum + 80000), Q11 (SONDE_HAS_RH) */
    uint16_t tuRaw;     /* RH sensor NTC ADC (frame 0x06..0x07) */
    int16_t  vs_dm;     /* vertical speed, dm/s (with SONDE_HAS_VEL)         */
    uint16_t hs_ms;     /* horizontal speed, whole m/s (with SONDE_HAS_VEL)  */
    uint16_t hdg;       /* heading, degrees 0..359 from north (SONDE_HAS_VEL) */
    uint8_t  flags;
} sonde_info_t;

enum {
    SONDE_HAS_POS   = 1u,
    SONDE_HAS_ALT   = 2u,
    SONDE_HAS_TEMP  = 4u,
    SONDE_HAS_RH    = 8u,
    SONDE_HAS_VEL   = 16u,
    SONDE_HAS_ID    = 32u
};
#endif
