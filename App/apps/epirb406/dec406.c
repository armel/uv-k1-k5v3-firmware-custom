/* Copyright 2026 Johan Denoyer F4WAT
 * https://github.com/jdenoy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "dec406.h"

#define SLOT      ((int32_t)DEC406_HALF * 256)   /* half-bit slot, Q8 samples  */
#define MEAN_SRCH 7     /* DC tracker while searching: ~13 ms at 9.6 kHz        */
#define MEAN_DATA 10    /* DC tracker once locked: ~107 ms                      */
#define LEAK_SHR  4     /* phase integrator leak: ~1.7 ms, bounds DC error gain */
#define PLL_SHR   3     /* zero-crossing correction gain 1/8                    */
#define SYNC_TOL  2     /* half-bit mismatches allowed in preamble + sync       */

/* 13 preamble ones + frame sync, as 44 half-bit signs (1 = "10", 0 = "01"),
 * split in a 12-bit high part and a 32-bit low part */
#define PAT_BITS     44u
#define PAT_HI_MASK  0xFFFu
#define PAT_HI       0xAAAu                               /* same for both syncs */
#define PAT_NORMAL   0xAAA959AAu                          /* sync 000101111      */
#define PAT_SELFTEST 0xAAA9A655u                          /* sync 011010000      */

/* BCH generators (C/S T.001): PDF-1 BCH(82,61), PDF-2 BCH(26,14) */
#define BCH1_GEN  0x26D9E3u    /* x^21+x^18+x^17+x^15+x^14+x^12+x^11+x^8+x^7+x^6+x^5+x+1 */
#define BCH2_GEN  0x1539u      /* x^12+x^10+x^8+x^5+x^4+x^3+1 */

/* Bit count, one pass per set bit: at most 4 x 32 passes per half-bit (every
 * 12 samples, and only while searching), far inside the 5000 cycles of a
 * sample period; 36 bytes smaller than the branch-free version. */
static uint8_t popc32(uint32_t x)
{
    uint8_t n = 0;
    for (; x; x &= x - 1u) n++;
    return n;
}

/* Match the last 44 half-bits against one sync pattern in both polarities:
 * returns 1 (normal), 2 (inverted) or 0. Inverted distance = 44 - distance. */
static uint8_t match(const dec406_t *d, uint32_t lo)
{
    uint8_t n = (uint8_t)(popc32((d->hsrHi ^ PAT_HI) & PAT_HI_MASK) + popc32(d->hsrLo ^ lo));
    if (n <= SYNC_TOL) return 1;
    if (n >= PAT_BITS - SYNC_TOL) return 2;
    return 0;
}

void dec406_rearm(dec406_t *d)
{
    d->lvl = 0; d->acc = 0; d->h1 = 0; d->ph = 0; d->hsrHi = d->hsrLo = 0;
    d->state = DEC406_SEARCH; d->half = 0; d->inv = 0; d->selftest = 0;
    d->sign = 0; d->nbits = 0; d->total = 0;
    for (uint8_t i = 0; i < sizeof d->bits; i++) d->bits[i] = 0;
}

void dec406_init(dec406_t *d, bool integrate)
{
    d->meanQ8 = -1;              /* seeded by the first sample */
    d->integrate = integrate ? 1u : 0u;
    dec406_rearm(d);
}

static void putBit(dec406_t *d, uint8_t b)
{
    if (b) d->bits[d->nbits >> 3] |= (uint8_t)(0x80u >> (d->nbits & 7u));
    d->nbits++;
}

static void emitHalf(dec406_t *d, int32_t h)
{
    d->hsrHi = (d->hsrHi << 1) | (d->hsrLo >> 31);
    d->hsrLo = (d->hsrLo << 1) | (h > 0 ? 1u : 0u);

    if (d->state == DEC406_SEARCH) {
        uint8_t m = match(d, PAT_NORMAL), st = 0;
        if (!m) { m = match(d, PAT_SELFTEST); st = 1; }
        if (m) {
            d->inv = (uint8_t)(m - 1u); d->selftest = st;
            d->state = DEC406_DATA; d->half = 0; d->nbits = 0;
        }
        return;
    }

    if (d->state != DEC406_DATA) return;

    if (!d->half) { d->h1 = h; d->half = 1; return; }
    d->half = 0;
    /* biphase-L: "1" = high then low, "0" = low then high (soft decision) */
    uint8_t b = (uint8_t)(((d->h1 - h) > 0) ^ d->inv);
    putBit(d, b);
    if (d->nbits == 1) d->total = b ? 120u : 88u;   /* bit 25: long / short */
    if (d->nbits >= d->total) d->state = DEC406_DONE;
}

