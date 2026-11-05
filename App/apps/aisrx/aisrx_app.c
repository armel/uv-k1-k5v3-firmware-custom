/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * AIS RX - receive 9600 bit/s AIS GMSK from the BK4829 discriminator output.
 * The ADC runs at four samples per bit through the resident API level 3 audio
 * capture. An equaliser, two hysteresis slicers with their own clock recovery,
 * the NRZI decoder, HDLC de-stuffer and CRC run in this overlay; no AIS
 * support is added to the resident firmware. test/model_rx.py is the
 * executable specification of the receive chain.
 *
 * Supported messages: 1, 2, 3, 5, 18, 19, 24 and 8 Inland AIS ship static
 * data (DAC 200, FI 10: ENI, dimensions, load). Dynamic and static reports
 * for the same MMSI are merged in a four-vessel history. This application
 * listens to the current VFO only: AIS1 and AIS2 must be selected separately.
 * The screen follows ATIS RX and APRS RX: name in bold with the counters at
 * its right, then the body, nothing cut and scrolled as APRS RX's (MMSI and
 * status, position, speed, course and heading, destination, whole name),
 * the dotted separator, the frequency with the AIS channel at its right,
 * then level, reports and age. Key * switches the body to the tiny font
 * (APRS RX's compact view). Keys are read every 50 ms while no lane is
 * inside a training sequence or a frame; a key redraws at once, a report or
 * the 5 s refresh once the squelch closes (1 s at the latest).
 *
 * Keys (UV-K5 and UV-K1): UP/DOWN held scroll the body, pressed again at its
 *   first / last row the newer / older vessel · * normal / compact view
 *   (saved) · 1 speaker on/off (saved, off by default; not gated by the
 *   squelch, as the other RAW apps) · 2 clear · EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "aisrx_assets.h"

#define FS                 38400u
#define CYC_PER_SAMPLE     (48000000u / FS)
#define HOUSE_EVERY        1920u   /* samples between key/screen slots (50 ms) */
#define BUSY_MAX           20u     /* serve them anyway after 20 busy slots (1 s) */
#define REFRESH_SLOTS      100u    /* 100 slots = 5 s (battery, age)            */
#define BIAS_CODE          2048u

/* The data block app_main() copies onto its stack: every asset but the
 * status-bar tail (gen_assets.py), word records first. */
#define DATA_SIZE          (MSG_BYTES + MSG_BYTES_LEN)
#define REGS               (REG_SETUP_LEN / 6u)    /* {reg, clear, set} */
_Static_assert(AIS_FREQS == 0u && REG_SETUP == 8u && BMP_TAIL == DATA_SIZE,
               "gen_assets.py data block layout");

#define PHASE_STEP         16384
#define PHASE_CENTER       49152
#define PHASE_GAIN         2
#define LANES              2u

#define RAW_BITS           544u
#define RAW_BYTES          ((RAW_BITS + 7u) / 8u)
#define FRAME_BYTES        57u
#define HISTORY            4u

#define HAS_POS            1u
#define HAS_NAME           2u
#define HAS_DEST           4u
#define HAS_NAV            8u
#define HAS_INL            16u
#define REDRAW_KEY         1u
#define REDRAW_FRAME       2u

#define ROW_CHARS          18u     /* small font: 7 px a character, no clipping */
#define TINY_CHARS         32u     /* tiny font: 4 px a character, no clipping  */
#define SEEN_MAX           999u    /* keeps the bottom row within 32 characters */

#define WAIT_CAPS_X        40u     /* "WAIT" capsule in the status bar          */
#define SPK_X              59u     /* speaker icon, then the marks at x = 72-76 */
#define CAPS_END           127u    /* last column of the line-0 capsules        */
#define CH_X               108u    /* the AIS channel, where ATIS RX shows CH   */

/* sog, cog and hdg keep the AIS "not available" values (1023, 3600, 511):
 * HAS_NAV only says a position report came. st is the navigational status
 * plus one (types 1-3), 0 when unknown. */
typedef struct {
    uint32_t mmsi;
    uint32_t t;
    int32_t lat;
    int32_t lon;
    uint16_t sog;
    uint16_t cog;
    uint16_t hdg;
    uint16_t seen;
    int16_t rssi;
    uint8_t flags;
    uint8_t st;
    uint16_t len, beam, dr;            /* Inland: dm, dm, cm (0 = unknown)        */
    uint8_t inl;                       /* Inland: load (0-2) | hazard (0-5) << 2  */
    char eni[9];
    char name[21];
    char dest[21];
} vessel_t;


/* One slicer: hysteresis threshold, clock recovery and HDLC receiver. */
typedef struct {
    int32_t phase;
    uint32_t history;
    uint16_t raw_n;
    uint8_t num;
    bool level;
    bool sampled;
    bool collecting;
    uint8_t raw[RAW_BYTES];
} lane_t;

typedef struct {
    int32_t dc;
    int32_t peak;
    int32_t x[5];
    lane_t lane[LANES];
} demod_t;

static struct {
    const app_api_t *A;
    const char *ui;
    char *text;
    uint8_t prev_key;
    uint8_t count;
    uint8_t cur;
    uint8_t redraw;
    uint8_t refresh;
    uint8_t spk;                       /* saved settings, in cfg order: speaker, */
    uint8_t cw;                        /* compact view (key *)                   */
    bool running;
    uint16_t vrow;                     /* y of the next body row before scrolling */
    uint16_t top, lim;                 /* pixel scroll of the body, its limit    */
    uint16_t sync;
    uint16_t ok;
    vessel_t *h;                       /* HISTORY records on app_main's stack:
                                          .bss counts in the 4 KiB overlay */
    const uint8_t *msg;                /* the message get_bits() reads       */
} g;

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    while (n) p[--n] = (uint8_t)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n) { --n; p[n] = q[n]; }
    return d;
}

