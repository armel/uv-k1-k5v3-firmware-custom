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
 *
 * Portions (navigation(), bearing(), isqrt32(), the dial and its drawing:
 * plot(), segment(), polar()) are adapted from GeoNav, Copyright 2026 Armel
 * F4HWN, also under the Apache License, Version 2.0.
 */

/*
 * Nav 406: walk to a 406 MHz distress beacon with a GNSS receiver on USB-C.
 *
 * The target is the position EPIRB 406 decoded, handed off with key 2 in the
 * QR Map message (latitude, longitude in centi-arcseconds, then the beacon ID
 * as a label). It is saved, so the app keeps it after a round trip to Sig
 * Finder or when launched from the menu. The own position comes live from an
 * NMEA receiver on the USB-C D+ or D- pin, read by a software UART as CW USB
 * takes those pins (RMC only).
 *
 * Distance and azimuth: GeoNav's equirectangular navigation (bearing(),
 * isqrt32(), the Q1000 cosine table) and its dial, arrow and 1-2-5 range ring,
 * by Armel F4HWN, Apache-2.0. The radio has no compass: walking (2 km/h or
 * more), the dial turns course up (the top is where you walk, the arrow is the
 * turn: "AHEAD", "L 40", "R 120"); standing still, it is north up.
 *
 * Target precision: a beacon position is coarse (PDF-1: 15', 2' or 4' grid by
 * protocol) unless the long message carried the fine offsets (4 s steps). A
 * position that lies exactly on the 15', 4' or 2' grid is taken as coarse,
 * and the radius shown is half the grid diagonal; else 4 s, about 90 m.
 *
 * Keys: 8 / 9 Sig Finder on 121.5 / 243 MHz (the VFO is staged, Sig Finder
 *       gives it back and launches Nav 406 again) . 1 baud rate . 2 pin D+/D-
 *       . 0 forget the target . EXIT back to EPIRB 406 when a hand-off
 *       opened this app (EPIRB 406, or Sig Finder coming back), else quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "../app_config_proxy.h"
#include "nav406_assets.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define GPIOA_MODER (*(volatile uint32_t *)0x50000000u)
#define GPIOA_PUPDR (*(volatile uint32_t *)0x5000000Cu)
#define GPIOA_IDR   (*(volatile uint32_t *)0x50000010u)
#define RCC_APBENR1 (*(volatile uint32_t *)0x4002103Cu)
#define SYST_LOAD   (*(volatile uint32_t *)0xE000E014u)
#define SYST_VAL    (*(volatile uint32_t *)0xE000E018u)
#define USBEN       (1u << 23)
#define PIN_DM      11u
#define PIN_DP      12u
#define USB_PINS    (0xFu << (2u * PIN_DM))
#define USB_PULLUP  (0x5u << (2u * PIN_DM))

#define CPU_HZ        48000000u
#define IDLE_UI       (CPU_HZ / 50u)   /* 20 ms of quiet line before UI work */
#define UI_PERIOD_MS  100u
#define DRAW_MS       1000u
#define STALE_MS      3000u
#define SKIP_MS       700u             /* line ignored after an RMC (1 Hz)   */
#define LINE_MAX      84u
#define BAUD_COUNT    6u
#define CFG_MAGIC     0x4Eu
#define LAT_MAX_CAS   32400000L
#define LON_MAX_CAS   64800000L
#define CIRCLE_CAS    129600000L
#define WALK_KMH10    20u              /* course over ground trusted above   */
#define ID_TAIL       4u               /* last ID characters kept and shown  */
#define CX            ((int16_t)DIAL_R) /* dial centre, both axes            */
#define COL_X         58u              /* right column, beside the dial      */
#define UI(off)       (ui + (off) - T_TITLE)
#define EPIRB_NAME    "EPIRB 406"

/* bit time in CPU cycles: 4800, 9600, 19200, 38400, 57600, 115200 bauds */
static const uint16_t BIT[BAUD_COUNT] = { 10000u, 5000u, 2500u, 1250u, 833u, 417u };


typedef struct {
    uint8_t magic, baud, pin, hasTarget;
    int32_t lat, lon;                   /* target, centi-arcseconds          */
    char    id[ID_TAIL];                /* last characters of the label (ID) */
} cfg_t;

