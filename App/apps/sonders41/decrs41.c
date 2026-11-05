/* Vaisala RS41 decoder.
 *
 * RS41 sends 4800 bit/s NRZ/GFSK, least-significant bit first. The standard
 * burst is 320 bytes including the 8-byte sync. Bytes after reception are
 * de-whitened with the protocol's repeating 64-byte mask. This compact first
 * version validates the STATUS and GPS sub-block CRCs but deliberately omits
 * Reed-Solomon correction to stay inside the 4 KiB overlay. Two layouts
 * share the STATUS block at 0x39: the classic one (GPS position 0x7B at
 * 0x112) and the multi-GNSS firmware (rs41mod: fw 0x50dd), which drops the
 * GPS blocks for a 0x82 position block at 0xA1.
 *
 * The RAW audio path cuts the low frequencies (corner near 1 kHz, as found
 * with POCSAG RX): a high-passed NRZ level droops back to zero within a few
 * bits, so a level (sign) decision fails on the runs of the sync word. Each
 * slicer instead flips its level when the signal passes +/- num/8 of its
 * peak and holds it in between. Two slicers run, at 2/8 and 4/8: in the
 * channel model (test/model_rx.py) they cover a corner from 30 Hz to 1.5 kHz
 * between them, and those two are the ones that read POCSAG on the radio.
 * Each slicer has its own DPLL, pulled by its level changes, and looks for
 * the 64-bit sync word on hard bits (either polarity, a few wrong bits
 * allowed): a click or the start of a carrier can no longer pass for a sync,
 * which held the decoder busy and the keys unread for half a second each.
 *
 * The sample clock is the MCU's internal oscillator, good to a few percent
 * from one radio to another. A phase-only DPLL lags behind such a rate error
 * and reads near the bit edges: in the model it failed from 2 % on. Each DPLL
 * therefore also learns the bit rate (second order): its step follows the
 * phase error at every level change, within +/-5 %. The model reads the frame
 * from -4 % to +4 %, the noise sweep is unchanged.
 */
#include "decrs41.h"

#define FS        19200u
#define STEP      ((int32_t)((65536u * RS41_BAUD) / FS))  /* 4 samples per bit */
/* A level change pulls the phase toward 3/4: the bit is read one sample
 * after the change the loop locks on (best phase in the model sweep). */
#define PLL_CTR   49152
#define PLL_SHIFT 2
/* The step (bit rate) gain, and its bound: 5 % of STEP. */
#define FREQ_SHIFT 8
#define STEP_MAX  820
#define DC_SH     8
#define AMP_DECAY 8
enum { ST_SRCH = 0, ST_DATA = 1, ST_DONE = 2 };

/* On-air sync 10 B6 CA 11 22 96 12 F8, first bit in bit 0. */
#define SYNC0 0x11CAB610u
#define SYNC1 0xF8129622u
/* At most RS41_SYNC_ERR bits set in the 64 bits a:b. */
static bool near64(uint32_t a, uint32_t b)
{
    unsigned n = 0;
    while (a) { a &= a - 1u; if (++n > RS41_SYNC_ERR) return false; }
    while (b) { b &= b - 1u; if (++n > RS41_SYNC_ERR) return false; }
    return true;
}

static void slRearm(rs41sl_t *s)
{
    s->state = ST_SRCH;
    s->nbit = 0;
}

/* One bit from a slicer; true when its frame is complete. */
static bool slBit(rs41sl_t *s, uint8_t bit)
{
    if (s->state == ST_SRCH) {
        uint32_t x0, x1;
        s->sr0 = (s->sr0 >> 1) | (s->sr1 << 31);
        s->sr1 = (s->sr1 >> 1) | ((uint32_t)bit << 31);
        if (s->nbit < 64u) { s->nbit++; return false; }
        x0 = s->sr0 ^ SYNC0;
        x1 = s->sr1 ^ SYNC1;
        if (near64(x0, x1)) s->inv = 0;
        else if (near64(~x0, ~x1)) s->inv = 1u;
        else return false;
        s->state = ST_DATA;            /* nbit = 64: the data start at byte 8 */
        return false;
    }
    bit ^= s->inv;
    if (!(s->nbit & 7u)) s->frm[s->nbit >> 3] = 0;
    if (bit) s->frm[s->nbit >> 3] |= (uint8_t)(1u << (s->nbit & 7u));
    if (++s->nbit < RS41_FRAME_LEN * 8u) return false;
    s->state = ST_DONE;
    return true;
}