static char *put(char *o, const char *s)
{
    while (*s) *o++ = *s++;
    return o;
}

__attribute__((noinline))
static char *puti(char *o, int32_t v)
{
    return g.A->append_i32(o, v);
}

static uint32_t divr(uint32_t n, uint32_t d, uint32_t *r)
{
    uint64_t qr = g.A->uidivmod(n, d);
    *r = (uint32_t)(qr >> 32);
    return (uint32_t)qr;
}

/* v in exactly n digits, leading zeros kept (MMSI, decimals, age). */
__attribute__((noinline))
static char *putn(char *o, uint32_t v, unsigned n)
{
    for (unsigned i = n; i--; ) {
        uint32_t r;
        v = divr(v, 10u, &r);
        o[i] = (char)('0' + r);
    }
    return o + n;
}

static char *put1(char *o, uint32_t v)
{
    uint32_t r;
    o = puti(o, (int32_t)divr(v, 10u, &r));
    *o++ = '.';
    *o++ = (char)('0' + r);
    return o;
}

/* AIS coordinates use 1/10000 minute, or 600000 units per degree. Four
 * decimals (about 11 m), as the sonde apps: "48.8566N 122.3458W" always fits
 * one 18-character row. */
static char *put_pos(char *o, int32_t v, char pos, char neg)
{
    bool minus = v < 0;
    uint32_t a = (uint32_t)(minus ? -v : v);
    uint32_t rem;
    o = puti(o, (int32_t)divr(a, 600000u, &rem));
    *o++ = '.';
    o = putn(o, divr(rem, 60u, &a), 4);
    *o++ = minus ? neg : pos;
    return o;
}

/* The HDLC receiver shifts bits in LSB first, which rebuilds the message
 * bytes; AIS sends each byte LSB first but numbers its fields from the MSB
 * of byte zero (ITU-R M.1371), so fields are read MSB first. */
/* The message is g.msg, set once per frame: each of the ~20 call sites then
 * passes two small constants only. */
static uint32_t get_bits(unsigned start, unsigned width)
{
    const uint8_t *p = g.msg;
    uint32_t v = 0;
    while (width--) {
        v = (v << 1) | ((p[start >> 3] >> (7u - (start & 7u))) & 1u);
        start++;
    }
    return v;
}

static int32_t get_signed(unsigned start, unsigned width)
{
    uint32_t v = get_bits(start, width);
    uint32_t sign = 1u << (width - 1u);
    return (int32_t)((v ^ sign) - sign);
}

