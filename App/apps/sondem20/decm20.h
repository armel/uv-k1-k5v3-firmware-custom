#ifndef DECM20_H
#define DECM20_H
#include <stdint.h>
#include <stdbool.h>
#include "sonde_info.h"

#define M20_FRAME_LEN 70u
#define M20_BAUD      9600u /* Manchester chip rate (auto_rx m20mod softin) */
#define M20_CHIP_N    64u   /* ring holds rawheader (32) + first softin word (32) */

typedef struct {
    int32_t  meanQ8, ph, integ, h1, pe;
    uint16_t nbit, needBits, guard;
    uint8_t  state, inv, sign, half, prev, cpos, nfill;
    int16_t  chips[M20_CHIP_N];
    uint8_t  frm[M20_FRAME_LEN];
} decm20_t;

void decm20_init(decm20_t *d);
void decm20_rearm(decm20_t *d);
bool decm20_busy(const decm20_t *d);
bool decm20_push(decm20_t *d, uint16_t sample);
/* CRC-checked; fills id/pos/alt/press and raw thermistor ADC (out->tRaw). */
bool decm20_parse(decm20_t *d, sonde_info_t *out);
#endif
