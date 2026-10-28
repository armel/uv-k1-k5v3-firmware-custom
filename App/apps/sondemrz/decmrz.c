/* MRZ-N1 / MP3-H1 decoder.
 *
 * The BK4829 RAW audio path is high-passed: a sign or integrate-and-dump
 * slicer loses the FSK level between edges. As in the RS41 receiver, two
 * adaptive hysteresis slicers hold the level through that droop. Each has a
 * second-order DPLL which learns the sample-clock error. The payload is
 * Manchester coded at 2400 chips/s; the 44-chip sync word covers the extra
 * 0xAA preamble, logical 0xAA, and the first six bits of 0xBF.
 */
#include "decmrz.h"

#define FS         19200u
#define BAUD       2400u
#define STEP       ((int32_t)((65536u * BAUD) / FS))
#define PLL_CTR    40960
#define PLL_SHIFT  2
#define FREQ_SHIFT 8
#define STEP_MAX   410
#define DC_SH      8
#define AMP_DECAY  8
enum { ST_SRCH = 0, ST_DATA = 1, ST_DONE = 2 };

#define RAW_HDR0 0x99999999u
#define RAW_HDR1 0x09AAu

static uint16_t le16u(const uint8_t *p)
{ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static int16_t le16(const uint8_t *p) { return (int16_t)le16u(p); }
static int32_t le32(const uint8_t *p)
{
    return (int32_t)(p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static uint32_t le32u(const uint8_t *p) { return (uint32_t)le32(p); }

static uint16_t crc16(const uint8_t *p, unsigned n)
{
    uint16_t crc = 0xFFFFu;
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8u; i++)
            crc = (uint16_t)((crc >> 1) ^
                             ((uint16_t)-(int16_t)(crc & 1u) & 0xA001u));
    }
    return crc;
}

static bool near44(uint32_t a, uint16_t b)
{
    unsigned n = 0;
    while (a) { a &= a - 1u; if (++n > MRZ_SYNC_ERR) return false; }
    while (b) { b &= (uint16_t)(b - 1u); if (++n > MRZ_SYNC_ERR) return false; }
    return true;
}

static void slRearm(mrzsl_t *s)
{
    s->state = ST_SRCH;
    s->nbit = 0;
}

static bool slChip(mrzsl_t *s, uint8_t chip)
{
    if (s->state == ST_SRCH) {
        uint32_t x0;
        uint16_t x1;
        s->sr0 = (s->sr0 << 1) | (s->sr1 >> 11);
        s->sr1 = (uint16_t)(((s->sr1 << 1) | chip) & 0x0FFFu);
        if (s->nbit < 44u) { s->nbit++; return false; }
        x0 = s->sr0 ^ RAW_HDR0;
        x1 = (uint16_t)(s->sr1 ^ RAW_HDR1);
        if (near44(x0, x1)) s->inv = 0;
        else if (near44(~x0, (uint16_t)(~x1 & 0x0FFFu))) s->inv = 1u;
        else return false;
        s->sr1 = 0x2Fu;
        s->nbit = 14u;
        s->needBits = MRZ_FRAME_MAX * 8u;
        s->half = 0;
        s->state = ST_DATA;
        return false;
    }
    chip ^= s->inv;
    if (!s->half) { s->half = (uint8_t)(chip + 1u); return false; }
    uint8_t first = (uint8_t)(s->half - 1u);
    s->half = 0;
    if (first == chip) { slRearm(s); return false; }
    s->sr1 = (uint16_t)((s->sr1 << 1) | first);
    if (!(++s->nbit & 7u)) s->frm[(s->nbit - 1u) >> 3] = (uint8_t)s->sr1;
    /* Byte 31 completes the format discriminator. */
    if (s->nbit == 256u && s->frm[30] == 0xFFu && s->frm[31] == 0xFFu)
        s->needBits = 47u * 8u;
    if (s->nbit < s->needBits) return false;
    s->state = ST_DONE;
    return true;
}

void decmrz_init(decmrz_t *d)
{
    uint8_t *p = (uint8_t *)d;
    /* arc[] is loaded from assets immediately after init. */
    for (unsigned i = 0; i < sizeof *d - sizeof d->arc; i++) p[i] = 0;
    d->dcQ8 = -1;
    for (unsigned k = 0; k < MRZ_NSL; k++)
        d->sl[k].step = STEP;
    d->lastSec = 0xFFu;
}

void decmrz_rearm(decmrz_t *d)
{
    slRearm(&d->sl[0]);
    slRearm(&d->sl[1]);
}

bool decmrz_busy(const decmrz_t *d)
{
    return d->sl[0].state == ST_DATA || d->sl[1].state == ST_DATA;
}

bool decmrz_push(decmrz_t *d, uint16_t sample)
{
    int32_t xq = (int32_t)sample << 8, y0, y, a;
    bool done = false;
    if (d->dcQ8 < 0) d->dcQ8 = xq;
    d->dcQ8 += (xq - d->dcQ8) >> DC_SH;
    y0 = xq - d->dcQ8;
    y = y0 + ((y0 - d->yPrev) >> 1);
    d->yPrev = y0;
    a = y < 0 ? -y : y;
    if (a > d->amp) d->amp += (a - d->amp) >> 2;
    else d->amp -= d->amp >> AMP_DECAY;
    for (unsigned k = 0; k < MRZ_NSL; k++) {
        mrzsl_t *s = &d->sl[k];
        int32_t threshold;
        uint8_t level;
        if (s->state == ST_DONE) { done = true; continue; }
        threshold = d->amp >> (2u - k);
        level = s->lev;
        if (y > threshold) level = 1u;
        else if (y < -threshold) level = 0;
        s->phase += s->step;
        if (level != s->lev) {
            int32_t err = PLL_CTR - s->phase;
            int32_t step;
            if (err > 32768) err -= 65536;
            else if (err < -32768) err += 65536;
            s->phase += err >> PLL_SHIFT;
            step = s->step + (err >> FREQ_SHIFT);
            if (step < STEP - STEP_MAX) step = STEP - STEP_MAX;
            else if (step > STEP + STEP_MAX) step = STEP + STEP_MAX;
            s->step = step;
            s->lev = level;
        }
        if (s->phase < 65536) continue;
        s->phase -= 65536;
        if (slChip(s, level)) done = true;
    }
    return done;
}

static void serial(decmrz_t *d, const uint8_t *f, unsigned tail, sonde_info_t *out)
{
    uint8_t sub = f[3] & 0x0Fu;
    uint32_t v = le32u(f + tail);
    if (sub == 0x0Cu) {
        d->snC = v; d->serialMask |= 1u;
    } else if (sub == 0x0Du) {
        d->snD = v; d->serialMask |= 2u;
    }
    out->id[0] = 'M'; out->id[1] = 'R'; out->id[2] = 'Z'; out->id[3] = 0;
    if (d->serialMask == 3u) {
        uint32_t n = (d->snC * 100000u + d->snD) & 0xFFFFFFu;
        for (unsigned i = 0; i < 6u; i++) {
            uint8_t x = (uint8_t)((n >> ((5u - i) * 4u)) & 15u);
            if (x >= 10u) x = (uint8_t)(x + ('A' - '0' - 10));
            out->id[3u + i] = (char)('0' + x);
        }
        out->id[9] = 0;
        out->flags |= SONDE_HAS_ID;
    }
}

static int32_t atan2Arc(int32_t y, int32_t x, const uint32_t *arc,
                        uint32_t *mag)
{
    int32_t angle = 0;
    if (!x && !y) { *mag = 0; return 0; }
    if (x < 0) {
        angle = y >= 0 ? 64800000 : -64800000;
        x = -x; y = -y;
    }
    for (unsigned i = 0; i < MRZ_ARC_N; i++) {
        int32_t ox = x;
        if (y > 0) {
            x += y >> i; y -= ox >> i; angle += (int32_t)arc[i];
        } else {
            x -= y >> i; y += ox >> i; angle -= (int32_t)arc[i];
        }
    }
    /* Reciprocal CORDIC gain, within 0.25 ppm. */
    *mag = (uint32_t)((x >> 1) + (x >> 3) - (x >> 6) - (x >> 9) -
                      (x >> 13) - (x >> 14) + (x >> 16) - (x >> 20));
    return angle;
}

#define EPS_C0 139012u
#define EPS_CM 1424

static bool ecef(const uint8_t *p, sonde_info_t *out, const uint32_t *arc)
{
    int32_t x = le32(p), y = le32(p + 4), z = le32(p + 8);
    int32_t xu = x >> 14, yu = y >> 14, zu = z >> 14;
    int32_t geocentric, alt;
    uint32_t hCm, rCm, speedCm;
    uint32_t hu, ru, h2, z2, r2;
    uint32_t azu = (uint32_t)(zu < 0 ? -zu : zu);
    uint32_t radiusM, q, s2, corr, delta;

    out->lonS = atan2Arc(y, x, arc, &hCm);
    geocentric = atan2Arc(z, (int32_t)hCm, arc, &rCm);
    radiusM = (rCm + 50u) / 100u;
    hu = hCm >> 14; ru = rCm >> 14;
    if (!ru) return false;
    h2 = hu * hu; z2 = azu * azu; r2 = h2 + z2;
    q = (azu << 15) / ru;
    s2 = (q * q) >> 15;
    corr = (s2 * (21493u - ((108u * s2) >> 15))) >> 15;
    alt = (int32_t)radiusM - (int32_t)(6378137u - corr);
    if ((uint32_t)(alt + 1000) > 81000u) return false;
    delta = azu * hu /
            ((r2 + (z2 >> 16) * 442u) /
             (EPS_C0 - (uint32_t)((alt * EPS_CM) >> 16)));
    out->latS = geocentric + (z < 0 ? -(int32_t)delta : (int32_t)delta);
    out->alt_m = alt;
    {
        int32_t vx = le16(p + 12), vy = le16(p + 14), vz = le16(p + 16);
        int32_t dot = xu * vx + yu * vy + zu * vz;
        int32_t x4 = xu >> 2, y4 = yu >> 2, z4 = zu >> 2;
        int32_t h4 = (int32_t)(hu >> 2);
        out->vs_dm = (int16_t)(dot / (int32_t)(ru * 10u));
        out->hs_ms = 0; out->hdg = 0;
        if (h4) {
            int32_t ve = (x4 * vy - y4 * vx) / h4;
            int32_t vr = (x4 * vx + y4 * vy) / h4;
            int32_t vn = (h4 * vz - z4 * vr) / (int32_t)(ru >> 2);
            int32_t a = atan2Arc(ve, vn, arc, &speedCm);
            uint32_t hs = (speedCm + 50u) / 100u;
            out->hs_ms = (uint8_t)(hs > 255u ? 255u : hs);
            if (a < 0) a += 129600000;
            a = (a + 180000) / 360000;
            if (a >= 360) a = 0;
            out->hdg = (uint8_t)a;
            out->flags |= (uint8_t)((a >> 8) * SONDE_HDG_256);
        }
    }
    out->flags |= SONDE_HAS_POS | SONDE_HAS_ALT | SONDE_HAS_VEL;
    return true;
}

static bool frameOk(decmrz_t *d, const uint8_t *f, sonde_info_t *out)
{
    bool ll;
    unsigned tail, crclen;
    out->flags = 0;
    if (f[1] != 0xBFu || f[2] != 0x35u) return false;
    ll = f[30] == 0xFFu && f[31] == 0xFFu;
    tail = ll ? 41u : 44u;
    crclen = tail + 1u;
    if (crc16(f + 3, crclen) != le16u(f + 3u + crclen)) return false;
    if (ll) {
        int32_t lat = le32(f + 7), lon = le32(f + 11), alt = le32(f + 15) / 100;
        int16_t speed = le16(f + 19);
        uint32_t hs;
        uint16_t hdg;
        out->latS = lat * 9 / 25;
        out->lonS = lon * 9 / 25;
        out->alt_m = alt;
        out->flags = SONDE_HAS_ALT | (lat || lon ? SONDE_HAS_POS : 0u);
        hs = (uint32_t)(speed < 0 ? -speed : speed);
        hs = (hs + 50u) / 100u;
        out->hs_ms = (uint8_t)(hs > 255u ? 255u : hs);
        hdg = (uint16_t)((le16u(f + 21) + 50u) / 100u);
        if (hdg >= 360u) hdg = 0;
        out->hdg = (uint8_t)hdg;
        out->vs_dm = 0;
        out->flags |= (uint8_t)(SONDE_HAS_VEL | (hdg >> 8) * SONDE_HDG_256);
    } else if (!ecef(f + 8, out, d->arc)) return false;
    {
        int16_t t = le16(f + tail - 15u);
        int16_t rh = le16(f + tail - 13u);
        if ((uint16_t)(t + 10000) <= 18000u) {
            out->temp_c10 = (int16_t)((t + (t < 0 ? -5 : 5)) / 10);
            out->flags |= SONDE_HAS_TEMP;
        }
        if ((uint16_t)rh <= 10000u) {
            out->rh = (uint8_t)((rh + 50) / 100);
            out->flags |= SONDE_HAS_RH;
        }
    }
    serial(d, f, tail, out);
    return true;
}

bool decmrz_parse(decmrz_t *d, sonde_info_t *out)
{
    for (unsigned k = 0; k < MRZ_NSL; k++) {
        mrzsl_t *s = &d->sl[k];
        if (s->state != ST_DONE) continue;
        if (frameOk(d, s->frm, out)) {
            bool fresh = s->frm[6] != d->lastSec;
            d->lastSec = s->frm[6];
            decmrz_rearm(d);
            return fresh;
        }
        slRearm(s);
    }
    return false;
}
