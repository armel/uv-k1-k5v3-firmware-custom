/* SPDX-License-Identifier: Apache-2.0
 * APRS position / AX.25 UI framing, written from the wire specifications.
 * No hardware, allocation, libc, or resident firmware dependencies.
 */
#include "protocol.h"

bool aprs_call_valid(const char *c)
{
    uint8_t i = 0;
    while (i < 6 && c[i]) {
        if (!((c[i] >= 'A' && c[i] <= 'Z') ||
              (c[i] >= '0' && c[i] <= '9'))) return false;
        ++i;
    }
    return i && !c[i];
}

bool aprs_model_valid(const aprs_model_t *m)
{
    return aprs_call_valid(m->call) && m->ssid < 16 && m->path < 4 &&
        m->table < 2 && m->symbol >= '!' && m->symbol <= '~' && m->valid == 1 &&
        m->lat >= -540000 && m->lat <= 540000 &&
        m->lon >= -1080000 && m->lon <= 1080000;
}

void aprs_defaults(aprs_model_t *m)
{
    m->lat = m->lon = 0;
    for (uint8_t i = 0; i < 7; ++i) m->call[i] = 0;
    m->ssid = m->path = m->table = m->valid = 0;
    m->symbol = '>';
}

static uint8_t crc8(const uint8_t *p)
{
    uint8_t c = 0;
    for (uint8_t n = 0; n < 15; ++n) {
        c ^= p[n];
        for (uint8_t i = 0; i < 8; ++i)
            c = (uint8_t)((c << 1) ^ ((c & 128) ? 7 : 0));
    }
    return c;
}

static int32_t signed24(const uint8_t *p)
{
    uint32_t v = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    return (v & 0x800000) ? (int32_t)v - 0x1000000 : (int32_t)v;
}

bool aprs_cfg_decode16(const uint8_t r[16], aprs_model_t *m)
{
    if (r[0] != 0xA1 || r[15] != crc8(r)) return false;
    uint8_t end = 6;
    while (end && r[end] == ' ') --end;
    for (uint8_t i = 0; i < 7; ++i) m->call[i] = i < end ? r[i + 1] : 0;
    m->ssid = r[7] & 15;
    m->path = (r[7] >> 4) & 3;
    m->table = (r[7] >> 6) & 1;
    m->valid = r[7] >> 7;
    m->lat = signed24(r + 8);
    m->lon = signed24(r + 11);
    m->symbol = r[14];
    return aprs_model_valid(m);
}

void aprs_cfg_encode16(const aprs_model_t *m, uint8_t r[16])
{
    r[0] = 0xA1;
    uint8_t i = 0;
    for (; i < 6 && m->call[i]; ++i) r[1 + i] = m->call[i];
    for (; i < 6; ++i) r[1 + i] = ' ';
    r[7] = m->ssid | (m->path << 4) | (m->table << 6) | (m->valid << 7);
    for (i = 0; i < 3; ++i) {
        r[8 + i] = (uint32_t)m->lat >> (8 * i);
        r[11 + i] = (uint32_t)m->lon >> (8 * i);
    }
    r[14] = m->symbol;
    r[15] = crc8(r);
}

uint16_t aprs_crc16(const uint8_t *p, uint16_t length)
{
    uint16_t c = 0xFFFF;
    for (uint16_t n = 0; n < length; ++n) {
        c ^= p[n];
        for (uint8_t i = 0; i < 8; ++i)
            c = (c >> 1) ^ ((c & 1) ? 0x8408 : 0);
    }
    return c;
}

void aprs_number(uint32_t n, char *p, uint8_t digits)
{
    static const uint16_t place[] = { 1, 10, 100, 1000, 10000 };
    p[digits] = 0;
    uint8_t i = 0;
    while (digits) { p[i++] = '0' + aprs_divmod(&n, place[--digits]); }
}

