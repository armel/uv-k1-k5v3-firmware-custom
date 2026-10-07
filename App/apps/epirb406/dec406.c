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

static uint8_t bit(const dec406_t *d, uint8_t n)   /* n = 25..144 */
{
    uint8_t i = (uint8_t)(n - 25u);
    return (uint8_t)((d->bits[i >> 3] >> (7u - (i & 7u))) & 1u);
}

static uint32_t field(const dec406_t *d, uint8_t first, uint8_t len)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < len; i++) v = (v << 1) | bit(d, (uint8_t)(first + i));
    return v;
}

/* Remainder of the codeword spanning bits first..last: 0 when valid. */
static uint32_t syndrome(const dec406_t *d, uint8_t first, uint8_t last, uint32_t gen, uint8_t r)
{
    uint32_t reg = 0;
    for (uint8_t n = first; n <= last; n++) {
        reg = (reg << 1) | bit(d, n);
        if (reg >> r) reg ^= gen;
    }
    return reg;
}

/* First bit of the position field in PDF-1, 0 when the position is not decoded:
 * 65 = standard location (15' steps), 67 = ELT(DT) and RLS (30' steps). */
static uint8_t posBase(uint8_t code)
{
    /* 0010 EPIRB MMSI, 0011 ELT 24-bit, 0100 ELT serial, 0101 ELT op. designator,
     * 0110 EPIRB serial, 0111 PLB serial, 1100 ship security, 1110 test */
    if ((code >= 2u && code <= 7u) || code == 12u || code == 14u) return 65u;
    /* 1001 ELT(DT), 1101 RLS */
    if (code == 9u || code == 13u) return 67u;
    return 0u;
}

void dec406_parse(const dec406_t *d, dec406_info_t *o)
{

    o->longMsg   = bit(d, 25);
    o->selftest  = d->selftest;
    o->bch1      = syndrome(d, 25, 106, BCH1_GEN, 21) == 0;
    o->bch2      = o->longMsg ? syndrome(d, 107, 144, BCH2_GEN, 12) == 0 : 1u;
    o->userProto = bit(d, 26);
    o->country   = (uint16_t)field(d, 27, 10);
    o->proto     = (uint8_t)(o->userProto ? field(d, 37, 3) : field(d, 37, 4));
    /* Both layouts are: sign, latitude, sign, longitude (one bit longer), up to
     * bit 85. Latitude is 9 bits from bit 66 or 8 bits from bit 68. */
    uint8_t base    = o->userProto ? 0u : posBase(o->proto);
    uint8_t latLen  = (uint8_t)(base == 65u ? 9u : 8u);
    uint8_t lonSign = (uint8_t)(base + 1u + latLen);
    o->stdLoc    = base == 65u;
    o->idRaw     = !base;
    o->idData    = field(d, 41, 24);
    o->hasPos = o->hasFine = o->internalPos = o->homing = 0;
    o->latS = o->lonS = 0;

    /* 15-hex ID: bits 26-85, with the position replaced by its default value
     * (signs 0, latitude and longitude all ones) when it is a decoded one */
    for (uint8_t k = 0; k < 15; k++) {
        uint8_t nib = 0;
        for (uint8_t j = 0; j < 4; j++) {
            uint8_t n = (uint8_t)(26u + 4u * k + j);
            uint8_t b = (base && n >= base) ? (uint8_t)(n != base && n != lonSign) : bit(d, n);
            nib = (uint8_t)((nib << 1) | b);
        }
        o->id[k] = (char)(nib < 10u ? '0' + nib : 'A' - 10 + nib);   /* no hex table */
    }
    o->id[15] = '\0';

    if (!base) return;

    uint32_t la = field(d, (uint8_t)(base + 1u), latLen);
    if (la != (1u << latLen) - 1u) {             /* not the default position */
        int32_t unit = o->stdLoc ? 900 : 1800;   /* arc seconds per step */
        int32_t lat = (int32_t)la * unit;
        int32_t lon = (int32_t)field(d, (uint8_t)(lonSign + 1u), (uint8_t)(latLen + 1u)) * unit;
        /* PDF-2 offsets: sign (1 = +), minutes, seconds in 4 s steps, twice.
         * Standard location: from bit 113, 5-bit minutes, behind the fixed
         * bits 107-110 = 1101. ELT(DT) and RLS: from bit 115, 4-bit minutes.
         * Seconds 1111 is the default (no offset) value. */
        if (o->longMsg && o->bch2 && (!o->stdLoc || field(d, 107, 4) == 0xDu)) {
            uint8_t p = (uint8_t)(base + 48u), m = (uint8_t)(latLen - 4u);
            uint32_t am = field(d, (uint8_t)(p + 1u), m),     as = field(d, (uint8_t)(p + 1u + m), 4);
            uint32_t om = field(d, (uint8_t)(p + 6u + m), m), os = field(d, (uint8_t)(p + 6u + 2u * m), 4);
            if (am <= 30u && om <= 30u && as < 15u && os < 15u) {
                int32_t dlat = (int32_t)(am * 60u + as * 4u), dlon = (int32_t)(om * 60u + os * 4u);
                lat += bit(d, p) ? dlat : -dlat;
                lon += bit(d, (uint8_t)(p + 5u + m)) ? dlon : -dlon;
                o->hasFine = 1;
            }
        }
        o->latS = bit(d, base) ? -lat : lat;
        o->lonS = bit(d, lonSign) ? -lon : lon;
        o->hasPos = 1;
    }
    if (o->stdLoc && o->longMsg && o->bch2) { o->internalPos = bit(d, 111); o->homing = bit(d, 112); }
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
    uint8_t n = in->userProto ? (uint8_t)(16u + (in->proto & 7u)) : (uint8_t)(in->proto & 15u);
    const char *p = NAMES;
    while (n--) { while (*p) p++; p++; }
    return p;
}
