#ifndef DECMRZ_H
#define DECMRZ_H
#include <stdint.h>
#include <stdbool.h>
#include "sonde_info.h"

#define MRZ_FRAME_MAX 50u
#define MRZ_NSL       2u
#define MRZ_SYNC_ERR  2u
#define MRZ_ARC_N     24u

typedef struct {
    int32_t phase, step;
    uint32_t sr0;
    uint16_t sr1, nbit, needBits;
    uint8_t state, inv, lev, half;
    uint8_t frm[MRZ_FRAME_MAX];
} mrzsl_t;

typedef struct {
    int32_t dcQ8, amp, yPrev;
    uint32_t snC, snD;
    uint8_t serialMask, lastSec;
    mrzsl_t sl[MRZ_NSL];
    uint32_t arc[MRZ_ARC_N];
} decmrz_t;

void decmrz_init(decmrz_t *d);
void decmrz_rearm(decmrz_t *d);
bool decmrz_busy(const decmrz_t *d);
bool decmrz_push(decmrz_t *d, uint16_t sample);
bool decmrz_parse(decmrz_t *d, sonde_info_t *out);
#endif