void decrs41_init(decrs41_t *d)
{
    uint8_t *p = (uint8_t *)d;
    for (unsigned i = 0; i < sizeof *d; i++) p[i] = 0;
    d->dcQ8 = -1;
    for (unsigned k = 0; k < RS41_NSL; k++) {
        d->sl[k].num = (uint8_t)(2u << k);
        d->sl[k].step = STEP;
    }
}

void decrs41_rearm(decrs41_t *d)
{
    for (unsigned k = 0; k < RS41_NSL; k++) slRearm(&d->sl[k]);
}

bool decrs41_busy(const decrs41_t *d)
{
    for (unsigned k = 0; k < RS41_NSL; k++)
        if (d->sl[k].state == ST_DATA) return true;
    return false;
}

/* No slicer reading a frame, or each one inside the bytes the parser ignores,
 * between the STATUS block (ends at 0x65) and the classic GPS block (starts
 * at 0x112), less the multi-GNSS position block (0xA1 to 0xCB), 3 bytes of
 * margin each: a held sample costs one bit at most there. With two windows,
 * the longest stretch without a key poll stays the 0x60 bytes from the sync
 * word to HOLD_LO, as with the classic layout alone. */
#define HOLD_LO     0x68u
#define HOLD_HI     0x10Fu
#define HOLD_GAP_LO 0x9Eu
#define HOLD_GAP_HI 0xCEu
bool decrs41_holdOk(const decrs41_t *d)
{
    for (unsigned k = 0; k < RS41_NSL; k++) {
        const rs41sl_t *s = &d->sl[k];
        unsigned b = (unsigned)s->nbit >> 3;  /* byte being read */
        if (s->state == ST_DATA && (b - HOLD_LO >= HOLD_HI - HOLD_LO ||
                                    b - HOLD_GAP_LO < HOLD_GAP_HI - HOLD_GAP_LO))
            return false;
    }
    return true;
}

bool decrs41_push(decrs41_t *d, uint16_t sample)
{
    int32_t xq = (int32_t)sample << 8, y0, y, a;
    bool done = false;
    if (d->dcQ8 < 0) d->dcQ8 = xq;
    d->dcQ8 += (xq - d->dcQ8) >> DC_SH;
    y0 = xq - d->dcQ8;
    /* Edge lift, y0 + (y0 - previous) / 2: a radio whose audio low-pass sits
     * lower rounds an isolated bit off before the slicers see it (two or
     * three wrong bits per block on one radio, M10/M20 fine). In the model
     * it reads more frames from a 2.7 kHz low-pass and loses none at 3.3. */
    y = y0 + ((y0 - d->yPrev) >> 1);
    d->yPrev = y0;
    a = y < 0 ? -y : y;
    if (a > d->amp) d->amp += (a - d->amp) >> 2;
    else d->amp -= d->amp >> AMP_DECAY;
    for (unsigned k = 0; k < RS41_NSL; k++) {
        rs41sl_t *s = &d->sl[k];
        int32_t t;
        uint8_t lev;
        if (s->state == ST_DONE) { done = true; continue; }
        t = (d->amp >> 3) * s->num;
        lev = s->lev;                  /* holds between the thresholds */
        if (y > t) lev = 1u;
        else if (y < -t) lev = 0;
        s->phase += s->step;
        if (lev != s->lev) {
            int32_t err = PLL_CTR - s->phase, st;
            if (err > 32768) err -= 65536;
            s->phase += err >> PLL_SHIFT;
            st = s->step + (err >> FREQ_SHIFT);
            if (st < STEP - STEP_MAX) st = STEP - STEP_MAX;
            else if (st > STEP + STEP_MAX) st = STEP + STEP_MAX;
            s->step = st;
            s->lev = lev;
        }
        if (s->phase < 65536) continue;
        s->phase -= 65536;
        if (slBit(s, lev)) done = true;
    }
    return done;
}

/* CRC-16/CCITT-FALSE in a word: bit 16 is the carry out, no uxth per bit. */
static uint16_t crc16(const uint8_t *p, unsigned len)
{
    unsigned crc = 0xFFFFu;
    while (len--) {
        crc ^= (unsigned)*p++ << 8;
        for (unsigned b = 0; b < 8u; b++) {
            crc <<= 1;
            if (crc & 0x10000u) crc ^= 0x11021u;
        }
    }
    return (uint16_t)crc;
}

