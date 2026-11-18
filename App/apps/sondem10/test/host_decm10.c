/* Host checks for the M10 parser and 19.2 kHz soft-input path. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../sonde_info.h"
#include "../decm10.h"
#include "../decm10.c"

static uint16_t mk_check(const uint8_t *msg, uint8_t len)
{
    uint16_t c = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = msg[i];
        b = (uint8_t)((b >> 1) | (b << 7));
        b ^= (uint8_t)((b >> 2) & 0xFFu);
        uint8_t t6 = (uint8_t)((c ^ (c >> 2) ^ (c >> 4)) & 1u);
        uint8_t t7 = (uint8_t)(((c >> 1) ^ (c >> 3) ^ (c >> 5)) & 1u);
        uint8_t t = (uint8_t)((c & 0x3Fu) | (t6 << 6) | (t7 << 7));
        uint8_t s = (uint8_t)((c >> 7) & 0xFFu);
        s ^= (uint8_t)((s >> 2) & 0xFFu);
        c = (uint16_t)(((c & 0xFFu) << 8) | (uint8_t)(b ^ t ^ s));
    }
    return c;
}

static void put_be32(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void feed_chip(decm10_t *d, uint8_t chip)
{
    uint16_t s = chip ? 3000u : 1000u;
    (void)decm10_push(d, s);
    (void)decm10_push(d, s);
}

static void feed_bit(decm10_t *d, uint8_t bit, uint8_t *prev)
{
    uint8_t man = bit ? *prev : (uint8_t)(1u ^ *prev);
    if (man) { feed_chip(d, 0); feed_chip(d, 1); }
    else     { feed_chip(d, 1); feed_chip(d, 0); }
    *prev = man;
}

static void feed_rawheader(decm10_t *d)
{
    uint32_t h = 0x99994C99u;
    for (int i = 0; i < 32; i++)
        feed_chip(d, (uint8_t)((h >> (31 - i)) & 1u));
}

static void feed_frame_body(decm10_t *d, const uint8_t *f)
{
    uint8_t prev = 0;
    uint16_t nb = (uint16_t)((f[0] + 1u) * 8u);
    for (uint16_t bi = 0; bi < nb; bi++) {
        uint8_t bit = (uint8_t)((f[bi >> 3] >> (7 - (bi & 7))) & 1u);
        feed_bit(d, bit, &prev);
    }
}

static void build_frame(uint8_t *f)
{
    memset(f, 0, M10_FRAME_LEN);
    f[0] = 0x64; f[1] = 0x9F; f[2] = 0x20;
    /* 8 m/s east, 3 m/s south, 5.2 m/s up, in 1/200 m/s big-endian */
    f[0x04] = 0x06; f[0x05] = 0x40;   /*  1600 */
    f[0x06] = 0xFD; f[0x07] = 0xA8;   /*  -600 */
    f[0x08] = 0x04; f[0x09] = 0x10;   /*  1040 */
    put_be32(f + 0x0E, 582206678); /* 48.8 degrees in 2^32/360 units */
    put_be32(f + 0x12, 27440069);  /* 2.3 degrees */
    put_be32(f + 0x16, 1200000);   /* 1200 m in millimetres */
    f[0x1D] = 0;                   /* Valid 3D fix. */
    f[0x3E] = 0;                   /* Thermistor range. */
    f[0x3F] = 0x99; f[0x40] = 0xA5; /* About 20 C. */
    /* 55 %RH reference 1000000, sensor 985500: ratio 0.9855, 45.0 %RH */
    f[0x32] = 0x40; f[0x33] = 0x42; f[0x34] = 0x0F;
    f[0x35] = 0x9C; f[0x36] = 0x09; f[0x37] = 0x0F;
    f[0x5D] = 0x0B;
    f[0x5F] = 0xA5;
    f[0x60] = 0xD2; f[0x61] = 0x24;
    {
        uint16_t cs = mk_check(f, 0x63);
        f[0x63] = (uint8_t)(cs >> 8);
        f[0x64] = (uint8_t)cs;
    }
}