/* n six-bit characters (20 at most), '@' as space, trailing spaces cut. */
static void get_text(char *out, unsigned start, unsigned n)
{
    char *o = out;
    while (n--) {
        unsigned c = get_bits(start, 6u);
        start += 6u;
        c = c < 32u ? c + 64u : c;
        *o++ = c == '@' ? ' ' : (char)c;
    }
    while (o > out && o[-1] == ' ') o--;
    *o = '\0';
}

static vessel_t *select_vessel(uint32_t mmsi)
{
    vessel_t e;
    unsigned i = 0;
    while (i < g.count && g.h[i].mmsi != mmsi) i++;
    if (i < g.count) e = g.h[i];
    else {
        memset(&e, 0, sizeof e);
        e.mmsi = mmsi;
        if (g.count < HISTORY) g.count++;
        i = g.count - 1u;
    }
    while (i) { g.h[i] = g.h[i - 1u]; i--; }
    g.h[0] = e;
    return &g.h[0];
}

static bool valid_position(int32_t lat, int32_t lon)
{
    return lat >= -90 * 600000 && lat <= 90 * 600000 &&
           lon >= -180 * 600000 && lon <= 180 * 600000;
}

static void accept_message(const uint8_t *p, unsigned bytes)
{
    g.msg = p;
    /* The bytes each supported message must have, MSG_BYTES in the data
     * block (0: not supported). Inland (8) may be longer. Type 24 part A is
     * 160 bits in M.1371, but many Class B units pad it to 168: one byte
     * more, which part B must have. */
    unsigned type = get_bits(0, 6);
    unsigned want = type < MSG_BYTES_LEN ? (uint8_t)g.ui[MSG_BYTES + type] : 0u;
    unsigned extra = bytes - want;          /* wraps when shorter */
    if (!want || extra > (type == 8u ? 99u : (unsigned)(type == 24u))) return;
    unsigned part = 0;
    if (type == 24u) {
        part = get_bits(38, 2);
        if (part > 1u || (part && !extra)) return;
    }

    uint32_t mmsi = get_bits(8, 30);
    if (!mmsi) return;
    /* Message 8: Inland AIS ship static data (DAC 200, FI 10) only. gpsd has
     * seen false DAC/FI matches: the ENI must be eight digits (a random
     * match is about 3e-7) and the two codes that index T_INLAND in range,
     * before the vessel is touched. */
    char eni[9];
    unsigned len = 0, beam = 0, dr = 0, inl = 0;
    if (type == 8u) {
        if (get_bits(40, 16) != ((200u << 6) | 10u)) return;
        get_text(eni, 56, 8);
        unsigned k = 0;
        while (k < 8u && (unsigned)(eni[k] - '0') < 10u) k++;
        len = get_bits(104, 13);
        beam = get_bits(117, 10);
        unsigned haz = get_bits(141, 3);    /* blue cones, 4 B-flag, 5 unknown */
        dr = get_bits(144, 11);
        unsigned load = get_bits(155, 2);   /* 1 unloaded, 2 loaded (gpsd) */
        if (k < 8u || haz > 5u || load > 2u) return;
        inl = load | haz << 2;
    }
    vessel_t *e = select_vessel(mmsi);

    if (type <= 3u || type == 18u || type == 19u) {
        /* Class B (18, 19) has the Class A fields 4 bits earlier: its 8
         * reserved bits replace status (4) and rate of turn (8). */
        unsigned d = type > 3u ? 4u : 0u;
        int32_t lon = get_signed(61u - d, 28);
        int32_t lat = get_signed(89u - d, 27);
        if (valid_position(lat, lon)) {
            e->lon = lon; e->lat = lat; e->flags |= HAS_POS;
        }
        e->sog = (uint16_t)get_bits(50u - d, 10);
        e->cog = (uint16_t)get_bits(116u - d, 12);
        e->hdg = (uint16_t)get_bits(128u - d, 9);
        e->flags |= HAS_NAV;
        if (!d) {
            unsigned st = get_bits(38, 4);     /* 15: not defined */
            e->st = (uint8_t)(st < 15u ? st + 1u : 0u);
        }
        if (type == 19u) { get_text(e->name, 143, 20); e->flags |= HAS_NAME; }
    } else if (type == 5u) {
        get_text(e->name, 112, 20);
        get_text(e->dest, 302, 20);
        e->flags |= HAS_NAME | HAS_DEST;
    } else if (type == 8u) {
        memcpy(e->eni, eni, sizeof eni);
        e->len = (uint16_t)len; e->beam = (uint16_t)beam; e->dr = (uint16_t)dr;
        e->inl = (uint8_t)inl;
        e->flags |= HAS_INL;
    } else {
        if (!part) { get_text(e->name, 40, 20); e->flags |= HAS_NAME; }
        else if (!(e->flags & HAS_NAME)) {
            get_text(e->name, 90, 7);
            e->flags |= HAS_NAME;
        }
    }

    e->t = g.A->ticks_ms();
    e->rssi = g.A->rssi_dbm();
    if (e->seen < SEEN_MAX) e->seen++;
    g.cur = 0;                         /* show it, from its first row */
    g.top = 0;
    g.redraw |= REDRAW_FRAME;
}

