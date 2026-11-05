#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../decrs41.h"

static const uint8_t MASK[64] = {
    0x96,0x83,0x3E,0x51,0xB1,0x49,0x08,0x98,
    0x32,0x05,0x59,0x0E,0xF9,0x44,0xC6,0x26,
    0x21,0x60,0xC2,0xEA,0x79,0x5D,0x6D,0xA1,
    0x54,0x69,0x47,0x0C,0xDC,0xE8,0x5C,0xF1,
    0xF7,0x76,0x82,0x7F,0x07,0x99,0xA2,0x2C,
    0x93,0x7C,0x30,0x63,0xF5,0x10,0x2E,0x61,
    0xD0,0xBC,0xB4,0xB6,0x06,0xAA,0xF4,0x23,
    0x78,0x6E,0x3B,0xAE,0xBF,0x7B,0x4C,0xC1
};

/* atan(2^-i) in centi-arcseconds, as gen_assets.py writes them. */
static const uint32_t ARC[RS41_ARC_N] = {
    16200000u, 9563418u, 5053048u, 2565006u, 1287480u, 644368u, 322263u,
    161141u, 80572u, 40286u, 20143u, 10072u, 5036u, 2518u, 1259u, 629u,
    315u, 157u, 79u, 39u, 20u, 10u, 5u, 2u
};

/* decrs41_init, then the tables the app reads from its assets. */
static void initDec(decrs41_t *d)
{
    decrs41_init(d);
    memcpy(d->arc, ARC, sizeof ARC);
    memcpy(d->mask, MASK, sizeof MASK);
}

static uint16_t crc16(const uint8_t *p, uint8_t len)
{
    uint16_t crc = 0xFFFFu;
    while (len--) {
        crc ^= (uint16_t)*p++ << 8;
        for (uint8_t b = 0; b < 8u; b++) {
            bool high = (crc & 0x8000u) != 0u;
            crc = (uint16_t)(crc << 1);
            if (high) crc = (uint16_t)(crc ^ (uint16_t)0x1021u);
        }
    }
    return crc;
}

static void put32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16);
    p[3] = (uint8_t)(u >> 24);
}

static void put16(uint8_t *p, int16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)((uint16_t)v >> 8);
}

static void finishBlock(uint8_t *f, uint16_t pos)
{
    uint8_t len = f[pos + 1u];
    uint16_t crc = crc16(f + pos + 2u, len);
    f[pos + 2u + len] = (uint8_t)crc;
    f[pos + 3u + len] = (uint8_t)(crc >> 8);
}

/* Sync and the STATUS block shared by both layouts. */
static void startFrame(uint8_t *clear, const char *serial, uint8_t battery)
{
    static const uint8_t sync[8] =
        { 0x86,0x35,0xF4,0x40,0x93,0xDF,0x1A,0x60 };
    uint8_t *status;
    memset(clear, 0, RS41_FRAME_LEN);
    memcpy(clear, sync, sizeof sync);

    status = clear + 0x39;
    status[0] = 0x79;
    status[1] = 40;
    status[2] = 0x34;
    status[3] = 0x12;
    memcpy(status + 4, serial, 8);
    status[12] = battery;              /* 0x45: battery, 0.1 V */
    status[25] = 0;
    finishBlock(clear, 0x39);
}

static void whiten(const uint8_t *clear, uint8_t *wire)
{
    for (uint16_t i = 0; i < RS41_FRAME_LEN; i++)
        wire[i] = clear[i] ^ MASK[i & 63u];
}

/* Classic GPS block 0x7B at 0x112. */
static void putGps(uint8_t *clear)
{
    uint8_t *gps = clear + 0x112;
    gps[0] = 0x7B;
    gps[1] = 21;
    /* WGS84 ECEF for 48.8 N, 2.3 E at 1200 m, in centimetres. */
    put32(gps + 2, 420659420);
    put32(gps + 6, 16895428);
    put32(gps + 10, 477684060);
    /* ECEF velocity in cm/s: 5.2 m/s up, 8 m/s east, 3 m/s south. */
    put16(gps + 14, 536);
    put16(gps + 16, 822);
    put16(gps + 18, 194);
    gps[20] = 8;
    finishBlock(clear, 0x112);
}

/* Multi-GNSS block 0x82 at 0xA1 (rs41mod: 0x8226, fw 0x50dd). */
static void putGnss(uint8_t *clear)
{
    uint8_t *gnss = clear + 0xA1;
    gnss[0] = 0x82;
    gnss[1] = 38;
    /* WGS84 ECEF for 45.2 N, 5.7 E at 2500 m, in centimetres. */
    put32(gnss + 2, 448134061);
    put32(gnss + 6, 44729726);
    put32(gnss + 10, 450481156);
    /* ECEF velocity in cm/s: 12.3 m/s down, 6 m/s west, 4 m/s north. */
    put16(gnss + 14, -1085);
    put16(gnss + 16, -711);
    put16(gnss + 18, -591);
    gnss[20] = (uint8_t)2026u;         /* UTC date and time, unread */
    gnss[21] = (uint8_t)(2026u >> 8);
    gnss[22] = 10;
    gnss[23] = 19;
    gnss[24] = 12;
    finishBlock(clear, 0xA1);
}