static int fields_ok(const sonde_info_t *out)
{
    /* centi-arcseconds: 48.8 N, 2.3 E */
    int32_t lat_err = out->latS - 17568000;
    int32_t lon_err = out->lonS - 828000;
    if (lat_err < 0) lat_err = -lat_err;
    if (lon_err < 0) lon_err = -lon_err;
    return strcmp(out->id, "A05-B-11234") == 0 &&
           (out->flags & (SONDE_HAS_ID | SONDE_HAS_POS | SONDE_HAS_ALT)) ==
               (SONDE_HAS_ID | SONDE_HAS_POS | SONDE_HAS_ALT) &&
           out->tRaw == 0x0599u && out->alt_m == 1200 &&
           (out->flags & SONDE_HAS_RH) && out->rh10 == 450 &&
           lat_err <= 1 && lon_err <= 1 &&
           /* 8.5 m/s towards 110.6 degrees, climbing 5.2 m/s */
           (out->flags & SONDE_HAS_VEL) && out->vs_dm == 52 &&
           out->hs_ms == 9 && out->hdg == 111;
}

int main(void)
{
    uint8_t f[M10_FRAME_LEN];
    int fail = 0;
    build_frame(f);

    {
        decm10_t d;
        sonde_info_t out;
        decm10_init(&d);
        memcpy(d.frm, f, M10_FRAME_LEN);
        memset(&out, 0, sizeof out);
        if (!decm10_parse(&d, &out) || !fields_ok(&out)) {
            fprintf(stderr, "parse: id=%s flags=%u lat=%ld lon=%ld alt=%ld\n",
                    out.id, out.flags, (long)out.latS, (long)out.lonS,
                    (long)out.alt_m);
            fail++;
        } else
            puts("  parse fields: OK");
    }

    {
        decm10_t d;
        sonde_info_t out;
        decm10_init(&d);
        d.ph = 0;
        for (int i = 0; i < 16; i++) feed_chip(&d, (uint8_t)(i & 1u));
        feed_rawheader(&d);
        feed_frame_body(&d, f);
        for (int i = 0; i < 8; i++) feed_chip(&d, (uint8_t)(i & 1u));
        memset(&out, 0, sizeof out);
        if (d.state != 2 || !decm10_parse(&d, &out) || !fields_ok(&out)) {
            fprintf(stderr, "softin: state=%u bits=%u id=%s flags=%u\n",
                    d.state, d.nbit, out.id, out.flags);
            fail++;
        } else
            puts("  softin frame: OK");
    }

    /* Heading past 255: the byte holds bits 0..7, bit 8 rides in the flags. */
    {
        uint8_t g[M10_FRAME_LEN];
        decm10_t d;
        sonde_info_t out;
        memcpy(g, f, sizeof g);
        g[0x04] = 0xF9; g[0x05] = 0xC0;   /* -1600: 8 m/s west  */
        g[0x06] = 0x02; g[0x07] = 0x58;   /*   600: 3 m/s north */
        {
            uint16_t cs = mk_check(g, 0x63);
            g[0x63] = (uint8_t)(cs >> 8);
            g[0x64] = (uint8_t)cs;
        }
        decm10_init(&d);
        memcpy(d.frm, g, M10_FRAME_LEN);
        memset(&out, 0, sizeof out);
        if (!decm10_parse(&d, &out) || out.hs_ms != 9 ||
            (out.hdg | (out.flags & SONDE_HDG_256) * (256u / SONDE_HDG_256)) != 291u) {
            fprintf(stderr, "heading 291: hs=%u hdg=%u flags=%u\n",
                    out.hs_ms, out.hdg, out.flags);
            fail++;
        } else
            puts("  heading past 255: OK");
    }

    if (fail) return 1;
    puts("OK");
    return 0;
}