static uint16_t crc16(const uint8_t *p, unsigned n)
{
    uint16_t crc = 0xFFFFu;
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8u; i++)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0x8408u : 0u));
    }
    return crc;
}

static bool finish_frame(const uint8_t *raw, unsigned raw_bits)
{
    uint8_t frame[FRAME_BYTES];
    memset(frame, 0, sizeof frame);
    unsigned out_n = 0, ones = 0;
    for (unsigned i = 0; i < raw_bits; i++) {
        unsigned b = (raw[i >> 3] >> (i & 7u)) & 1u;
        if (b) {
            if (++ones > 5u) return false;
        } else {
            if (ones == 5u) { ones = 0; continue; }
            ones = 0;
        }
        if (out_n >= FRAME_BYTES * 8u) return false;
        frame[out_n >> 3] |= (uint8_t)(b << (out_n & 7u));
        out_n++;
    }
    if ((out_n & 7u) || out_n < 24u) return false;
    unsigned n = out_n >> 3;
    if (crc16(frame, n) != 0xF0B8u) return false;
    g.ok++;
    accept_message(frame, n - 2u);
    return true;
}

static bool busy(const demod_t *m)
{
    for (unsigned i = 0; i < LANES; i++)
        if (m->lane[i].collecting) return true;
    return false;
}

/* A lane inside a frame, or whose last 8 bits alternate as the 24-bit
 * training does: a key scan there (~0.4 ms, 4 bits) would lose the burst. */
static bool in_burst(const demod_t *m)
{
    for (unsigned i = 0; i < LANES; i++) {
        unsigned top = m->lane[i].history >> 24;
        if (m->lane[i].collecting || top == 0xAAu || top == 0x55u) return true;
    }
    return false;
}

static void stop_lanes(demod_t *m)
{
    for (unsigned i = 0; i < LANES; i++) m->lane[i].collecting = false;
}

static void hdlc_bit(demod_t *m, lane_t *l, unsigned b)
{
    l->history = (l->history >> 1) | ((uint32_t)b << 31);
    if (l->collecting) {
        if (l->raw_n >= RAW_BITS) l->collecting = false;
        else {
            l->raw[l->raw_n >> 3] |= (uint8_t)(b << (l->raw_n & 7u));
            l->raw_n++;
        }
    }
    if ((l->history >> 24) != 0x7Eu) return;
    if (l->collecting) {
        l->collecting = false;
        /* The first lane with a valid frame ends the burst for both. */
        if (l->raw_n > 8u && finish_frame(l->raw, l->raw_n - 8u)) stop_lanes(m);
    } else {
        /* Use the last 12 training bits: enough to reject noise while
         * tolerating the GMSK filter and initial NRZI phase settling. */
        uint32_t sync = l->history & 0xFFFFF000u;
        if (sync == 0x7EAAA000u || sync == 0x7E555000u) {
            if (!busy(m)) g.sync++;
            memset(l->raw, 0, sizeof l->raw);
            l->raw_n = 0;
            l->collecting = true;
        }
    }
}

/* Four samples per bit. The receive audio high-pass (near 1 kHz) makes long
 * runs droop and its low-pass flattens single-bit runs, so a sign slicer
 * fails. The 3-tap equaliser, taps half a bit apart, gains x1 at DC, x2.8 at
 * 2.4 kHz and x7 at 4.8 kHz. Each lane's level then flips only beyond
 * +/- num/8 of the peak and holds in between, whatever the droop;
 * transitions pull its sampling point to mid-symbol. NRZI data is polarity
 * independent. */
