/* Copyright 2026 — Apache-2.0
 * GeoNav: bearing and distance from the APRS TX home position to a target
 * handed off by a radiosonde receiver overlay.
 */
#include <stdint.h>
#include <stdbool.h>
#include "../app_api.h"
#include "../aprs_profile.h"
#include "../geonav_handoff.h"
#include "geonav_assets.h"

#define LAT_MAX_CAS  32400000L
#define LON_MAX_CAS  64800000L
#define CIRCLE_CAS  129600000L
#define BATTERY_MS       5000u
#define LOOP_MS            20u
#define CX     ((int16_t)DIAL_R)    /* dial centre, both axes */
#define COL_X              58u     /* right column, beside the dial */
#define RULE_Y             26u
#define ROW_ALT_Y          30u
#define ROW_EL_Y           38u
#define ROW_SCALE_Y        46u
#define STATUS_RIGHT       76u
#define ICON_X             53u
#define HINT_Y             50u
#define UI(off) (ui + (off) - T_TITLE)

static const app_api_t *A;

uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d) { return A->uidivmod(n, d); }
uint64_t __aeabi_uidiv(uint32_t n, uint32_t d) __attribute__((alias("__aeabi_uidivmod")));
uint64_t __aeabi_idivmod(int32_t n, int32_t d) { return A->idivmod(n, d); }
uint64_t __aeabi_idiv(int32_t n, int32_t d) __attribute__((alias("__aeabi_idivmod")));

static char *put(char *out, const char *s)
{
    while (*s) *out++ = *s++;
    return out;
}

static uint32_t abs32(int32_t value)
{
    return value < 0 ? (uint32_t)-value : (uint32_t)value;
}

