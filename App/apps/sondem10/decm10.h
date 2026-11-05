#ifndef DECM10_H
#define DECM10_H
#include <stdint.h>
#include <stdbool.h>
#include "sonde_info.h"

#define M10_FRAME_LEN 119u
#define M10_BAUD      9615u
#define M10_CHIP_N    64u

typedef struct {
    int32_t  meanQ8, ph, integ, h1, pe;
    uint16_t nbit, needBits, guard;
    uint8_t  state, inv, sign, half, prev, cpos, nfill;
    int16_t  chips[M10_CHIP_N];
    uint8_t  frm[M10_FRAME_LEN];
} decm10_t;

void decm10_init(decm10_t *d);
void decm10_rearm(decm10_t *d);
bool decm10_busy(const decm10_t *d);
bool decm10_push(decm10_t *d, uint16_t sample);
/* CRC-checked standard M10; fills ID, thermistor data, position and altitude. */
bool decm10_parse(decm10_t *d, sonde_info_t *out);
#endif