static void makeFrame(uint8_t *clear, uint8_t *wire)
{
    startFrame(clear, "S1234567", 29);
    putGps(clear);
    whiten(clear, wire);
}

/* Multi-GNSS layout: PTU 0x7A, 0x96 (likely an authentication tag), 0x82
 * position, 0x83 satellites, 0x76 padding to the end; no classic GPS block. */
static void makeGnssFrame(uint8_t *clear, uint8_t *wire)
{
    static const uint8_t blocks[][3] = {
        { 0x65, 0x7A, 42 }, { 0x93, 0x96, 10 }, { 0xCB, 0x83, 41 },
        { 0xF8, 0x76, RS41_FRAME_LEN - 0xF8 - 4 }
    };
    startFrame(clear, "X7654321", 31);
    for (unsigned i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        clear[blocks[i][0]] = blocks[i][1];
        clear[blocks[i][0] + 1u] = blocks[i][2];
        finishBlock(clear, blocks[i][0]);
    }
    putGnss(clear);
    whiten(clear, wire);
}

static void loadWire(decrs41_t *d, const uint8_t *wire)
{
    static const uint8_t sync[8] =
        { 0x86,0x35,0xF4,0x40,0x93,0xDF,0x1A,0x60 };
    memcpy(d->sl[0].frm, wire, RS41_FRAME_LEN);
    memcpy(d->sl[0].frm, sync, sizeof sync);
    d->sl[0].state = 2u;               /* ST_DONE: a complete frame */
}

static void checkParse(void)
{
    uint8_t clear[RS41_FRAME_LEN], wire[RS41_FRAME_LEN];
    decrs41_t d;
    sonde_info_t info = {0};
    makeFrame(clear, wire);
    initDec(&d);
    loadWire(&d, wire);
    assert(decrs41_parse(&d, &info));
    assert(!strcmp(info.id, "S1234567"));
    assert((info.flags & (SONDE_HAS_ID | SONDE_HAS_POS | SONDE_HAS_ALT)) ==
           (SONDE_HAS_ID | SONDE_HAS_POS | SONDE_HAS_ALT));
    /* 48.8 N, 2.3 E, 1200 m: within 1" (31 m) and 5 m */
    assert(info.latS > 17567900 && info.latS < 17568100);
    assert(info.lonS > 827900 && info.lonS < 828100);
    assert(info.alt_m >= 1195 && info.alt_m <= 1205);
    assert(info.vs_dm >= 51 && info.vs_dm <= 53);
    /* 8 m/s east, 3 m/s south: 8.5 m/s towards 110.6 degrees */
    assert(info.hs_ms >= 8 && info.hs_ms <= 9);
    assert(info.hdg >= 109 && info.hdg <= 112);
    assert(info.batt_dv == 29u);

    initDec(&d);
    wire[0x112 + 4] ^= 1u;
    loadWire(&d, wire);
    memset(&info, 0, sizeof info);
    assert(decrs41_parse(&d, &info));
    assert(info.flags == SONDE_HAS_ID);

    makeFrame(clear, wire);
    initDec(&d);
    wire[0x39 + 4] ^= 1u;
    loadWire(&d, wire);
    assert(!decrs41_parse(&d, &info));

    /* Classic block ID broken: the multi-GNSS offset holds no 0x82 block. */
    makeFrame(clear, wire);
    initDec(&d);
    wire[0x112] ^= 1u;
    loadWire(&d, wire);
    memset(&info, 0, sizeof info);
    assert(decrs41_parse(&d, &info));
    assert(info.flags == SONDE_HAS_ID);

    /* Both position blocks valid: the classic one is read, as before. */
    startFrame(clear, "S1234567", 29);
    putGps(clear);
    putGnss(clear);
    whiten(clear, wire);
    initDec(&d);
    loadWire(&d, wire);
    memset(&info, 0, sizeof info);
    assert(decrs41_parse(&d, &info));
    assert(info.flags & SONDE_HAS_POS);
    assert(info.latS > 17567900 && info.latS < 17568100);
}

