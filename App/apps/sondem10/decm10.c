/* M10 soft-decision decoder derived from the Sonde M20 acquisition model.
 *
 * M10 uses differential Manchester at about 9615 chips/s. The 19.2 kHz ADC
 * rate gives just under two samples per chip; the edge DPLL tracks the small
 * rate difference. Standard 0x9F frames and their optional 0x76-byte form are
 * accepted. M10+ (0xAF) and the periodic 0x49 auxiliary frame are ignored.
 */
#include "decm10.h"

#define FS       19200u
#define SPB_Q8   ((int32_t)((FS << 8) / M10_BAUD))
#define MEAN_SH  6
#define CHIP_MSK ((uint8_t)(M10_CHIP_N - 1u))
enum { ST_SRCH = 0, ST_DATA = 1, ST_DONE = 2 };

#define RAW_HDR 0x99994C99u

/* Words throughout, the byte ranges kept by masks: no uxtb per step. */
static uint16_t checkM10(const uint8_t *msg, unsigned len)
{
    unsigned c = 0;
    for (unsigned i = 0; i < len; i++) {
        unsigned b = msg[i];
        b = ((b >> 1) | (b << 7)) & 0xFFu;
        b ^= b >> 2;
        unsigned t6 = (c ^ (c >> 2) ^ (c >> 4)) & 1u;
        unsigned t7 = ((c >> 1) ^ (c >> 3) ^ (c >> 5)) & 1u;
        unsigned t = (c & 0x3Fu) | (t6 << 6) | (t7 << 7);
        unsigned s = (c >> 7) & 0xFFu;
        s ^= s >> 2;
        c = ((c & 0xFFu) << 8) | (b ^ t ^ s);
    }
    return (uint16_t)c;
}

static uint32_t be(const uint8_t *p, unsigned n)
{
    uint32_t v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}

/* Return the high word of a*b without requiring a 64-bit multiply helper. */
static uint32_t mulHi32(uint32_t a, uint32_t b)
{
    uint32_t ah = a >> 16, al = a & 0xFFFFu;
    uint32_t bh = b >> 16, bl = b & 0xFFFFu;
    uint32_t t = al * bl;
    uint32_t carry = t >> 16;
    t = ah * bl + carry;
    uint32_t mid = t & 0xFFFFu;
    uint32_t high = t >> 16;
    t = al * bh + mid;
    return ah * bh + high + (t >> 16);
}

/* 2^32 per turn to centi-arcseconds (129600000 per turn, 0.3 m). */
static int32_t binaryAngleToCas(const uint8_t *p)
{
    int32_t v = (int32_t)be(p, 4);
    uint32_t neg = v < 0;
    uint32_t u = (uint32_t)v;
    if (neg) u = 0u - u;
    uint32_t arc = mulHi32(u, 129600000u);
    return neg ? -(int32_t)arc : (int32_t)arc;
}

static int16_t softOf(int32_t integ)
{
    int32_t v = integ >> 8;
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    return (int16_t)v;
}

static bool hdrOk(const int16_t *ch, unsigned base)
{
    int32_t s = 0;
    uint32_t e = 0;
    for (unsigned i = 0; i < 32u; i++) {
        int32_t c = ch[(base + i) & CHIP_MSK];
        e += (uint32_t)(c >= 0 ? c : -c);
        if ((RAW_HDR >> (31u - i)) & 1u) s += c;
        else s -= c;
    }
    if (s < 0) s = -s;
    return e > 120u && (uint32_t)s * 5u > e * 3u;
}

static uint16_t softWord(const int16_t *ch, unsigned base, uint8_t *prev_out)
{
    uint16_t w = 0;
    uint8_t prev = 0;
    for (unsigned i = 0; i < 16u; i++) {
        int32_t s1 = ch[(base + i * 2u) & CHIP_MSK];
        int32_t s2 = ch[(base + i * 2u + 1u) & CHIP_MSK];
        uint8_t man = (s2 >= s1) ? 1u : 0u;
        w = (uint16_t)((w << 1) | (prev == man ? 1u : 0u));
        prev = man;
    }
    *prev_out = prev;
    return w;
}

static void putBit(decm10_t *d, uint8_t bit)
{
    if (d->nbit >= d->needBits) return;
    if (!(d->nbit & 7u)) d->frm[d->nbit >> 3] = 0;
    if (bit) d->frm[d->nbit >> 3] |= (uint8_t)(1u << (7u - (d->nbit & 7u)));
    d->nbit++;
}

