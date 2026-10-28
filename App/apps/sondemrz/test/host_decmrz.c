/* Host checks for the MRZ parser and 19.2 kHz receive path. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../sonde_info.h"
#include "../decmrz.h"
#include "../decmrz.c"

static void load_arc(decmrz_t *d)
{
    static const uint32_t arc[MRZ_ARC_N] = {
        16200000, 9563418, 5053048, 2565006, 1287480, 644368,
        322263, 161141, 80572, 40286, 20143, 10072,
        5036, 2518, 1259, 629, 315, 157, 79, 39, 20, 10, 5, 2
    };
    memcpy(d->arc, arc, sizeof arc);
}

static uint16_t mk_crc(const uint8_t *p, unsigned n)
{
    uint16_t crc = 0xFFFFu;
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8u; i++)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0xA001u : 0u));
    }
    return crc;
}

static void put_le16(uint8_t *p, int16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void finish(uint8_t *f, unsigned crc_len)
{
    uint16_t crc = mk_crc(f + 3, crc_len);
    put_le16(f + 3 + crc_len, (int16_t)crc);
}

static void common(uint8_t *f, unsigned len, uint8_t sub, uint8_t sec)
{
    memset(f, 0, len);
    f[0] = 0xAA; f[1] = 0xBF; f[2] = 0x35; f[3] = sub;
    f[4] = 12; f[5] = 34; f[6] = sec;
}

static void build_ecef(uint8_t *f, uint8_t sub, uint8_t sec, uint32_t serial)
{
    common(f, 50, sub, sec);
    put_le32(f + 8, 420659420); put_le32(f + 12, 16895428); put_le32(f + 16, 477684060);
    put_le16(f + 20, 536); put_le16(f + 22, 822); put_le16(f + 24, 194);
    f[26] = 9;
    put_le16(f + 29, 2410); put_le16(f + 31, 4500);
    f[43] = sub; put_le32(f + 44, (int32_t)serial);
    finish(f, 45);
}

static void build_latlon(uint8_t *f, uint8_t sub, uint8_t sec, uint32_t serial)
{
    common(f, 47, sub, sec);
    put_le32(f + 7, 48800000); put_le32(f + 11, 2300000); put_le32(f + 15, 120000);
    put_le16(f + 19, 900); put_le16(f + 21, 11100); f[23] = 9;
    put_le16(f + 26, 2410); put_le16(f + 28, 4500);
    f[30] = f[31] = 0xFF; f[40] = sub; put_le32(f + 41, (int32_t)serial);
    finish(f, 42);
}

static void init(decmrz_t *d)
{
    decmrz_init(d);
    load_arc(d);
}

static void load_frame(decmrz_t *d, const uint8_t *f, unsigned len)
{
    memcpy(d->sl[0].frm, f, len);
    d->sl[0].state = ST_DONE;
}

static int common_fields(const sonde_info_t *out)
{
    int32_t la = out->latS - 17568000, lo = out->lonS - 828000;
    if (la < 0) la = -la;
    if (lo < 0) lo = -lo;
    return (out->flags & (SONDE_HAS_POS | SONDE_HAS_ALT | SONDE_HAS_TEMP |
                          SONDE_HAS_RH | SONDE_HAS_VEL)) ==
           (SONDE_HAS_POS | SONDE_HAS_ALT | SONDE_HAS_TEMP |
            SONDE_HAS_RH | SONDE_HAS_VEL) &&
           la <= 100 && lo <= 100 && out->alt_m >= 1195 && out->alt_m <= 1205 &&
           out->temp_c10 == 241 && out->rh == 45;
}

static void feed_chip(decmrz_t *d, uint8_t chip, uint8_t inv)
{
    uint16_t sample = (chip ^ inv) ? 3000u : 1000u;
    for (unsigned i = 0; i < 8u; i++) (void)decmrz_push(d, sample);
}

static void feed_byte(decmrz_t *d, uint8_t byte, uint8_t inv)
{
    for (int shift = 7; shift >= 0; shift--) {
        uint8_t bit = (uint8_t)((byte >> shift) & 1u);
        feed_chip(d, bit ? 1u : 0u, inv);
        feed_chip(d, bit ? 0u : 1u, inv);
    }
}

static void feed_frame(decmrz_t *d, const uint8_t *f, unsigned len, uint8_t inv)
{
    for (unsigned i = 0; i < 64u; i++) feed_chip(d, (uint8_t)(i & 1u), inv);
    feed_byte(d, 0xAA, inv);
    for (unsigned i = 0; i < len; i++) feed_byte(d, f[i], inv);
    for (unsigned i = 0; i < 4u; i++) feed_chip(d, (uint8_t)(i & 1u), inv);
}

int main(void)
{
    uint8_t f[MRZ_FRAME_MAX];
    int fail = 0;
    decmrz_t d;
    sonde_info_t out;

    init(&d);
    build_ecef(f, 0x0C, 56, 5667);
    load_frame(&d, f, 50);
    if (!decmrz_parse(&d, &out) || !common_fields(&out) ||
        (out.flags & SONDE_HAS_ID) || out.vs_dm < 50 || out.vs_dm > 54 ||
        out.hs_ms < 8 || out.hs_ms > 9) {
        fprintf(stderr, "ECEF parse: flags=%u lat=%ld lon=%ld alt=%ld vs=%d hs=%u\n",
                out.flags, (long)out.latS, (long)out.lonS, (long)out.alt_m,
                out.vs_dm, out.hs_ms);
        fail++;
    } else puts("  ECEF fields: OK");

    build_ecef(f, 0x0D, 57, 39155);
    load_frame(&d, f, 50);
    if (!decmrz_parse(&d, &out) || strcmp(out.id, "MRZC7C0D3") ||
        !(out.flags & SONDE_HAS_ID)) {
        fprintf(stderr, "serial collection: id=%s flags=%u\n", out.id, out.flags);
        fail++;
    } else puts("  multi-subframe serial: OK");

    init(&d);
    build_latlon(f, 0x0C, 56, 5667);
    load_frame(&d, f, 47);
    if (!decmrz_parse(&d, &out) || !common_fields(&out) ||
        out.latS != 17568000 || out.lonS != 828000 || out.alt_m != 1200 ||
        out.vs_dm != 0 || out.hs_ms != 9 || out.hdg != 111) {
        fprintf(stderr, "lat/lon parse: flags=%u lat=%ld lon=%ld alt=%ld hdg=%u\n",
                out.flags, (long)out.latS, (long)out.lonS, (long)out.alt_m, out.hdg);
        fail++;
    } else puts("  lat/lon fields: OK");

    f[7] ^= 1u;
    load_frame(&d, f, 47);
    if (decmrz_parse(&d, &out)) {
        fputs("bad CRC accepted\n", stderr); fail++;
    } else puts("  CRC rejection: OK");
    f[7] ^= 1u;

    for (uint8_t inv = 0; inv < 2u; inv++) {
        bool done = false;
        init(&d);
        feed_frame(&d, f, 47, inv);
        for (unsigned k = 0; k < MRZ_NSL; k++) done |= d.sl[k].state == ST_DONE;
        if (!done || !decmrz_parse(&d, &out) || !common_fields(&out)) {
            fprintf(stderr, "demod input %u: state=%u/%u bits=%u/%u flags=%u\n",
                    inv, d.sl[0].state, d.sl[1].state,
                    d.sl[0].nbit, d.sl[1].nbit, out.flags);
            fail++;
        } else printf("  demod input%s: OK\n", inv ? " inverted" : "");
    }

    if (fail) return 1;
    puts("OK");
    return 0;
}