static void checkGnss(void)
{
    uint8_t clear[RS41_FRAME_LEN], wire[RS41_FRAME_LEN];
    decrs41_t d;
    sonde_info_t info = {0};
    makeGnssFrame(clear, wire);
    initDec(&d);
    loadWire(&d, wire);
    assert(decrs41_parse(&d, &info));
    assert(!strcmp(info.id, "X7654321"));
    assert((info.flags & (SONDE_HAS_ID | SONDE_HAS_POS | SONDE_HAS_ALT)) ==
           (SONDE_HAS_ID | SONDE_HAS_POS | SONDE_HAS_ALT));
    /* 45.2 N, 5.7 E, 2500 m: within 1" (31 m) and 5 m */
    assert(info.latS > 16271900 && info.latS < 16272100);
    assert(info.lonS > 2051900 && info.lonS < 2052100);
    assert(info.alt_m >= 2495 && info.alt_m <= 2505);
    assert(info.vs_dm >= -124 && info.vs_dm <= -122);
    /* 6 m/s west, 4 m/s north: 7.2 m/s towards 303.7 degrees */
    assert(info.hs_ms == 7);
    assert(info.hdg >= 302 && info.hdg <= 305);
    assert(info.batt_dv == 31u);

    /* A broken 0x82 block: the frame still counts, without a position. */
    initDec(&d);
    wire[0xA1 + 4] ^= 1u;
    loadWire(&d, wire);
    memset(&info, 0, sizeof info);
    assert(decrs41_parse(&d, &info));
    assert(info.flags == SONDE_HAS_ID);
}

/* Key polls only between the parsed blocks, 3 bytes of margin each side:
 * after STATUS (0x39..0x65), around the 0x82 block (0xA1..0xCB), before the
 * classic GPS block (0x112). */
static void checkHold(void)
{
    decrs41_t d;
    initDec(&d);
    assert(decrs41_holdOk(&d));        /* both slicers searching */
    d.sl[0].state = 1u;                /* ST_DATA */
    for (uint16_t b = 8; b < RS41_FRAME_LEN; b++) {
        bool in = (b >= 0x68u && b < 0x9Eu) || (b >= 0xCEu && b < 0x10Fu);
        d.sl[0].nbit = (uint16_t)(b * 8u);
        assert(decrs41_holdOk(&d) == in);
        d.sl[0].nbit = (uint16_t)(b * 8u + 7u);
        assert(decrs41_holdOk(&d) == in);
    }
}

static void feedBit(decrs41_t *d, uint8_t bit, bool inverted)
{
    uint16_t sample = (bit ^ inverted) ? 3200u : 800u;
    for (uint8_t i = 0; i < 4u; i++) decrs41_push(d, sample);
}

static void checkDemod(bool inverted, bool gnss)
{
    uint8_t clear[RS41_FRAME_LEN], wire[RS41_FRAME_LEN];
    decrs41_t d;
    sonde_info_t info = {0};
    if (gnss) makeGnssFrame(clear, wire);
    else makeFrame(clear, wire);
    initDec(&d);
    for (uint16_t i = 0; i < 320u; i++) feedBit(&d, i & 1u, inverted);
    for (uint16_t i = 0; i < RS41_FRAME_LEN; i++)
        for (uint8_t b = 0; b < 8u; b++)
            feedBit(&d, (uint8_t)((wire[i] >> b) & 1u), inverted);
    /* The last bit is read a sample or two after its end. */
    bool done = false;
    for (uint8_t i = 0; i < 8u; i++) done |= decrs41_push(&d, inverted ? 800u : 3200u);
    assert(done);
    assert(decrs41_parse(&d, &info));
    assert(!strcmp(info.id, gnss ? "X7654321" : "S1234567"));
    assert(info.flags & SONDE_HAS_POS);
}

/* The radio's clock off by permille/1000: a bit lasts 4 (1 + e) samples. The
 * DPLL learns that rate; a phase-only one read wrong bits from 2 % on. */
static void checkDemodClock(int permille)
{
    uint8_t clear[RS41_FRAME_LEN], wire[RS41_FRAME_LEN];
    const unsigned nbits = 320u + RS41_FRAME_LEN * 8u;
    const unsigned spb = (unsigned)(4000 + 4 * permille);   /* samples per bit x 1000 */
    decrs41_t d;
    sonde_info_t info = {0};
    bool done = false;
    makeFrame(clear, wire);
    initDec(&d);
    for (unsigned n = 0; !done; n++) {
        unsigned k = n * 1000u / spb;   /* the bit this sample falls in */
        uint8_t bit = k < 320u ? (uint8_t)(k & 1u)
                    : k < nbits ? (uint8_t)((wire[(k - 320u) >> 3] >> ((k - 320u) & 7u)) & 1u)
                    : 1u;
        done = decrs41_push(&d, bit ? 3200u : 800u);
        assert(k < nbits + 4u);
    }
    assert(decrs41_parse(&d, &info));
    assert(!strcmp(info.id, "S1234567"));
    assert(info.flags & SONDE_HAS_POS);
}

int main(void)
{
    checkParse();
    checkGnss();
    checkHold();
    checkDemod(false, false);
    checkDemod(true, false);
    checkDemod(false, true);
    checkDemod(true, true);
    checkDemodClock(-30);
    checkDemodClock(30);
    puts("RS41 decoder tests passed");
    return 0;
}