uint32_t aprs_divmod(uint32_t *v, uint32_t d)
{
    uint32_t q = 0;
    while (*v >= d) { *v -= d; ++q; }
    return q;
}

void aprs_coordinate(int32_t v, bool longitude, char *out)
{
    bool negative = v < 0;
    if (negative) v = -v;
    uint32_t minutes = (uint32_t)v;
    uint32_t degrees = aprs_divmod(&minutes, 6000);
    uint8_t i = longitude ? 3 : 2;
    aprs_number(degrees, out, i);
    aprs_number(minutes, out + i, 4);
    out[i + 4] = out[i + 3]; out[i + 3] = out[i + 2];
    out[i + 2] = '.';
    out[i + 5] = longitude ? (negative ? 'W' : 'E') : (negative ? 'S' : 'N');
    out[i + 6] = 0;
}

static uint16_t address(uint8_t *f, uint16_t n, const char *c, uint8_t ssid)
{
    uint8_t i = 0;
    for (; i < 6 && c[i]; ++i) f[n++] = (uint8_t)c[i] << 1;
    for (; i < 6; ++i) f[n++] = ' ' << 1;
    f[n++] = ssid;
    return n;
}

uint16_t aprs_frame_build(const aprs_model_t *m, const char *comment, uint8_t f[APRS_FRAME_CAP])
{
    if (!aprs_model_valid(m)) return 0;
    uint8_t nc = 0;
    while (comment[nc]) {
        if (nc == 43 || comment[nc] < ' ' || comment[nc] > '~') return 0;
        ++nc;
    }
    uint16_t n = address(f, 0, "APZOV1", 0xE0);
    n = address(f, n, m->call, 0x60 | (m->ssid << 1) | (m->path == 0));
    if (m->path) {
        n = address(f, n, m->path == 3 ? "ARISS" : "WIDE1",
                    m->path == 3 ? 0x61 : (m->path == 1 ? 0x63 : 0x62));
        if (m->path == 2) n = address(f, n, "WIDE2", 0x63);
    }
    f[n++] = 3;
    f[n++] = 0xF0;
    f[n++] = '!';
    /* Write coordinates directly into the sole frame buffer; their NUL is
     * replaced by the next field. At most 95 bytes including FCS. */
    aprs_coordinate(m->lat, false, (char *)f + n); n += 8;
    f[n++] = m->table ? '\\' : '/';
    aprs_coordinate(m->lon, true, (char *)f + n); n += 9;
    f[n++] = m->symbol;
    for (uint8_t i = 0; i < nc; ++i) f[n++] = comment[i];
    uint16_t c = aprs_crc16(f, n) ^ 0xFFFF;
    f[n++] = c;
    f[n++] = c >> 8;
    return n;
}

void aprs_hdlc_init(aprs_hdlc_t *s, const uint8_t *f, uint16_t n,
                    uint16_t pre, uint8_t tail)
{
    s->frame = f; s->length = n; s->flags = pre; s->tail = tail;
    s->index = 0; s->stage = s->bit = s->ones = s->zero = 0;
}

bool aprs_hdlc_next(aprs_hdlc_t *s, uint8_t *bit)
{
    for (;;) {
        if (s->stage != 1) {
            if (!s->flags) {
                if (s->stage == 2) return false;
                s->stage = 1;
                continue;
            }
            *bit = (0x7E >> s->bit) & 1;
            if (++s->bit == 8) { s->bit = 0; --s->flags; }
            return true;
        }
        if (s->zero) { s->zero = s->ones = 0; *bit = 0; return true; }
        if (s->index == s->length) {
            s->stage = 2; s->flags = s->tail; s->bit = 0;
            continue;
        }
        *bit = (s->frame[s->index] >> s->bit) & 1;
        if (++s->bit == 8) { s->bit = 0; ++s->index; }
        s->ones = *bit ? s->ones + 1 : 0;
        if (s->ones == 5) s->zero = 1;
        return true;
    }
}