bool dec406_push(dec406_t *d, uint16_t sample)
{
    if (d->state == DEC406_DONE) return true;

    int32_t xq = (int32_t)sample << 8;
    if (d->meanQ8 < 0) d->meanQ8 = xq;
    int32_t e = xq - d->meanQ8;
    /* round to nearest: a plain >> floors, so small negative errors always moved
     * the mean down and small positive ones never moved it up; at the ~150 LSB
     * swing measured on PA4 that downward creep corrupted the end of the frame */
    uint8_t msh = d->state == DEC406_SEARCH ? MEAN_SRCH : MEAN_DATA;
    d->meanQ8 += (e + (1 << (msh - 1))) >> msh;
    int32_t v = e >> 4;

    if (d->integrate) d->lvl += v - (d->lvl >> LEAK_SHR);
    else              d->lvl  = v;

    /* DPLL: phase-waveform zero crossings sit on half-bit slot boundaries */
    uint8_t s = d->lvl > 0 ? 1u : 0u;
    if (s != d->sign) {
        int32_t err = d->ph;
        if (err > SLOT / 2) err -= SLOT;
        d->ph -= err >> PLL_SHR;
        if (d->ph < 0) d->ph += SLOT;
        d->sign = s;
    }

    d->acc += d->lvl;
    d->ph  += 256;
    if (d->ph >= SLOT) {
        d->ph -= SLOT;
        emitHalf(d, d->acc);
        d->acc = 0;
    }
    return d->state == DEC406_DONE;
}

/* ---- parsing ---- */

/* Message bits are numbered as in C/S T.001: 25..144, bit 25 = MSB of b[0]. */
static unsigned bit(const uint8_t *b, unsigned n)
{
    unsigned i = n - 25u;
    return (b[i >> 3] >> (7u - (i & 7u))) & 1u;
}

static uint32_t field(const uint8_t *b, unsigned first, unsigned len)
{
    uint32_t v = 0;
    while (len--) v = (v << 1) | bit(b, first++);
    return v;
}

/* Write v (len bits) at first..first+len-1. */
static void setf(uint8_t *b, unsigned first, unsigned len, uint32_t v)
{
    while (len--) {
        unsigned i = first + len - 25u, m = 0x80u >> (i & 7u);
        if (v & 1u) b[i >> 3] |= m; else b[i >> 3] &= (uint8_t)~m;
        v >>= 1;
    }
}

/* Remainder of the codeword spanning bits first..last: 0 when valid. */
static uint32_t syndrome(const uint8_t *b, unsigned first, unsigned last, uint32_t gen, unsigned r)
{
    uint32_t reg = 0;
    for (unsigned n = first; n <= last; n++) {
        reg = (reg << 1) | bit(b, n);
        if (reg >> r) reg ^= gen;
    }
    return reg;
}

/* Position formats (T.001 Annex A3.3). Every one is: N/S flag, 7 bits of
 * latitude degrees, sub-degree bits, E/W flag, 8 bits of longitude degrees,
 * sub-degree bits; then, in PDF-2, an optional offset per axis: sign (1 = +),
 * minutes, seconds in 4 s steps (1111 = no offset). */
typedef struct {
    uint8_t base;       /* first bit of the position                       */
    uint8_t sub;        /* sub-degree bits                                 */
    uint8_t unit;       /* minutes per sub-degree step                     */
    uint8_t off;        /* first bit of the offsets, 0 = none              */
    uint8_t offMin;     /* offset minute bits                              */
    uint8_t src;        /* position source bit (homing = next), 0 = none   */
} pos_fmt_t;

enum { F_STD, F_NAT, F_RLS, F_ELTDT, F_USER, F_NONE };

static const pos_fmt_t FMT[] = {
    [F_STD]   = {  65, 2, 15, 113, 5, 111 },   /* A3.3.5 standard location  */
    [F_NAT]   = {  59, 5,  2, 113, 2, 111 },   /* A3.3.6 national location  */
    [F_RLS]   = {  67, 1, 30, 115, 4, 107 },   /* A3.3.7 RLS location       */
    [F_ELTDT] = {  67, 1, 30, 115, 4,   0 },   /* A3.3.8 ELT(DT) location   */
    [F_USER]  = { 108, 4,  4,   0, 0, 107 },   /* A3.3.4 user-location      */
};

/* Location protocol code (bits 37-40) -> format (Table A2-B). */
static const uint8_t LOC_FMT[16] = {
    F_NONE, F_NONE, F_STD, F_STD, F_STD, F_STD, F_STD, F_STD,
    F_NAT, F_ELTDT, F_NAT, F_NAT, F_STD, F_RLS, F_STD, F_NAT,
};