static bool supportedHeader(uint16_t w)
{
    uint8_t len = (uint8_t)(w >> 8);
    return (len == 0x64u || len == 0x66u || len == 0x76u) &&
           (uint8_t)w == 0x9Fu;
}

static void emitChip(decm10_t *d, int16_t soft)
{
    if (d->state == ST_SRCH) {
        uint8_t prev;
        uint16_t w;
        d->chips[d->cpos] = soft;
        d->cpos = (uint8_t)((d->cpos + 1u) & CHIP_MSK);
        if (d->nfill < 64u) { d->nfill++; return; }
        if (!hdrOk(d->chips, d->cpos)) return;
        w = softWord(d->chips, (uint8_t)(d->cpos + 32u) & CHIP_MSK, &prev);
        d->inv = (uint8_t)(w >> 15);
        w &= 0x7FFFu;
        if (!supportedHeader(w)) return;
        d->frm[0] = (uint8_t)(w >> 8);
        d->frm[1] = (uint8_t)w;
        d->nbit = 16u;
        d->needBits = (uint16_t)((d->frm[0] + 1u) * 8u);
        d->prev = (uint8_t)(prev ^ d->inv);
        d->state = ST_DATA;
        return;
    }
    if (++d->guard > (uint16_t)(d->needBits * 3u + 64u)) {
        d->state = ST_DONE;
        return;
    }
    {
        int32_t c = d->inv ? -(int32_t)soft : (int32_t)soft;
        uint8_t man;
        if (!d->half) { d->h1 = c; d->half = 1u; return; }
        d->half = 0;
        man = (c >= d->h1) ? 1u : 0u;
        putBit(d, d->prev == man ? 1u : 0u);
        d->prev = man;
        if (d->nbit >= d->needBits) d->state = ST_DONE;
    }
}

void decm10_init(decm10_t *d)
{
    uint8_t *p = (uint8_t *)d;
    for (unsigned i = 0; i < sizeof *d; i++) p[i] = 0;
    d->meanQ8 = -1;
    d->ph = SPB_Q8 / 2;
}

void decm10_rearm(decm10_t *d)
{
    int32_t mean = d->meanQ8;
    decm10_init(d);
    d->meanQ8 = mean;
}

bool decm10_busy(const decm10_t *d) { return d->state == ST_DATA; }

bool decm10_push(decm10_t *d, uint16_t sample)
{
    int32_t xq, e;
    uint8_t s;

    if (d->state == ST_DONE) return true;
    xq = (int32_t)sample << 8;
    if (d->meanQ8 < 0) d->meanQ8 = xq;
    e = xq - d->meanQ8;
    if (d->state != ST_DATA)
        d->meanQ8 += (e + (1 << (MEAN_SH - 1))) >> MEAN_SH;
    s = e > 0 ? 1u : 0u;
    if (s != d->sign) {
        uint32_t a = (uint32_t)(d->pe < 0 ? -d->pe : d->pe);
        uint32_t b = (uint32_t)(e < 0 ? -e : e);
        int32_t err = (int32_t)((a << 8) / (a + b + 1u)) - 128 - d->ph;
        if (err < -(SPB_Q8 / 2)) err += SPB_Q8;
        d->ph += err >> 3;
        d->sign = s;
    }
    d->pe = e;
    if (d->ph < 256) {
        int32_t part = (e * (d->ph < 0 ? 0 : d->ph)) >> 8;
        int16_t chip = softOf(d->integ + part);
        d->integ = e - part;
        d->ph += SPB_Q8 - 256;
        emitChip(d, chip);
        return d->state == ST_DONE;
    }
    d->integ += e;
    d->ph -= 256;
    return false;
}

static char hexDigit(uint8_t v)
{
    if (v < 10u) return (char)('0' + (int)v);
    return (char)('A' + (int)v - 10);
}