/* The loader keeps APP_CONFIG_LOCAL_MAX bytes and drops the rest silently. */
_Static_assert(sizeof(cfg_t) <= APP_CONFIG_LOCAL_MAX, "Nav 406 config must fit the saved bytes");

struct globals {
    const app_api_t *api;
    cfg_t    cfg;
    bool     running, saver, help, newRmc, hasPos, fromEpirb;
    uint8_t  prevKey, len, ck, ckRx, ckState;
    char     status;
    uint16_t kmh10, course;
    uint32_t bit, pinMask, load, lastVal;
    uint32_t lastUi, lastDraw, lastLineMs;
    uint32_t moder, pupdr, apbenr1;
    int32_t  lat, lon;                  /* own position, centi-arcseconds    */
    char     buf[LINE_MAX];
    char     idText[16];                /* full ID after a hand-off, else the saved tail */
};
static struct globals g;
#define A (g.api)

/* the resident division helpers, as GeoNav */
uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d) { return A->uidivmod(n, d); }
uint64_t __aeabi_uidiv(uint32_t n, uint32_t d) __attribute__((alias("__aeabi_uidivmod")));
uint64_t __aeabi_idivmod(int32_t n, int32_t d) { return A->idivmod(n, d); }
uint64_t __aeabi_idiv(int32_t n, int32_t d) __attribute__((alias("__aeabi_idivmod")));

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; return o; }
__attribute__((noinline))
static char *putu(char *o, uint32_t v) { return A->append_u32(o, v); }
static uint32_t abs32(int32_t v) { return v < 0 ? (uint32_t)-v : (uint32_t)v; }

/* cos(0..90 degrees) x 1000, from the assets */
static int32_t cosine(uint32_t deg)
{
    uint16_t c = 0u;
    A->asset_read((uint16_t)(COS_Q1000 + deg * 2u), &c, sizeof c);
    return c;
}

static uint32_t irqSave(void)
{
    uint32_t primask;
    __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
    return primask;
}
static void irqRestore(uint32_t primask)
{
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}
#define IRQ_OFF() __asm volatile ("cpsid i" ::: "memory")
#define IRQ_ON()  __asm volatile ("cpsie i" ::: "memory")

/* ---- USB pins as a GPIO input (as CW USB) ---- */
static void portSet(bool open)
{
    /* called once to open, once to close (app_main) */
    const uint32_t irq = irqSave();
    uint32_t pu = USB_PULLUP, mode = 0u, clock = 0u;
    if (open) {
        g.moder = GPIOA_MODER & USB_PINS;
        g.pupdr = GPIOA_PUPDR & USB_PINS;
        g.apbenr1 = RCC_APBENR1 & USBEN;
    } else {
        pu = g.pupdr;
        mode = g.moder;
        clock = g.apbenr1;
    }
    GPIOA_PUPDR = (GPIOA_PUPDR & ~USB_PINS) | pu;
    GPIOA_MODER = (GPIOA_MODER & ~USB_PINS) | mode;
    RCC_APBENR1 = (RCC_APBENR1 & ~USBEN) | clock;
    irqRestore(irq);
}

static void applyCfg(void)
{
    g.bit = BIT[g.cfg.baud];
    g.pinMask = 1u << (g.cfg.pin ? PIN_DM : PIN_DP);
    g.len = 0;
}

static void saveCfg(void) { A->cfg_save((const uint8_t *)&g.cfg, sizeof g.cfg); }

/* ---- software UART ---- */
__attribute__((always_inline))
static inline uint32_t ticksSince(void)
{
    const uint32_t now = SYST_VAL;
    const uint32_t prev = g.lastVal;
    g.lastVal = now;
    return prev >= now ? prev - now : prev + g.load + 1u - now;
}

__attribute__((noinline))
static uint32_t waitTo(uint32_t el, uint32_t t)
{
    while (el < t) el += ticksSince();
    return el;
}