static void demod_sample(demod_t *m, uint16_t adc)
{
    int32_t *x = m->x;
    m->dc += (((int32_t)adc << 8) - m->dc) >> 8;
    x[4] = x[3]; x[3] = x[2]; x[2] = x[1]; x[1] = x[0];
    x[0] = (int32_t)adc - (m->dc >> 8);
    int32_t y = 7 * x[2] - 3 * (x[0] + x[4]);
    int32_t a = y < 0 ? -y : y;
    if (a > m->peak) m->peak += (a - m->peak) >> 2;
    else m->peak -= m->peak >> 8;
    for (lane_t *l = m->lane; l < m->lane + LANES; l++) {
        int32_t th = (m->peak * l->num) >> 3;
        bool level = y > th ? true : y < -th ? false : l->level;
        l->phase += PHASE_STEP;
        if (level != l->level)
            l->phase += (PHASE_CENTER - l->phase) >> PHASE_GAIN;
        l->level = level;
        if (l->phase >= 65536) {
            l->phase -= 65536;
            hdlc_bit(m, l, level == l->sampled);
            l->sampled = level;
        }
    }
}

static void tiny(char *buf, uint8_t y, char *end)
{
    *end = '\0';
    g.A->print_tiny(buf, 0, y, false, true);
}

/* Body rows text..end, wrapped every 18 characters (32 in the compact view),
 * as APRS RX. A visible row is rendered on scratch page 4, then its columns
 * are ORed into lines 0-3 at its exact pixel offset, so 8-pixel and 6-pixel
 * rows scroll alike without another framebuffer. Rows may spill over line 0
 * and the separator: draw() redraws both afterwards. The scratch page is left
 * blank: display_clear() blanks it before the first row and each row clears
 * it after use. */
static char *emit(char *end)
{
    const app_api_t *A = g.A;
    char *p = g.text;
    while (p < end) {
        unsigned w = g.cw ? TINY_CHARS : ROW_CHARS;
        char *q = (unsigned)(end - p) > w ? p + w : end;
        char c = *q; *q = '\0';
        unsigned y = (unsigned)g.vrow - g.top;    /* huge above the screen */
        if (y - 1u < 30u) {             /* y = 1..30: higher rows lie under line 0 */
            uint8_t *scratch = A->fb[4];
            if (g.cw) A->print_tiny(p, 0, 32, false, true);
            else A->print_normal(p, 0, 0, 4);
            for (unsigned x = 0; x < 128u; x++) {
                uint32_t bits = (uint32_t)scratch[x] << y;
                for (uint8_t *out = A->fb[0] + x; bits; out += 128) {
                    *out |= (uint8_t)bits;
                    bits >>= 8;
                }
            }
            memset(scratch, 0, 128);
        }
        *q = c;
        g.vrow += g.cw ? 6u : 8u;
        p = q;
    }
    return g.text;                      /* the next row starts there */
}

/* Between two parts of a row ("SOG 12.3kt COG 088" / "HDG 090"): a new row
 * in the normal view, a space in the compact one. */
static char *brk(char *o)
{
    if (g.cw) { *o++ = ' '; return o; }
    return emit(o);
}