static char *digits(char *o, uint32_t v, uint32_t div)
{
    for (; div; div /= 10u) *o++ = (char)('0' + (v / div) % 10u);
    return o;
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

/* Compass bearing of the (east, north) vector, whole degrees 0..359: the
 * octant folded, then atan(t) ~ 45 t + 15.6 t (1 - t) degrees for
 * t = min / max in [0, 1] (within 0.3 degree before rounding). */
static uint16_t bearing(int32_t e, int32_t n)
{
    uint32_t ae = (uint32_t)(e < 0 ? -e : e), an = (uint32_t)(n < 0 ? -n : n);
    uint32_t mn = ae < an ? ae : an, mx = ae < an ? an : ae;
    uint32_t t, a;
    if (!mx) return 0;
    t = (mn << 12) / mx;                                        /* Q12 */
    a = (450u * t + ((156u * t * (4096u - t)) >> 12) + 2048u) >> 12;  /* 0.1 deg */
    if (ae > an) a = 900u - a;           /* from north, within the quadrant */
    if (n < 0) a = 1800u - a;
    if (e < 0) a = 3600u - a;
    a = (a + 5u) / 10u;
    return (uint16_t)(a >= 360u ? 0u : a);
}

bool decm10_parse(decm10_t *d, sonde_info_t *out)
{
    const uint8_t *f = d->frm;
    uint8_t pc = (uint8_t)(f[0] - 1u);
    int32_t alt;

    if ((f[0] != 0x64u && f[0] != 0x66u && f[0] != 0x76u) ||
        f[1] != 0x9Fu || pc >= M10_FRAME_LEN - 1u ||
        be(f + pc, 2) != checkM10(f, pc))
        return false;

    {
        uint8_t b = f[0x5Fu];
        uint16_t n = (uint16_t)(f[0x60] | ((uint16_t)f[0x61] << 8));
        char *o = out->id;
        *o++ = hexDigit((uint8_t)(b >> 4));
        o = digits(o, b & 0x0Fu, 10u); *o++ = '-';
        *o++ = hexDigit(f[0x5Du] & 0x0Fu); *o++ = '-';
        *o++ = (char)('0' + ((n >> 13) & 7u));
        o = digits(o, n & 0x1FFFu, 1000u); *o = 0;
    }
    out->flags = SONDE_HAS_ID;
    {
        uint8_t sc = f[0x3Eu];
        uint16_t adc = (uint16_t)(f[0x3Fu] | ((uint16_t)f[0x40u] << 8));
        if (sc < 3u && adc > 0xA000u && adc <= 0xAFFFu)
            out->tRaw = (uint16_t)((adc - 0xA000u) | ((uint16_t)sc << 12));
        else
            out->tRaw = 0;
    }
    /* Humidity (m10mod get_RH): the capacitive sensor's 555 period over the
     * 55 %RH reference's, both 24-bit LE at 0x35 and 0x32; RH = (ratio -
     * 0.8955) / 0.002, here in 0.1 % before the temperature terms (the app's).
     * Both counts scaled down to 17 bits, the ratio in Q15 with one division:
     * within 0.1 %RH of the exact quotient. */
    {
        uint32_t c55 = f[0x32u] | ((uint32_t)f[0x33u] << 8) | ((uint32_t)f[0x34u] << 16);
        uint32_t cRH = f[0x35u] | ((uint32_t)f[0x36u] << 8) | ((uint32_t)f[0x37u] << 16);
        while (cRH > 0x1FFFFu) { cRH >>= 1; c55 >>= 1; }
        if (c55) {
            out->rh10 = (int16_t)((int32_t)((((cRH << 15) / c55) * 5000u) >> 15) - 4477);
            out->flags |= SONDE_HAS_RH;
        }
    }
    if (f[2] != 0x20u || (f[0x1Du] & 1u)) return true;

    out->latS = binaryAngleToCas(f + 0x0Eu);
    out->lonS = binaryAngleToCas(f + 0x12u);
    alt = (int32_t)be(f + 0x16u, 4);
    out->alt_m = alt / 1000;
    out->flags |= SONDE_HAS_ALT;
    if (out->latS || out->lonS) out->flags |= SONDE_HAS_POS;
    /* GPS velocity east / north / up, int16 big-endian in 1/200 m/s
     * (m10mod pos_GPSvE / vN / vU) */
    {
        int32_t ve = (int16_t)be(f + 0x04u, 2), vn = (int16_t)be(f + 0x06u, 2);
        out->vs_dm = (int16_t)((int16_t)be(f + 0x08u, 2) / 20);
        out->hs_ms = (uint8_t)((isqrt32((uint32_t)ve * (uint32_t)ve +
                                        (uint32_t)vn * (uint32_t)vn) + 100u) / 200u);
        const uint16_t hdg = bearing(ve, vn);
        out->hdg = (uint8_t)hdg;
        out->flags |= (uint8_t)(SONDE_HAS_VEL | (hdg >> 8) * SONDE_HDG_256);
    }
    return true;
}