/* IRQs masked, g.lastVal set at the start edge: mid-bit samples. */
static int16_t rxByte(void)
{
    const uint32_t b = g.bit, m = g.pinMask;
    uint32_t el = 0, t = b >> 1;
    uint8_t v = 0;
    el = waitTo(el, t);
    if (GPIOA_IDR & m) return -2;
    for (uint8_t i = 0; i < 9u; i++) {
        t += b;
        el = waitTo(el, t);
        if (i == 8u) break;
        v >>= 1;
        if (GPIOA_IDR & m) v |= 0x80u;
    }
    return (GPIOA_IDR & m) ? (int16_t)v : -1;
}

/* ---- NMEA: RMC only ---- */
static uint8_t hexv(uint8_t c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0xFFu;
}

static void parseRmc(const char *s);

/* A checked RMC is parsed at once, in the gap after its LF: well within a
 * byte time, and at worst the first byte of the next sentence (GGA, VTG...,
 * not read here) is missed. One buffer, no copy. */
static void endLine(void)
{
    char *s = g.buf;
    if (g.ckState != 3u || g.ck != g.ckRx || g.len < 7u) return;
    s[g.len] = '\0';
    if (s[1] == 'P' || s[3] != 'R' || s[4] != 'M' || s[5] != 'C') return;
    parseRmc(s);                        /* now: the next sentence is not needed */
    g.newRmc = true;
}

static void feed(uint8_t c)
{
    char *s = g.buf;
    if (c == '$') { s[0] = '$'; g.len = 1; g.ck = 0; g.ckRx = 0; g.ckState = 0; return; }
    if (g.len == 0u) return;
    if (c == '\n') { endLine(); g.len = 0; return; }
    if (c == '\r') return;
    if (g.len >= LINE_MAX - 1u) { g.len = 0; return; }
    s[g.len++] = (char)c;
    if (g.ckState == 0u) {
        if (c == '*') g.ckState = 1; else g.ck ^= c;
    } else if (g.ckState < 3u) {
        const uint8_t h = hexv(c);
        g.ckRx = (uint8_t)((g.ckRx << 4) | (h & 0x0Fu));
        g.ckState = h > 15u ? 9u : (uint8_t)(g.ckState + 1u);
    }
}

static bool isEnd(char c) { return c == ',' || c == '*' || c == '\0'; }

static const char *field(const char *s, uint8_t n)
{
    while (n) {
        if (*s == '\0' || *s == '*') return 0;
        if (*s++ == ',') n--;
    }
    return s;
}

static bool num(const char *p, uint32_t *ip, uint32_t *fr, uint8_t nd)
{
    if (!p || isEnd(*p)) return false;
    uint32_t i = 0, f = 0;
    uint8_t k = 0;
    while (*p >= '0' && *p <= '9') i = i * 10u + (uint32_t)(*p++ - '0');
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++)
            if (k < nd) { f = f * 10u + (uint32_t)(*p - '0'); k++; }
    for (; k < nd; k++) f *= 10u;
    *ip = i; *fr = f;
    return true;
}

/* ddmm.mmmmm + hemisphere -> centi-arcseconds: 1e-5 minute = 3/50 cas. */
static bool coord(const char *s, uint8_t n, int32_t *out)
{
    uint32_t ip, m5;
    if (!num(field(s, n), &ip, &m5, 5u)) return false;
    const char *h = field(s, n + 1u);
    if (!h || isEnd(*h)) return false;
    const uint32_t deg = ip / 100u;
    if (deg > 180u) return false;
    const uint32_t v = deg * 360000u + (ip - deg * 100u) * 6000u + m5 * 3u / 50u;
    *out = (*h == 'S' || *h == 'W') ? -(int32_t)v : (int32_t)v;
    return true;
}

static void parseRmc(const char *s)
{
    uint32_t ip, fr;
    int32_t la, lo;
    const char *p = field(s, 2u);
    g.status = (p && !isEnd(*p)) ? *p : 'V';
    g.hasPos = g.status == 'A' && coord(s, 3u, &la) && coord(s, 5u, &lo);
    if (g.hasPos) { g.lat = la; g.lon = lo; }
    g.kmh10 = 0;
    if (num(field(s, 7u), &ip, &fr, 3u))
        g.kmh10 = (uint16_t)((ip * 1000u + fr) * 1852u / 100000u);   /* milliknots */
    g.course = num(field(s, 8u), &ip, &fr, 0u) ? (uint16_t)ip : 0u;
}