void dec406_parse(dec406_t *d, dec406_info_t *o)
{
    uint8_t *b = d->bits;

    o->longMsg   = bit(b, 25);
    o->selftest  = d->selftest;
    o->bch1      = syndrome(b, 25, 106, BCH1_GEN, 21) == 0;
    o->bch2      = o->longMsg ? syndrome(b, 107, 144, BCH2_GEN, 12) == 0 : 1u;
    o->userProto = bit(b, 26);
    o->country   = (uint16_t)field(b, 27, 10);
    unsigned code = field(b, 37, 4);
    o->proto     = (uint8_t)(o->userProto ? 16u + (code >> 1) : code);   /* name index */
#ifndef DEC406_LEAN                      /* host test only (not on the radio) */
    o->idData    = field(b, 41, 24);
#endif
    o->hasPos = o->hasFine = o->internalPos = o->homing = 0;   /* latS, lonS: only with hasPos */

    /* User-location: long user protocols other than orbitography (000),
     * national (100) and spare (101), position in PDF-2 (A3.3.4). */
    unsigned f = o->userProto ? (o->longMsg && ((0xCEu >> (code >> 1)) & 1u) ? F_USER : F_NONE)
                             : LOC_FMT[code];
#ifndef DEC406_LEAN
    o->stdLoc = f == F_STD;
#endif
    o->idRaw  = !o->userProto && f == F_NONE;   /* spare location code: layout unknown */

    int32_t *v = &o->latS;              /* latS, then lonS */
    unsigned neg = 0;
    const pos_fmt_t *p = &FMT[f];
    if (f != F_NONE) {
        /* Read the position and leave its default value in the message (A3.2:
         * flags 0, degrees all ones, sub-degree all ones in standard, RLS and
         * ELT(DT) location, zeros in national and user-location). */
        unsigned n = p->base, w = 8u + p->sub;
        for (unsigned k = 0; k < 2u; k++, n += w++) {
            neg |= bit(b, n) << k;
            v[k] = (int32_t)(field(b, n + 1u, 7u + k) * 3600u + field(b, n + 8u + k, p->sub) * p->unit * 60u);
            unsigned len = 7u + k + p->sub;                           /* after the flag */
            setf(b, n, len + 1u, p->unit > 4u ? (1u << len) - 1u : ((1u << (7u + k)) - 1u) << p->sub);
        }
    }

    for (unsigned k = 0; k < 15; k++) {
        unsigned nib = field(b, 26u + 4u * k, 4);
        o->id[k] = (char)(nib < 10u ? '0' + nib : 'A' - 10 + nib);   /* no hex table */
    }
    o->id[15] = '\0';

    /* PDF-2 is only trusted with a valid BCH-2 */
    unsigned pdf2 = o->longMsg && o->bch2;
    /* No position: latitude beyond 90 deg, i.e. the default (flag N, 127 deg) or
     * the ELT(DT) cancellation message (fixed bits 67-85: flag S, 125 deg). */
    if (f == F_NONE || (f == F_USER && !pdf2)) return;
    if (v[0] > 90 * 3600) { if (f == F_ELTDT && neg & 1u) o->idRaw = 2; return; }

    /* Offsets: standard and national need bits 107-110 = 1101 (fixed bits and,
     * in national, the position data flag); ELT(DT) bits 113-114 = 00 flag a
     * rotating field (e.g. operator 3LD) instead of offsets. */
    if (p->off && pdf2 &&
        (p->off == 113u ? field(b, 107, 4) == 0xDu : f != F_ELTDT || field(b, 113, 2))) {
        unsigned n = p->off, m = p->offMin;
        o->hasFine = 1;
        for (unsigned k = 0; k < 2u; k++, n += 5u + m) {
            uint32_t se = field(b, n + 1u + m, 4);
            int32_t dd = (int32_t)(field(b, n + 1u, m) * 60u + se * 4u);
            if (se == 15u) { o->hasFine = 0; continue; }     /* default: no offset */
            v[k] += bit(b, n) ? dd : -dd;
        }
    }
    for (unsigned k = 0; k < 2u; k++) if (neg >> k & 1u) v[k] = -v[k];
    o->hasPos = 1;

    /* bit 1 set: the source flag is present; bit 0: 1 = internal device */
    if (p->src && pdf2) {
        o->internalPos = (uint8_t)(2u | bit(b, p->src));
        if (f != F_USER) o->homing = bit(b, p->src + 1u);
    }
}

const char *dec406_proto_name(const dec406_info_t *in)
{
    /* 16 location protocol names (bits 37-40), then 8 user protocol names
     * (bits 37-39), packed to avoid a pointer table */
    static const char NAMES[] =
        "Loc 0000\0Loc 0001\0EPIRB MMSI\0ELT 24bit\0ELT serial\0ELT opdes\0"
        "EPIRB ser\0PLB serial\0Nat ELT\0ELT-DT\0Nat EPIRB\0Nat PLB\0"
        "Ship sec\0RLS\0Std test\0Nat test\0"
        "Orbito\0ELT avia\0Maritime\0Serial\0National\0Spare\0Callsign\0User test";
    uint8_t n = in->proto;
    const char *p = NAMES;
    while (n--) { while (*p) p++; p++; }
    return p;
}