__attribute__((noinline))
static void draw(void)
{
    const app_api_t *A = g.A;
    char *s = g.text, *o;
    const vessel_t *e = &g.h[g.cur];   /* read only when there is a vessel */
    uint8_t *fb = A->fb[0];
    A->display_clear(); A->status_clear();
    g.vrow = 8;                         /* the body starts under line 0 */
    A->print_inverse(g.ui + T_TITLE, 2, 0, true, true,
                     (uint8_t)(2u + T_TITLE_CHARS * 4u));
    A->draw_battery();

    if (!g.count)                       /* no vessel yet: a capsule, as ATIS RX */
        A->print_inverse(g.ui + T_WAIT, WAIT_CAPS_X, 0, true, true,
                         (uint8_t)(WAIT_CAPS_X + T_WAIT_CHARS * 4u));
    else {
        /* Line 0 first, drawn last over the scrolled rows: at its right the
         * frames received and, once several vessels are kept, "2/4" at the
         * edge (the vessel shown / the vessels kept, 1 = newest); in bold the
         * name, cut before the capsules, else the MMSI. */
        char l0[22], nb[8];
        unsigned caps = g.count > 1u ? CAPS_END - 16u : CAPS_END;
        o = puti(nb, g.ok); *o = '\0';
        unsigned x0 = caps - 4u * (unsigned)(o - nb);
        const char *n = e->name;
        bool named = (e->flags & HAS_NAME) && *n;
        o = l0;
        if (named) {
            for (unsigned w = 7u; w < x0 && *n; w += 7u) *o++ = *n++;
        } else o = putn(l0, e->mmsi, 9);
        *o = '\0';

        /* Body, nothing cut, scrolled with UP/DOWN:
         *   "227123456 Moored"   the MMSI when line 0 shows the name, status
         *   "48.8566N 2.3522E"   position, four decimals (about 11 m)
         *   "SOG 12.3kt COG 088" speed, and course from 1 kt: below, it is
         *   "HDG 090"            GPS noise; the true heading (same row when
         *                        compact)
         *   ">LE HAVRE"          destination
         *   "ENI 01823456"       Inland AIS: ENI, then length x beam x
         *   "110.0x11.4x2.5m"    draught (same row when compact),
         *   "Loaded 2 cones"     load and dangerous cargo
         *   the whole name when line 0 cut it */
        o = s;
        if (named) o = putn(o, e->mmsi, 9);
        if (e->st) {
            if (o != s) *o++ = ' ';
            o = put(o, g.ui + T_STATUS + (e->st - 1u) * T_STATUS_STRIDE);
        }
        emit(o);
        if (e->flags & HAS_POS) {
            o = put_pos(s, e->lat, 'N', 'S'); *o++ = ' ';
            emit(put_pos(o, e->lon, 'E', 'W'));
        }
        if (e->flags & HAS_NAV) {
            unsigned sog = e->sog;
            uint32_t r;
            o = s;
            if (sog < 1023u) {
                o = put(o, g.ui + T_SOG);
                o = put1(o, sog);
                o = put(o, g.ui + T_KT);
            }
            if (sog - 10u < 1013u && e->cog < 3600u) {   /* 1.0 kt up to 102.2 */
                o = put(o, g.ui + T_COG);
                o = putn(o, divr(e->cog, 10u, &r), 3);
            }
            if (e->hdg < 360u) {
                if (o != s) o = brk(o);
                o = put(o, g.ui + T_HDG);
                o = putn(o, e->hdg, 3);
            }
            emit(o);
        }
        if ((e->flags & HAS_DEST) && e->dest[0]) {
            o = s; *o++ = '>';
            emit(put(o, e->dest));
        }
        /* Inland AIS: "ENI 01823456" (brk) "110.0x11.4x2.5m", length, beam
         * and draught; then "Loaded 2 cones" (blue cones, or B-flag) */
        if (e->flags & HAS_INL) {
            uint32_t r;
            o = put(s, g.ui + T_ENI); o = put(o, e->eni);
            if (e->len) {
                o = brk(o);
                o = put1(o, e->len); *o++ = 'x'; o = put1(o, e->beam);
                if (e->dr) { *o++ = 'x'; o = put1(o, divr(e->dr, 10u, &r)); }
                *o++ = 'm';
            }
            emit(o);
            unsigned load = e->inl & 3u, haz = e->inl >> 2;
            o = s;
            if (load) o = put(o, g.ui + T_INLAND + (load - 1u) * T_INLAND_STRIDE);
            if (haz - 1u < 4u) {
                if (o != s) *o++ = ' ';
                o = put(o, g.ui + T_INLAND + (haz + 1u) * T_INLAND_STRIDE);
            }
            emit(o);
        }
        if (named && *n) emit(put(s, e->name));

        /* line 0 over whatever the scroll brought there */
        memset(fb, 0, 128);
        A->print_bold(l0, 0, 0, 0);
        if (g.count > 1u) {
            o = s; *o++ = (char)('1' + g.cur); *o++ = '/';
            *o++ = (char)('0' + g.count); *o = '\0';
            A->print_inverse(s, CAPS_END - 12u, 0, false, true, CAPS_END);
        }
        A->print_inverse(nb, (uint8_t)x0, 0, false, true, (uint8_t)caps);
    }
    /* The last glyph ends one pixel before its row height: the scroll stops
     * with it right above the separator. */
    unsigned lim = g.vrow > 32u ? g.vrow - 32u : 0u;
    g.lim = (uint16_t)lim;
    /* Status bar from SPK_X: the speaker icon while on, an up mark while rows
     * or a newer vessel hide above, a down mark while rows or an older vessel
     * hide below: UP / DOWN has something to show. */
    unsigned tail = g.spk;
    if (g.top || g.cur) tail += 2u;
    if (g.top < lim || g.cur + 1u < g.count) tail += 4u;
    A->asset_read((uint16_t)(BMP_TAIL + tail * TAIL_W),
                  A->status_line + SPK_X, TAIL_W);

    /* dotted separator above the frequency: y = 31, bit 7 of line 3, over
     * whatever the scroll brought there */
    for (unsigned x = 0; x < 128u; x += 2u) {
        fb[384u + x] |= 0x80u;
        fb[385u + x] &= 0x7Fu;
    }
    uint32_t f = A->rx_freq();
    A->draw_frequency(f, 0, 4);
    /* the AIS channel, right of the frequency: "AIS" over 1 or 2 */
    const uint32_t *fq = (const uint32_t *)(const void *)(g.ui + AIS_FREQS);
    unsigned ch = f == fq[0] ? 1u : f == fq[1] ? 2u : 0u;
    if (ch) {
        A->print_tiny(g.ui + T_AIS, CH_X + 4u, 34, false, true);
        s[0] = (char)('0' + ch); s[1] = '\0';
        A->print_bold(s, CH_X, 127, 5);
    }

    /* bottom row: "-89dBm  heard 3  last 02m15", the level of the vessel
     * shown, its reports, the time since the last one: "02m15" (minutes,
     * seconds) under an hour, then "03h07" (hours, minutes). Without a
     * vessel, "sync 3  crc 2": bursts with training and flag, frames with a
     * valid CRC. */
    o = s;
    if (g.count) {
        uint32_t age = A->ticks_ms() - e->t, rem;
        uint32_t mn = divr(age, 60000u, &rem);
        o = puti(o, e->rssi); o = put(o, g.ui + T_DBM);
        o = puti(o, e->seen); o = put(o, g.ui + T_LAST);
        if (mn >= 60u) {
            uint32_t h = divr(mn, 60u, &mn);
            o = h < 100u ? putn(o, h, 2) : puti(o, (int32_t)h);
            *o++ = 'h';
            o = putn(o, mn, 2);
        } else {
            o = putn(o, mn, 2); *o++ = 'm';
            o = putn(o, divr(rem, 1000u, &age), 2);
        }
    } else {
        o = put(o, g.ui + T_SYNC); o = puti(o, g.sync);
        o = put(o, g.ui + T_CRC); o = puti(o, g.ok);
    }
    tiny(s, 49, o);
}