static uint32_t isqrt32(uint32_t n)
{
    uint32_t root = 0u, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* Compass bearing of the (east, north) vector, in whole degrees. */
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

static bool home_position(int32_t *lat, int32_t *lon)
{
    uint8_t profile[APRS_PROFILE_COMMENT];
    for (uint8_t i = 0u; i < sizeof(profile); i++) profile[i] = 0u;
    A->cfg_load(profile, sizeof(profile));
    const uint32_t magic = (uint32_t)profile[0] |
                           ((uint32_t)profile[1] << 8) |
                           ((uint32_t)profile[2] << 16) |
                           ((uint32_t)profile[3] << 24);
    const uint8_t *d = profile + APRS_PROFILE_POS;
    if (magic != APRS_PROFILE_MAGIC || profile[APRS_PROFILE_HEMI] > 3u)
        return false;
    for (uint8_t i = 0u; i < APRS_POS_DIGITS; i++)
        if (d[i] > 9u) return false;

    const uint32_t lat_deg = d[0] * 10u + d[1];
    const uint32_t lat_min100 = d[2] * 1000u + d[3] * 100u + d[4] * 10u + d[5];
    const uint32_t lon_deg = d[6] * 100u + d[7] * 10u + d[8];
    const uint32_t lon_min100 = d[9] * 1000u + d[10] * 100u + d[11] * 10u + d[12];
    if (lat_deg > 90u || lat_min100 >= 6000u || lon_deg > 180u || lon_min100 >= 6000u ||
        (lat_deg == 90u && lat_min100) || (lon_deg == 180u && lon_min100))
        return false;

    *lat = (int32_t)(lat_deg * 360000u + lat_min100 * 60u);
    *lon = (int32_t)(lon_deg * 360000u + lon_min100 * 60u);
    if (profile[APRS_PROFILE_HEMI] & 1u) *lat = -*lat;
    if (profile[APRS_PROFILE_HEMI] & 2u) *lon = -*lon;
    return true;
}

static uint8_t slen(const char *s)
{
    const char *e = s;
    while (*e) e++;
    return (uint8_t)(e - s);
}

/* Cosine of 0..90 degrees, Q1000. */
static int32_t cosine(uint32_t deg)
{
    uint16_t c = 0u;
    A->asset_read((uint16_t)(COS_Q1000 + deg * 2u), &c, sizeof(c));
    return c;
}

static bool target_valid(const geonav_handoff_t *target, int16_t n)
{
    return n >= (int16_t)sizeof(*target) &&
           target->lat_cas >= -LAT_MAX_CAS && target->lat_cas <= LAT_MAX_CAS &&
           target->lon_cas >= -LON_MAX_CAS && target->lon_cas <= LON_MAX_CAS;
}

/* Equirectangular distance is accurate for the intended radiosonde range.
 * Scaling keeps the squared vector inside uint32_t even across a hemisphere. */
static uint32_t navigation(int32_t home_lat, int32_t home_lon,
                           int32_t target_lat, int32_t target_lon,
                           uint16_t *heading)
{
    int32_t north = target_lat - home_lat;
    int32_t east = target_lon - home_lon;
    if (east > LON_MAX_CAS) east -= CIRCLE_CAS;
    else if (east < -LON_MAX_CAS) east += CIRCLE_CAS;

    const int32_t mean_lat = home_lat / 2 + target_lat / 2;
    uint32_t lat_deg = (abs32(mean_lat) + 180000u) / 360000u;
    if (lat_deg > 90u) lat_deg = 90u;
    const int32_t cos_q1000 = cosine(lat_deg);

    uint32_t unit = 1u;
    while (abs32(north) / unit > 30000u || abs32(east) / unit > 30000u)
        unit *= 10u;
    north /= (int32_t)unit;
    east /= (int32_t)unit;
    east = east * cos_q1000 / 1000;
    *heading = bearing(east, north);

    const uint32_t an = abs32(north), ae = abs32(east);
    const uint32_t root_cas = isqrt32(an * an + ae * ae) * unit;
    /* 111200 m/degree divided by 360000 centi-arcseconds/degree = 139/450. */
    return (root_cas / 450u) * 139u + ((root_cas % 450u) * 139u + 225u) / 450u;
}

static void status(const char *ui, const char *label)
{
    A->status_clear();
    A->print_inverse(UI(T_TITLE), 2u, 0u, true, true,
                     (uint8_t)(2u + T_TITLE_CHARS * 4u));
    A->print_tiny(label, (uint8_t)(STATUS_RIGHT - slen(label) * 4u), 1u, true, true);
    A->draw_battery();
}

static void message_screen(const char *ui, uint16_t first, uint16_t second,
                           uint16_t hint)
{
    A->display_clear();
    for (uint8_t page = 0u; page < ICON_PAGES; page++)
        A->asset_read((uint16_t)(ICON + page * ICON_W), A->fb[page] + ICON_X, ICON_W);
    A->print_bold(UI(first), 0u, 127u, 4u);
    A->print_normal(UI(second), 0u, 127u, 5u);
    A->print_tiny(UI(hint), (uint8_t)((128u - slen(UI(hint)) * 4u) / 2u), HINT_Y,
                  false, true);
}

static void plot(int16_t x, int16_t y, bool black)
{
    if ((uint16_t)x >= 128u || (uint16_t)y >= 56u)
        return;
    uint8_t *b = &A->fb[y >> 3][x];
    const uint8_t mask = (uint8_t)(1u << (y & 7));
    *b = black ? (uint8_t)(*b | mask) : (uint8_t)(*b & ~mask);
}

/* Bresenham: the resident draw_line leaves gaps in steep lines. */
static void segment(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    const int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int16_t dy = y1 > y0 ? y0 - y1 : y1 - y0;   /* -|dy| */
    const int16_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;
    for (;;) {
        plot(x0, y0, true);
        if (x0 == x1 && y0 == y1) return;
        const int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Filled triangle {x0, y0, x1, y1, x2, y2}: every pixel on the inner side of
 * the three edges, edges included. */
static void triangle(const int16_t *p)
{
    int16_t x_lo = p[0], x_hi = p[0], y_lo = p[1], y_hi = p[1];
    for (uint8_t i = 2u; i < 6u; i += 2u) {
        if (p[i] < x_lo) x_lo = p[i];
        if (p[i] > x_hi) x_hi = p[i];
        if (p[i + 1u] < y_lo) y_lo = p[i + 1u];
        if (p[i + 1u] > y_hi) y_hi = p[i + 1u];
    }
    for (int16_t y = y_lo; y <= y_hi; y++)
        for (int16_t x = x_lo; x <= x_hi; x++) {
            bool neg = false, pos = false;
            for (uint8_t i = 0u; i < 6u; i += 2u) {
                const uint8_t j = i == 4u ? 0u : (uint8_t)(i + 2u);
                const int32_t e = (int32_t)(p[j] - p[i]) * (y - p[i + 1u]) -
                                  (int32_t)(p[j + 1u] - p[i + 1u]) * (x - p[i]);
                if (e < 0) neg = true;
                else if (e > 0) pos = true;
            }
            if (!(neg && pos)) plot(x, y, true);
        }
}

/* Dial point at horizontal radius r on azimuth deg (-359..719), north up. */
static void polar(int16_t r, int16_t deg, int16_t *x, int16_t *y)
{
    if (deg < 0) deg += 360;
    else if (deg >= 360) deg -= 360;
    const uint16_t quadrant = (uint16_t)deg / 90u, a = (uint16_t)deg % 90u;
    int32_t s = cosine(90u - a), c = cosine(a);
    for (uint16_t q = quadrant; q; q--) {   /* +90 degrees: (s, c) -> (c, -s) */
        const int32_t t = s;
        s = c;
        c = -t;
    }
    /* Pixels are taller than wide: vertical radii shrink by Y_NUM / Y_DEN. */
    const int32_t den = 1000 * (int32_t)DIAL_Y_DEN;
    s *= r;
    c *= r * (int32_t)DIAL_Y_NUM;
    *x = (int16_t)(CX + (s + (s < 0 ? -500 : 500)) / 1000);
    *y = (int16_t)(CX - (c + (c < 0 ? -den / 2 : den / 2)) / den);
}

static void row(const char *label, const char *value, uint8_t right, uint8_t y)
{
    A->print_tiny(label, COL_X + 2u, y, false, true);
    A->print_tiny(value, (uint8_t)(right - slen(value) * 4u), y, false, true);
}

static void degree_ring(uint8_t x, uint8_t y)
{
    A->draw_rect(A->fb, x, y, (int16_t)(x + 2u), (int16_t)(y + 2u), true);
}

static void draw_navigation(const char *ui, const geonav_handoff_t *target,
                            int32_t home_lat, int32_t home_lon)
{
    char line[24], table[T_DIR_STRIDE];
    char *out;
    int16_t p[6];
    uint16_t heading;
    const uint32_t distance_m = navigation(home_lat, home_lon,
                                           target->lat_cas, target->lon_cas, &heading);
    A->display_clear();

    /* Fixed dial, then the azimuth: a radial from home to the rim, ended by
     * an arrow tip on the graduation. */
    for (uint8_t page = 0u; page < DIAL_PAGES; page++)
        A->asset_read((uint16_t)(DIAL + page * DIAL_W), A->fb[page], DIAL_W);
    polar(CX - 6, (int16_t)heading, &p[0], &p[1]);
    segment(CX, CX, p[0], p[1]);
    polar(CX - 1, (int16_t)heading, &p[0], &p[1]);
    polar(CX - 7, (int16_t)((int16_t)heading - 9), &p[2], &p[3]);
    polar(CX - 7, (int16_t)((int16_t)heading + 9), &p[4], &p[5]);
    triangle(p);

    /* 1-2-5 range ring: twice the ring covers at least 1.25 x the distance,
     * so the target never reaches the arrow tip. */
    uint32_t ring_m = 100u;
    for (uint8_t k = 0u; ring_m * 8u < distance_m * 5u; k = k == 2u ? 0u : (uint8_t)(k + 1u))
        ring_m = k == 1u ? ring_m * 5u / 2u : ring_m * 2u;
    polar((int16_t)(distance_m * DIAL_RING / ring_m), (int16_t)heading, &p[0], &p[1]);
    for (int16_t dy = -3; dy <= 3; dy++)
        for (int16_t dx = -3; dx <= 3; dx++) {
            const int16_t d = (int16_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
            if (d <= 3) plot(p[0] + dx, p[1] + dy, d < 3);   /* blip in a white halo */
        }
    A->draw_rect(A->fb, CX - 1, CX - 1, CX + 1, CX + 1, true);

    /* Distance in big digits, its unit beside them. */
    const char *unit = UI(T_KM) + 1;
    out = line;
    if (distance_m < 1000u) {
        out = A->append_u32(out, distance_m); unit = UI(T_M) + 1;
    } else if (distance_m < 100000u) {
        out = A->append_u32(out, distance_m / 1000u); *out++ = '.';
        *out++ = (char)('0' + (distance_m % 1000u) / 100u);
    } else {
        out = A->append_u32(out, (distance_m + 500u) / 1000u);
    }
    *out = '\0';
    A->display_freq(line, COL_X, 0u, false);
    uint8_t x = COL_X;
    for (const char *c = line; *c; c++) x = (uint8_t)(x + (*c == '.' ? 3u : 13u));
    A->print_normal(unit, (uint8_t)(x + 2u), 0u, 1u);

    /* "AZ 327° NNW": tag, whole degrees, ring, 16-point direction. */
    const uint8_t direction = (uint8_t)(((uint32_t)heading * 2u + 22u) / 45u) & 15u;
    A->asset_read((uint16_t)(T_DIR + direction * T_DIR_STRIDE), table, T_DIR_STRIDE);
    A->print_inverse(UI(T_AZ), COL_X + 2u, 2u, false, true, COL_X + 10u);
    out = A->append_u32(line, heading); *out = '\0';
    A->print_bold(line, COL_X + 14u, 0u, 2u);
    x = (uint8_t)(COL_X + 14u + (uint8_t)(out - line) * 7u);
    degree_ring(x, 16u);
    A->print_normal(table, (uint8_t)(x + 5u), 0u, 2u);

    for (x = COL_X + 2u; x < 128u; x += 2u)
        A->fb[RULE_Y >> 3][x] |= (uint8_t)(1u << (RULE_Y & 7u));

    out = put(A->append_i32(line, target->alt_m), UI(T_M)); *out = '\0';
    row(UI(T_ALT), line, 128u, ROW_ALT_Y);

    /* Elevation over a flat horizon, from the target altitude. */
    out = A->append_u32(line, bearing(target->alt_m > 0 ? target->alt_m : 0,
                                      (int32_t)distance_m));
    *out = '\0';
    row(UI(T_EL), line, 123u, ROW_EL_Y);
    degree_ring(124u, ROW_EL_Y);

    out = ring_m < 1000u ? put(A->append_u32(line, ring_m), UI(T_M))
                         : put(A->append_u32(line, ring_m / 1000u), UI(T_KM));
    *out = '\0';
    row(UI(T_SCALE), line, 128u, ROW_SCALE_Y);
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    /* The sonde app's whole message, handed back unchanged on EXIT. */
    struct {
        geonav_handoff_t target;
        uint8_t rest[APP_HANDOFF_MAX - sizeof(geonav_handoff_t)];
    } msg;
    char from[APP_NAME_LEN + 1];
    char label[sizeof(msg.target.label) + 1u];
    char ui[UI_SIZE];
    int32_t home_lat = 0, home_lon = 0;
    A = api;
    const int16_t received = A->handoff_read(&msg, sizeof(msg), from);
    const bool have_target = target_valid(&msg.target, received);
    const bool have_home = home_position(&home_lat, &home_lon);
    /* The sonde ID goes to the status line: printable ASCII only. */
    uint8_t n = 0u;
    while (have_target && n < sizeof(msg.target.label) &&
           (uint8_t)(msg.target.label[n] - ' ') <= (uint8_t)('~' - ' ')) {
        label[n] = msg.target.label[n];
        n++;
    }
    label[n] = '\0';
    A->asset_read(T_TITLE, ui, UI_SIZE);
    A->backlight_on();
    A->battery_sample();
    status(ui, label);
    if (!have_target)
        message_screen(ui, T_NO_TARGET, T_OPEN_SONDE, T_BACK);
    else if (!have_home)
        message_screen(ui, T_QTH_MISSING, T_USE_APRS_EDIT, T_NEED_APRS_TX);
    else
        draw_navigation(ui, &msg.target, home_lat, home_lon);
    A->blit_status(); A->blit_full();

    uint8_t previous = APP_KEY_INVALID;
    uint32_t battery_at = A->ticks_ms();
    for (;;) {
        const uint8_t key = A->get_key();
        if (key != previous && key == APP_KEY_EXIT) {
            if (received >= 0 && from[0]) A->handoff(from, &msg, (uint8_t)received);
            return;
        }
        previous = key;
        const uint32_t now = A->ticks_ms();
        if (now - battery_at >= BATTERY_MS) {
            battery_at = now;
            A->battery_sample(); status(ui, label); A->blit_status();
        }
        A->backlight_update();
        A->delay_ms(LOOP_MS);
    }
}