/* pos is one of the fixed block offsets, all well inside the frame. */
static bool blockOk(const uint8_t *f, unsigned pos, unsigned id, unsigned minLen)
{
    unsigned len, end;
    if (f[pos] != id) return false;
    len = f[pos + 1u];
    end = pos + 2u + len;
    if (len < minLen || end + 2u > RS41_FRAME_LEN) return false;
    return crc16(f + pos + 2u, len) == (f[end] | ((unsigned)f[end + 1u] << 8));
}

static int32_t le32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int32_t le16(const uint8_t *p)
{
    return (int16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t isqrt32(uint32_t n)
{
    uint32_t root = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

/* CORDIC atan2, result in centi-arcseconds (0.01", 0.3 m on the ground);
 * arc = atan(2^-i) (from the assets). |x|, |y| up to 2^31 / 1.65 (the CORDIC
 * gain): the positions in cm fit. */
static int32_t atan2Arc(int32_t y, int32_t x, const uint32_t *arc)
{
    int32_t angle = 0;
    if (!x && !y) return 0;
    if (x < 0) {
        angle = y >= 0 ? 64800000 : -64800000;
        x = -x;
        y = -y;
    }
    for (unsigned i = 0; i < RS41_ARC_N; i++) {
        int32_t ox = x;
        if (y > 0) {
            x += y >> i;
            y -= ox >> i;
            angle += (int32_t)arc[i];
        } else {
            x -= y >> i;
            y += ox >> i;
            angle -= (int32_t)arc[i];
        }
    }
    return angle;
}

/* Geodetic latitude = geocentric + atan(eps s c / (1 + eps s^2))
 *                   = geocentric + eps z p / (p^2 + (1 + eps) z^2)
 * with eps = w / (1 - w), w = e^2 N / (N + h): exact, the atan dropped (below
 * 1e-8 rad). EPS_C0 - (h EPS_CM >> 16) is eps in centi-arcseconds per radian,
 * linear in h at the mean N. The (1 + eps) z^2 term uses eps = 442 / 2^16. */
#define EPS_C0 139012u
#define EPS_CM 1424

static bool ecef(const uint8_t *p, sonde_info_t *out, const uint32_t *arc)
{
    int32_t x = le32(p), y = le32(p + 4), z = le32(p + 8);
    int32_t x100 = x / 10000, y100 = y / 10000, z100 = z / 10000;
    uint32_t az100 = (uint32_t)(z100 < 0 ? -z100 : z100);
    /* In 100 m every square and their sum fit a uint32 (the unsigned
     * product squares a negative value as well). */
    uint32_t h2 = (uint32_t)x100 * (uint32_t)x100 + (uint32_t)y100 * (uint32_t)y100;
    uint32_t z2 = az100 * az100, r2 = h2 + z2;
    uint32_t horizontal = isqrt32(h2), r100 = isqrt32(r2);
    /* The radius and the distance to the polar axis to the centimetre,
     * without 64-bit arithmetic: one Newton step from the 100 m roots, each
     * position split as 10000 x100 + remainder (cm, same sign, so every term
     * is positive), the squared remainders dropped (under 1 cm). */
    uint32_t pxy = (uint32_t)(x100 * (x - x100 * 10000)) +
                   (uint32_t)(y100 * (y - y100 * 10000));
    uint32_t pr = pxy + (uint32_t)(z100 * (z - z100 * 10000));
    uint32_t radiusM = r100 * 100u + (5000u * (r2 - r100 * r100) + pr) / (r100 * 100u);
    int32_t pCm = (int32_t)(horizontal * 10000u +
                            (5000u * (h2 - horizontal * horizontal) + pxy) / horizontal);
    uint32_t q, s2, corr, d;
    int32_t alt;
    if (radiusM < 6300000u || radiusM > 6450000u) return false;
    /* Ellipsoid radius at the geocentric latitude, to the second order:
     * a - a e'^2/2 s^2 + 3 a e'^4/8 s^4 (s: its sine, Q15). */
    q = (az100 << 15) / (radiusM / 100u);
    s2 = (q * q) >> 15;
    corr = (s2 * (21493u - ((108u * s2) >> 15))) >> 15;
    alt = (int32_t)radiusM - (int32_t)(6378137u - corr);
    if (alt < -1000 || alt > 50000) return false;
    out->lonS = atan2Arc(y, x, arc);
    d = az100 * horizontal / ((r2 + (z2 >> 16) * 442u) / (EPS_C0 - (uint32_t)((alt * EPS_CM) >> 16)));
    out->latS = atan2Arc(z, pCm, arc) + (z < 0 ? -(int32_t)d : (int32_t)d);
    out->alt_m = alt;
    /* ECEF velocity (cm/s) projected on the local vertical: x100.v is in
     * 100 m.cm/s, so dividing by the radius in 10 m gives dm/s. It fits an
     * int32 below 330 m/s. */
    int32_t vx = le16(p + 12), vy = le16(p + 14), vz = le16(p + 16);
    {
        int32_t dot = x100 * vx + y100 * vy + z100 * vz;
        out->vs_dm = (int16_t)(dot / (int32_t)(r100 * 10u));
    }
    /* The same velocity on the local east and north, without trigonometry:
     * east = (-y, x, 0) / h, north = (-z x, -z y, h^2) / (r h). Only ratios
     * of positions appear, so any scale works: 400 m (shifts, no division)
     * keeps every product within an int32 (h: distance to the polar axis). */
    out->hs_ms = 0;
    out->hdg = 0;
    {
        int32_t x4 = x100 >> 2, y4 = y100 >> 2, z4 = z100 >> 2;
        int32_t h4 = (int32_t)(horizontal >> 2);
        if (h4) {
            int32_t ve = (x4 * vy - y4 * vx) / h4;                          /* cm/s */
            int32_t vr = (x4 * vx + y4 * vy) / h4;                          /* away from the axis */
            int32_t vn = (h4 * vz - z4 * vr) / (int32_t)(r100 >> 2);
            int32_t a = atan2Arc(ve, vn, arc);                              /* from north, east > 0 */
            /* int16 cm/s components: the sum of squares stays below 2^32 */
            out->hs_ms = (uint16_t)((isqrt32((uint32_t)ve * (uint32_t)ve + (uint32_t)vn * (uint32_t)vn) + 50u) / 100u);
            if (a < 0) a += 129600000;
            a = (a + 180000) / 360000;
            out->hdg = (uint16_t)(a >= 360 ? 0 : a);
        }
    }
    return true;
}

/* De-whiten one frame and read it; out is valid only on success. */
static bool frameOk(const decrs41_t *d, uint8_t *f, sonde_info_t *out)
{
    const uint16_t statusPos = 0x39u;
    unsigned gpsPos = 0x112u, gpsId = 0x7Bu, gpsLen = 21u;
    for (unsigned i = 8u; i < RS41_FRAME_LEN; i++) f[i] ^= d->mask[i & 63u];
    if (!blockOk(f, statusPos, 0x79u, 40u)) return false;
    for (unsigned i = 0; i < 8u; i++) {
        unsigned c = f[statusPos + 4u + i];
        if (c < 0x20u || c > 0x7Eu) return false;
        out->id[i] = (char)c;
    }
    out->id[8] = 0;
    out->batt_dv = f[statusPos + 12u];     /* 0x45: battery, 0.1 V (rs41mod) */
    out->flags = SONDE_HAS_ID;
    /* Without the classic block's ID at 0x112, the multi-GNSS block 0x82 at
     * 0xA1 (38 bytes, rs41mod: 0x8226), which starts with the same ECEF
     * position and velocity. A classic frame takes the classic path as before. */
    if (f[gpsPos] != gpsId) { gpsPos = 0xA1u; gpsId = 0x82u; gpsLen = 38u; }
    if (blockOk(f, gpsPos, gpsId, gpsLen) && ecef(f + gpsPos + 2u, out, d->arc))
        out->flags |= SONDE_HAS_POS | SONDE_HAS_ALT;
    return true;
}

bool decrs41_parse(decrs41_t *d, sonde_info_t *out)
{
    for (unsigned k = 0; k < RS41_NSL; k++) {
        rs41sl_t *s = &d->sl[k];
        if (s->state != ST_DONE) continue;
        /* The other slicer is reading the same burst: drop it on success. */
        if (frameOk(d, s->frm, out)) { decrs41_rearm(d); return true; }
        slRearm(s);
    }
    return false;
}