static void handle_key(void)
{
    uint8_t key = g.A->get_key();
    /* UP/DOWN, held: scroll the body 1 px per slot (20 px/s) while rows
     * overflow. Pressed again once at an end: the newer (UP) or older (DOWN)
     * vessel, from its first row; holding the key never leaves the vessel.
     * Raw KEY_UP is UP on K5 and LEFT on K1; KEY_DOWN is DOWN / RIGHT
     * (as APRS RX). */
    int d = (key == APP_KEY_DOWN) - (key == APP_KEY_UP);
    unsigned top = (unsigned)(g.top + d);    /* wraps past the first row */
    if (top <= g.lim) {
        if (top != g.top) { g.top = (uint16_t)top; g.redraw |= REDRAW_KEY; }
    } else if (key != g.prev_key && (unsigned)(g.cur + d) < g.count) {
        g.cur = (uint8_t)(g.cur + d);   /* the new key redraws below */
        g.top = 0;
    }
    if (key == APP_KEY_INVALID || key == g.prev_key) { g.prev_key = key; return; }
    g.prev_key = key;
    g.redraw |= REDRAW_KEY;
    if (key == APP_KEY_EXIT) g.running = false;
    else if (key == APP_KEY_1) g.spk = !g.spk;
    else if (key == APP_KEY_STAR) { g.cw = !g.cw; g.top = 0; }   /* saved on exit */
    else if (key == APP_KEY_2) {        /* g.lim: 0 at the redraw that follows */
        g.count = g.cur = 0;
        g.top = 0;
        g.sync = g.ok = 0;
    }
}