/* ---- navigation (GeoNav, Armel F4HWN, Apache-2.0) ---- */
static uint32_t isqrt32(uint32_t n)
{
    uint32_t root = 0u, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

/* Compass bearing of the (east, north) vector, whole degrees. */
static uint16_t bearing(int32_t east, int32_t north)
{
    const uint32_t ae = abs32(east), an = abs32(north);
    const uint32_t mn = ae < an ? ae : an, mx = ae < an ? an : ae;
    uint32_t t, angle;
    if (!mx) return 0u;
    t = (mn << 12) / mx;
    angle = (450u * t + ((156u * t * (4096u - t)) >> 12) + 2048u) >> 12;
    if (ae > an) angle = 900u - angle;
    if (north < 0) angle = 1800u - angle;
    if (east < 0) angle = 3600u - angle;
    angle = (angle + 5u) / 10u;
    return (uint16_t)(angle >= 360u ? 0u : angle);
}

/* Equirectangular: metres, and the azimuth in *heading. */
static uint32_t navigation(int32_t home_lat, int32_t home_lon,
                           int32_t target_lat, int32_t target_lon, uint16_t *heading)
{
    int32_t north = target_lat - home_lat;
    int32_t east = target_lon - home_lon;
    if (east > LON_MAX_CAS) east -= CIRCLE_CAS;
    else if (east < -LON_MAX_CAS) east += CIRCLE_CAS;
    const int32_t mean_lat = home_lat / 2 + target_lat / 2;
    uint32_t lat_deg = (abs32(mean_lat) + 180000u) / 360000u;
    if (lat_deg > 90u) lat_deg = 90u;
    uint32_t unit = 1u;
    while (abs32(north) / unit > 30000u || abs32(east) / unit > 30000u)
        unit *= 10u;
    north /= (int32_t)unit;
    east /= (int32_t)unit;
    east = east * cosine(lat_deg) / 1000;
    *heading = bearing(east, north);
    const uint32_t an = abs32(north), ae = abs32(east);
    const uint32_t root_cas = isqrt32(an * an + ae * ae) * unit;
    /* 111200 m per degree / 360000 cas per degree = 139/450 */
    return (root_cas / 450u) * 139u + ((root_cas % 450u) * 139u + 225u) / 450u;
}

/* Target uncertainty, metres: half the diagonal of the coarsest grid the
 * position lies on (15', 4', 2'), else the 4 s fine step. */
static uint32_t radius(void)
{
    /* step x 139/450 (m per cas) x 0.7 (half the diagonal) */
    static const uint32_t STEP[3] = { 90000u, 24000u, 12000u };
    static const uint16_t RAD[4] = { 19460u, 5189u, 2594u, 86u };
    const uint32_t la = abs32(g.cfg.lat), lo = abs32(g.cfg.lon);
    uint8_t i = 0;
    while (i < 3u && (la % STEP[i] || lo % STEP[i])) i++;
    return RAD[i];
}

/* ---- display: GeoNav's dial (Armel F4HWN, Apache-2.0) ---- */
static void plot(int16_t x, int16_t y)
{
    if ((uint16_t)x < 128u && (uint16_t)y < 56u)
        A->fb[y >> 3][x] |= (uint8_t)(1u << (y & 7));
}

/* A line by steps along its longer axis, 8.8 fixed point: two divisions. */
static void segment(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    const int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t n = dx < 0 ? -dx : dx;
    if ((dy < 0 ? -dy : dy) > n) n = dy < 0 ? -dy : dy;
    /* n > 0: the arrow's strokes are 7 px or more */
    const int32_t sx = dx * 256 / n, sy = dy * 256 / n;
    int32_t x = x0 * 256 + 128, y = y0 * 256 + 128;
    for (int32_t i = 0; i <= n; i++, x += sx, y += sy)
        plot((int16_t)(x >> 8), (int16_t)(y >> 8));
}

/* Dial point at horizontal radius r on angle deg (-359..719), top = 0. */
static void polar(int16_t r, int16_t deg, int16_t *x, int16_t *y)
{
    if (deg < 0) deg += 360;
    else if (deg >= 360) deg -= 360;
    const uint16_t quadrant = (uint16_t)deg / 90u, a = (uint16_t)deg % 90u;
    int32_t sn = cosine(90u - a), c = cosine(a);
    for (uint16_t q = quadrant; q; q--) { const int32_t t = sn; sn = c; c = -t; }
    const int32_t den = 1000 * (int32_t)DIAL_Y_DEN;
    sn *= r;
    c *= r * (int32_t)DIAL_Y_NUM;
    *x = (int16_t)(CX + (sn + (sn < 0 ? -500 : 500)) / 1000);
    *y = (int16_t)(CX - (c + (c < 0 ? -den / 2 : den / 2)) / den);
}

/* "123", "12.3", "123" (km from 1000 m: the caller adds the unit) */
__attribute__((noinline))
static char *putNum(char *o, uint32_t m)
{
    if (m < 1000u) return putu(o, m);
    if (m < 100000u) {
        o = putu(o, m / 1000u);
        *o++ = '.';
        *o++ = (char)('0' + (m % 1000u) / 100u);
        return o;
    }
    return putu(o, (m + 500u) / 1000u);
}

/* "86m", "2.6km": number then unit */
static char *putDist(char *o, const char *ui, uint32_t m)
{
    return put(putNum(o, m), m < 1000u ? UI(T_M) : UI(T_KM));
}

static void text(const char *s, uint8_t x, uint8_t row) { A->print_normal(s, x, 0, row); }

/* Dial (course up when walking, else north up), arrow, range ring and the
 * target, then the right column. */
static void drawNav(char *s, const char *ui)
{
    char *o;
    int16_t p[6];
    uint16_t az;
    const uint32_t d = navigation(g.lat, g.lon, g.cfg.lat, g.cfg.lon, &az);
    const bool walk = g.kmh10 >= WALK_KMH10;
    int16_t ang = (int16_t)az;
    if (walk) {
        ang = (int16_t)((int16_t)az - (int16_t)g.course);
        if (ang > 180) ang -= 360;
        else if (ang < -180) ang += 360;
    }
    const uint16_t dial = walk ? DIAL_CUP : DIAL_NUP;
    for (uint8_t page = 0u; page < DIAL_PAGES; page++)
        A->asset_read((uint16_t)(dial + page * DIAL_W), A->fb[page], DIAL_W);
    polar(CX - 6, ang, &p[0], &p[1]);
    segment(CX, CX, p[0], p[1]);
    polar(CX - 1, ang, &p[0], &p[1]);           /* the tip: two strokes */
    polar(CX - 8, (int16_t)(ang - 12), &p[2], &p[3]);
    polar(CX - 8, (int16_t)(ang + 12), &p[4], &p[5]);
    segment(p[0], p[1], p[2], p[3]);
    segment(p[0], p[1], p[4], p[5]);
    A->draw_rect(A->fb, CX - 1, CX - 1, CX + 1, CX + 1, true);

    o = putNum(s, d); *o = '\0';
    A->display_freq(s, COL_X, 0, false);        /* big digits, lines 0-1 */
    text(d < 1000u ? UI(T_M) : UI(T_KM), 114, 1);   /* 3 big digits end at x = 97 */

    o = putu(put(s, UI(T_AZ)), az); *o = '\0';
    A->print_bold(s, COL_X, 0, 2);
    if (!walk) o = put(s, UI(T_NUP));
    else if (ang >= -15 && ang <= 15) o = put(s, UI(T_AHEAD));
    else o = putu(put(s, ang < 0 ? UI(T_LEFT) : UI(T_RIGHT)), (uint32_t)(ang < 0 ? -ang : ang));
    *o = '\0'; text(s, COL_X, 3);
    o = putDist(put(s, UI(T_PM)), ui, radius()); *o = '\0'; text(s, COL_X, 4);
}

/* MENU: the GPS settings in bold ("GPS 9600 8N1 D+"), then the keys. Keys 1
 * and 2 still work here, so the change shows at once. */
static void drawHelp(char *s, const char *ui)
{
    char h[HELP_SIZE] __attribute__((aligned(4)));
    char baud[T_BAUD_STRIDE];
    A->asset_read((uint16_t)(T_BAUD + g.cfg.baud * T_BAUD_STRIDE), baud, T_BAUD_STRIDE);
    char *o = put(put(put(put(s, UI(T_GPS)), baud), UI(T_8N1)), g.cfg.pin ? UI(T_DM) : UI(T_DP));
    *o = '\0';
    A->print_bold(s, 1, 0, 0);
    if (g.cfg.hasTarget) {               /* whole ID after EPIRB 406, else the tail */
        *put(put(s, UI(T_ID)), g.idText) = '\0';
        A->print_tiny(s, 0, 10, false, true);
    }
    A->asset_read(T_HELP, h, HELP_SIZE);
    uint8_t y = 18;
    for (const char *p = h; *p; y = (uint8_t)(y + 6u)) {
        A->print_tiny(p, 0, y, false, true);
        while (*p++) {}
    }
}

static void draw(uint32_t now)
{
    char ui[UI_SIZE] __attribute__((aligned(4)));
    char s[24];
    char *o;
    A->asset_read(T_TITLE, ui, UI_SIZE);
    A->display_clear();
    A->status_clear();
    A->print_inverse(UI(T_TITLE), 2, 0, true, true, 50);
    A->draw_battery();

    const bool fresh = now - g.lastLineMs < STALE_MS;   /* last RMC */
    const bool nav = g.cfg.hasTarget && fresh && g.hasPos;
    if (g.help) {
        drawHelp(s, ui);
    } else if (nav) {
        drawNav(s, ui);
    } else if (!g.cfg.hasTarget) {
        text(UI(T_NO_TARGET), 1, 1);
        text(UI(T_GET_TARGET), 1, 2);
    } else {
        text(UI(T_SAVED), 1, 1);      /* the radius shows once navigating */
    }

    /* GPS: "5km/h" in the right column, "GPS NO FIX" full width (the pin
     * and baud rate are on the MENU page) */
    if (g.help) goto blit;
    o = nav ? s : put(s, UI(T_GPS));
    if (!fresh) o = put(o, UI(T_NO_DATA));
    else if (!g.hasPos) o = put(o, UI(T_NO_FIX));
    else {
        o = put(putu(o, (g.kmh10 + 5u) / 10u), UI(T_KMH));
    }
    *o = '\0';
    text(s, nav ? COL_X : 1, 5);
blit:
    A->blit_status();
    A->blit_full();
    g.lastDraw = now;
}

/* ---- hand-offs ---- */
static void readTarget(void)
{
    union { int32_t pos[2]; uint8_t b[APP_HANDOFF_MAX]; } m;
    const int16_t n = A->handoff_read(m.b, sizeof m.b, NULL);
    if (n < 0) return;                                /* from the menu */
    g.fromEpirb = true;                               /* EXIT goes back there */
    if (n < 8 || abs32(m.pos[0]) > LAT_MAX_CAS || abs32(m.pos[1]) > LON_MAX_CAS)
        return;                                       /* Sig Finder: no message */
    g.cfg.lat = m.pos[0];
    g.cfg.lon = m.pos[1];
    /* the whole label (beacon ID) in RAM for the help page; its last
     * ID_TAIL characters saved, right-aligned, spaces before */
    uint8_t e = 0;
    while (e < sizeof g.idText - 1u && e + 8 < n && m.b[8u + e]) {
        const uint8_t c = m.b[8u + e];
        g.idText[e++] = (char)(c >= 0x20u && c < 0x7Fu ? c : '?');
    }
    g.idText[e] = '\0';
    for (uint8_t k = ID_TAIL; k--; )
        g.cfg.id[k] = e ? g.idText[--e] : ' ';
    g.cfg.hasTarget = 1;
    saveCfg();
}

/* 8 / 9: stage 121.5 MHz AM narrow or 243 MHz AM wide and open Sig Finder
 * with the current VFO as the message (it gives it back on EXIT and launches
 * Nav 406 again), as EPIRB 406 does. */
static void sigFinder(bool mil)
{
    if (A->api_size < offsetof(app_api_t, vfo_stage) + sizeof A->vfo_stage || !A->vfo_stage)
        return;
    app_beam_channel_t ch;
    A->beam_get(&ch);
    if (!A->handoff("Sig Finder", &ch, offsetof(app_beam_channel_t, name))) return;
    ch.rx_frequency = mil ? 24300000u : 12150000u;    /* 10 Hz units */
    ch.tx_offset_frequency = 0;
    ch.tx_offset_direction = 0;
    ch.modulation = 1;                                /* AM */
    ch.channel_bandwidth = !mil;
    ch.rx_codetype = ch.tx_codetype = 0;
    A->vfo_stage(&ch);
    g.running = false;
}

/* ---- keys and the UI pass ---- */
static bool keys(void)
{
    const uint8_t key = A->get_key();
    /* Screen saver: SAVER while it shows (no drawing), WAKE once dismissed
     * (redraw). Taken as keys, they would turn the backlight on again at each
     * timeout: the screen would blink. get_key() itself relights on a key. */
    if (key >= APP_KEY_SAVER) { g.saver = key == APP_KEY_SAVER; return !g.saver; }
    if (key == APP_KEY_INVALID || key == g.prevKey) { g.prevKey = key; return false; }
    g.prevKey = key;
    switch (key) {
    case APP_KEY_1:
        if (++g.cfg.baud >= BAUD_COUNT) g.cfg.baud = 0;
        applyCfg();
        break;
    case APP_KEY_2:
        g.cfg.pin ^= 1u;
        applyCfg();
        break;
    case APP_KEY_0:
        g.cfg.hasTarget = 0;
        break;
    case APP_KEY_8:
    case APP_KEY_9:
        sigFinder(key == APP_KEY_9);
        return false;
    case APP_KEY_MENU:
        g.help ^= 1u;
        return true;
    case APP_KEY_EXIT:
        if (g.help) { g.help = 0; return true; }   /* EXIT closes the help first */
        g.running = false;
        if (g.fromEpirb) A->handoff(EPIRB_NAME, NULL, 0);
        return false;
    default:
        return false;
    }
    saveCfg();
    return true;
}

static void ui(void)
{
    const uint32_t now = A->ticks_ms();
    bool redraw = false;
    g.lastUi = now;
    if (g.newRmc) {
        g.newRmc = false;
        g.lastLineMs = now;
        redraw = true;
    }
    if (keys()) redraw = true;
    if (now - g.lastDraw >= DRAW_MS) redraw = true;
    if (redraw && !g.saver && g.running) draw(now);
    A->battery_sample();
    A->backlight_update();
    g.lastVal = SYST_VAL;
}

static void loop(void)
{
    uint32_t idle = 0;
    g.lastVal = SYST_VAL;
    while (g.running) {
        /* After an RMC, the rest of the burst (GGA, GSV...) is not needed:
         * for SKIP_MS the line is ignored and the keys are read every 10 ms.
         * Read only in the quiet gaps, a short press was lost in a burst
         * (about 600 ms at 9600 baud): EXIT had to be held. */
        if (A->ticks_ms() - g.lastLineMs < SKIP_MS) { ui(); A->delay_ms(10); continue; }
        if (!(GPIOA_IDR & g.pinMask)) {
            IRQ_OFF();
            g.lastVal = SYST_VAL;
            const int16_t c = rxByte();
            IRQ_ON();
            if (c >= 0) feed((uint8_t)c);
            else        g.len = 0;
            idle = 0;
            if (g.newRmc) ui();         /* then the SKIP_MS window above */
            continue;
        }
        if (idle < IDLE_UI) { idle += ticksSince(); continue; }
        if (g.newRmc || A->ticks_ms() - g.lastUi >= UI_PERIOD_MS) ui();
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    A->cfg_load((uint8_t *)&g.cfg, sizeof g.cfg);
    if (g.cfg.magic != CFG_MAGIC || g.cfg.baud >= BAUD_COUNT || g.cfg.pin > 1u) {
        g.cfg.magic = CFG_MAGIC;
        g.cfg.baud = 1u;                          /* 9600 */
        g.cfg.pin = 0u;                           /* D+   */
        g.cfg.hasTarget = 0;
    }
    for (uint8_t k = 0; k < ID_TAIL; k++) g.idText[k] = g.cfg.id[k];   /* saved tail */
    readTarget();
    g.prevKey = A->get_key();
    g.load = SYST_LOAD;
    g.running = true;
    applyCfg();
    A->backlight_on();
    portSet(true);
    draw(A->ticks_ms());
    g.lastUi = A->ticks_ms();
    loop();
    portSet(false);
}