static void house(void)
{
    if (!g.running) return;
    if (g.redraw & REDRAW_FRAME) g.A->backlight_on();
    g.A->backlight_update();
    if (++g.refresh >= REFRESH_SLOTS) g.redraw |= REDRAW_KEY;
    if (!g.redraw) return;
    g.redraw = 0; g.refresh = 0;
    g.A->battery_sample();
    draw();
    g.A->blit_status(); g.A->blit_full();
}

__attribute__((noinline))
static void listen(void)
{
    demod_t m;
    memset(&m, 0, sizeof m);
    m.dc = BIAS_CODE << 8;
    m.lane[0].num = 1;
    m.lane[1].num = 3;
    uint32_t (*const clk)(void) = g.A->cycles_48mhz;
    uint16_t (*const sample)(void) = g.A->af_capture_sample;
    uint32_t next = clk();
    unsigned cnt = 0, busy_for = 0;
    g.redraw = REDRAW_KEY;
    while (g.running) {
        while ((int32_t)(clk() - next) < 0) {}
        next += CYC_PER_SAMPLE;
        demod_sample(&m, sample());
        if (++cnt < HOUSE_EVERY) continue;
        cnt = 0;
        /* Speaker amplifier as key 1 left it, not gated by the squelch, as
         * POCSAG RX, EPIRB 406 and the sonde apps on the same RAW path: the
         * hiss tells the audio is there and a burst is heard as the short
         * buzz it is (a 26 ms burst falls between two squelch reads 50 ms
         * apart). Ahead of the burst test: a frame keeps a lane busy. */
        g.A->audio_path(g.spk);
        bool carrier = (g.A->bk_read(0x0Cu) & 2u) != 0;
        if (in_burst(&m) && ++busy_for < BUSY_MAX) continue;
        /* Keys every slot outside a burst, whatever the squelch (SQL 0 keeps
         * it open). A key redraws at once; a report or the refresh waits for
         * the squelch to close, up to 1 s, so it does not fall on a burst. */
        handle_key();
        if (!carrier || (g.redraw & REDRAW_KEY) || ++busy_for >= BUSY_MAX) {
            busy_for = 0;
            house();
            stop_lanes(&m);
        }
        next = clk();                   /* skip the samples the slot took */
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    char ui[DATA_SIZE] __attribute__((aligned(4)));
    char text[48];                     /* longest body row: 27 characters */
    vessel_t h[HISTORY];
    uint16_t saved[REGS];
    g.A = api; g.ui = ui; g.text = text; g.h = h; g.prev_key = APP_KEY_INVALID;
    api->asset_read(0, ui, DATA_SIZE);

    api->af_capture_open();
    api->backlight_on();
    /* Receive set-up from the data block: each register saved, then written
     * as (saved & ~clear) | set; restored in reverse order on exit. */
    const uint16_t *rs = (const uint16_t *)(const void *)(ui + REG_SETUP);
    for (unsigned i = 0; i < REGS; i++) saved[i] = api->bk_read((uint8_t)rs[3u * i]);
    api->set_af(APP_AF_FM);
    for (unsigned i = 0; i < REGS; i++)
        api->bk_write((uint8_t)rs[3u * i],
                      (uint16_t)((saved[i] & ~rs[3u * i + 1u]) | rs[3u * i + 2u]));
    api->cfg_load(&g.spk, 2);          /* speaker, view; erased flash: off, normal */
    g.spk = g.spk == 1u;
    g.cw = g.cw == 1u;
    api->delay_ms(50);
    g.running = true;
    listen();

    api->audio_path(false);
    api->set_af(APP_AF_MUTE);
    for (unsigned i = REGS; i--; ) api->bk_write((uint8_t)rs[3u * i], saved[i]);
    api->af_capture_close();
    api->cfg_save(&g.spk, 2);
}
