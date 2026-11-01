/* Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
 * Licensed under the Apache License, Version 2.0 (see nav406_app.c).
 *
 * Nav 406 host test (run_tests.sh): the app source on a stub API. RMC to
 * centi-arcseconds, distance and azimuth against haversine (30 m to 89 km),
 * the four precision grids, the hand-off cases (EPIRB 406, Sig Finder, Apps
 * menu, out of range), the 16-byte save and reload, the screen texts, the
 * screen saver keys and the help page. FB_WALK / FB_STOP, when set, receive
 * the frame buffer (8 x 128 bytes) of the dial, walking and standing still.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#define __asm(...)
#define _Static_assert(...)
#define __attribute__(x)
#include APP_SRC

static char screen[8][40];
static char big[16];
static uint64_t m_uidivmod(uint32_t n, uint32_t d) { return (uint64_t)(n / d) | ((uint64_t)(n % d) << 32); }
static uint64_t m_idivmod(int32_t n, int32_t d) { return (uint32_t)(n / d) | ((uint64_t)(uint32_t)(n % d) << 32); }
static char *m_u32(char *o, uint32_t v) { return o + sprintf(o, "%u", v); }
static void m_print(const char *s, uint8_t a, uint8_t b, uint8_t l) { (void)a; (void)b; snprintf(screen[l], 40, "%s", s); }
static void m_bold(const char *s, uint8_t a, uint8_t b, uint8_t l) { m_print(s, a, b, l); }
static void m_freq(const char *s, uint8_t x, uint8_t y, bool sb) { (void)x;(void)y;(void)sb; snprintf(big, 16, "%s", s); }
static void m_nop(void) {}
static void m_inv(const char *s, uint8_t x, uint8_t l, bool sb, bool f, uint8_t e) { (void)s;(void)x;(void)l;(void)sb;(void)f;(void)e; }
static char tinyId[40];
static void m_tiny(const char *s, uint8_t x, uint8_t y, bool sb, bool f) { (void)x;(void)sb;(void)f; if (y == 10) snprintf(tinyId, 40, "%s", s); }
static int saves;
static uint8_t nextKey = APP_KEY_INVALID;
static uint8_t m_key(void) { return nextKey; }
static uint8_t fb[8][128];
static uint8_t assets[4096]; static size_t assetsLen;
static uint16_t m_aread(uint16_t off, void *buf, uint16_t len) { memcpy(buf, assets + off, len); return len; }
static void m_rect(app_fb_t f, int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool b)
{ (void)b; for (int y = y1; y <= y2; y++) for (int x = x1; x <= x2; x++) f[y >> 3][x] |= (uint8_t)(1u << (y & 7)); }
static void m_clear(void) { memset(fb, 0, sizeof fb); }
static void dump(const char *name) { if (!name) return; FILE *f = fopen(name, "wb"); fwrite(fb, 1, sizeof fb, f); fclose(f); }
/* the loader keeps APP_CONFIG_LOCAL_MAX bytes, as app_cfg_save() */
static uint8_t flash[16]; static uint8_t flashLen;
static void m_save(const uint8_t *b, uint8_t n) { if (n > 16) n = 16; memcpy(flash, b, n); flashLen = n; saves++; }
static void m_load(uint8_t *b, uint8_t n) { if (n > 16) n = 16; memcpy(b, flash, n); }

static uint8_t hoMsg[32]; static int16_t hoLen; static const char *hoFrom;
static int16_t m_hread(void *buf, uint8_t max, char *from)
{
    if (hoLen < 0) return -1;
    memcpy(buf, hoMsg, (size_t)(hoLen < max ? hoLen : max));
    if (from) strcpy(from, hoFrom);
    return hoLen;
}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void sentence(const char *body)
{
    uint8_t ck = 0; char out[120];
    for (const char *p = body; *p; p++) ck ^= (uint8_t)*p;
    snprintf(out, sizeof out, "$%s*%02X\r\n", body, ck);
    for (char *p = out; *p; p++) feed((uint8_t)*p);
}

static double hav(double la1, double lo1, double la2, double lo2, double *brg)
{
    const double r = M_PI / 180, R = 6371000;
    double p1 = la1 * r, p2 = la2 * r, dl = (lo2 - lo1) * r;
    double a = pow(sin((p2 - p1) / 2), 2) + cos(p1) * cos(p2) * pow(sin(dl / 2), 2);
    double y = sin(dl) * cos(p2), x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    *brg = fmod(atan2(y, x) / r + 360, 360);
    return 2 * R * asin(sqrt(a));
}
static int32_t cas(double deg) { return (int32_t)lround(deg * 360000); }

int main(void)
{
    static app_api_t api;
    api.uidivmod = m_uidivmod; api.idivmod = m_idivmod; api.append_u32 = m_u32;
    api.print_normal = m_print; api.print_bold = m_bold; api.display_freq = m_freq;
    api.display_clear = m_nop; api.status_clear = m_nop; api.draw_battery = m_nop;
    api.blit_status = m_nop; api.blit_full = m_nop; api.print_inverse = m_inv;
    api.print_tiny = m_tiny; api.cfg_save = m_save; api.cfg_load = m_load; api.handoff_read = m_hread;
    api.asset_read = m_aread; api.draw_rect = m_rect; api.fb = fb; api.display_clear = m_clear;
    FILE *af = fopen(getenv("ASSETS"), "rb"); assetsLen = fread(assets, 1, sizeof assets, af); fclose(af);
    api.get_key = m_key;
    g.api = &api;

    /* 1. RMC, the GN-801 bench sentence: 4916.15018N 00047.37472E */
    sentence("GNRMC,153109.00,A,4916.15018,N,00047.37472,E,0.162,,011126,,,A");
    CHECK(g.newRmc, "rmc");
    CHECK(g.hasPos && g.lat == 17736901, "lat %d", g.lat);   /* 49.269170 x 360000 */
    CHECK(g.lon == 284248, "lon %d", g.lon);                 /* 0.789579 x 360000 */
    CHECK(g.kmh10 == 3, "kmh10 %u", g.kmh10);       /* 0.162 kn = 0.30 km/h */
    sentence("GNRMC,153110.00,V,,,,,,,011126,,,N");
    g.newRmc = false;
    CHECK(!g.hasPos, "void fix kept a position");
    sentence("GNRMC,153111.00,A,3326.93340,S,07040.15590,W,3.0,123.4,011126,,,A");
    CHECK(g.lat == -cas(33.448890) && g.lon == -cas(70.669265), "S/W %d %d", g.lat, g.lon);
    CHECK(g.course == 123 && g.kmh10 == 55, "course %u kmh %u", g.course, g.kmh10);

    /* 2. distance and azimuth against haversine, 30 m to 80 km */
    const double h[2] = { 49.271113, 0.782088 };
    const double off[][2] = { {0.0003,0}, {0,0.0004}, {-0.002,0.003}, {0.01,-0.01},
                              {-0.05,-0.02}, {0.2,0.3}, {-0.5,0.4}, {0.7,-0.6} };
    for (unsigned i = 0; i < sizeof off / sizeof off[0]; i++) {
        double brg, ref = hav(h[0], h[1], h[0] + off[i][0], h[1] + off[i][1], &brg);
        uint16_t az;
        uint32_t d = navigation(cas(h[0]), cas(h[1]), cas(h[0] + off[i][0]), cas(h[1] + off[i][1]), &az);
        double da = fabs(fmod(az - brg + 540, 360) - 180);
        printf("  %8.0f m ref %8u m  az %3u ref %5.1f\n", ref, d, az, brg);
        CHECK(fabs(d - ref) <= 0.01 * ref + 3, "dist %u ref %.0f", d, ref);
        CHECK(da <= 1.5, "az %u ref %.1f", az, brg);
    }

    /* 3. precision from the grid */
    g.cfg.lat = cas(49.25); g.cfg.lon = cas(0.75);        /* 15' grid */
    CHECK(radius() == 19460, "15' %u", radius());
    g.cfg.lat = cas(49 + 16.0/60); g.cfg.lon = cas(47.0/60);  /* 2' grid only? 16' 47' */
    CHECK(radius() == 86 || radius() == 2594, "odd minutes %u", radius());
    g.cfg.lat = cas(49 + 16.0/60); g.cfg.lon = cas(48.0/60);  /* 4' grid: 16' and 48' */
    CHECK(radius() == 5189, "4' %u", radius());
    g.cfg.lat = cas(49 + 14.0/60); g.cfg.lon = cas(46.0/60);  /* 2' grid */
    CHECK(radius() == 2594, "2' %u", radius());
    g.cfg.lat = 17736900; g.cfg.lon = 284248;               /* fine */
    CHECK(radius() == 86, "fine %u", radius());

    /* 4. hand-off: from EPIRB 406 with an ID, then back from Sig Finder */
    int32_t pos[2] = { cas(49.2800), cas(0.7900) };
    memset(hoMsg, 0, sizeof hoMsg); memcpy(hoMsg, pos, 8);
    memcpy(hoMsg + 8, "9C7FEC2AACD3591", 16);
    hoLen = 24; hoFrom = "EPIRB 406"; memset(&g.cfg, 0, sizeof g.cfg); saves = 0;
    readTarget();
    CHECK(g.cfg.hasTarget && g.cfg.lat == pos[0] && g.cfg.lon == pos[1], "target");
    CHECK(!memcmp(g.cfg.id, "3591", 4) && g.fromEpirb && saves == 1, "id %.4s from %d", g.cfg.id, g.fromEpirb);
    CHECK(!strcmp(g.idText, "9C7FEC2AACD3591"), "full id in RAM |%s|", g.idText);
    CHECK(sizeof(cfg_t) <= 16, "cfg %zu B: cut by the loader", sizeof(cfg_t));
    /* manual start: everything comes back from the 16 saved bytes */
    cfg_t back; memset(&back, 0, sizeof back); m_load((uint8_t *)&back, sizeof back);
    CHECK(back.hasTarget && back.lat == pos[0] && back.lon == pos[1] && !memcmp(back.id, "3591", 4), "reload");
    memcpy(hoMsg + 8, "AB", 3); hoLen = 24; readTarget();
    CHECK(!memcmp(g.cfg.id, "  AB", 4), "short label |%.4s|", g.cfg.id);
    memcpy(hoMsg + 8, "9C7FEC2AACD3591", 16); readTarget();
    hoLen = 0; hoFrom = "Sig Finder"; g.fromEpirb = true; saves = 0;
    readTarget();
    CHECK(g.cfg.hasTarget && g.cfg.lat == pos[0] && g.fromEpirb && saves == 0, "kept after Sig Finder, EXIT to EPIRB 406");
    hoLen = -1; readTarget();
    CHECK(g.cfg.hasTarget, "menu launch keeps the target");
    pos[0] = 40000000; memcpy(hoMsg, pos, 8); hoLen = 24; hoFrom = "EPIRB 406";
    readTarget();
    CHECK(g.cfg.lat != 40000000, "out of range accepted");

    /* 5. screen: walking at 4.5 km/h heading 90, target 1.1 km NNE */
    g.lat = cas(h[0]); g.lon = cas(h[1]); g.hasPos = true;
    g.lastLineMs = 1000; g.kmh10 = 45; g.course = 90;
    memset(screen, 0, sizeof screen);
    draw(1500); dump(getenv("FB_WALK"));
    printf("  big |%s|\n", big);
    for (int i = 0; i < 7; i++) { printf("  |%-18s| %zu\n", screen[i], strlen(screen[i])); CHECK(strlen(screen[i]) <= 18, "row %d too long", i); }
    CHECK(!strcmp(big, "1.1") && !strcmp(screen[1], "km"), "big %s %s", big, screen[1]);
    CHECK(!strcmp(screen[2], "AZ 30"), "%s", screen[2]);
    CHECK(!strcmp(screen[3], "L 60"), "%s", screen[3]);
    CHECK(!strcmp(screen[4], "+-86m") && !strcmp(screen[5], "5km/h"), "%s %s", screen[4], screen[5]);
    g.course = 25; draw(1500);
    CHECK(!strcmp(screen[3], "AHEAD"), "%s", screen[3]);
    g.course = 300; draw(1500);
    CHECK(!strcmp(screen[3], "R 90"), "%s", screen[3]);
    g.kmh10 = 5; draw(1500); dump(getenv("FB_STOP"));
    CHECK(!strcmp(screen[3], "N UP"), "%s", screen[3]);
    g.cfg.hasTarget = 0; memset(screen, 0, sizeof screen); draw(1500);
    CHECK(!strcmp(screen[1], "NO TARGET") && !strcmp(screen[2], "EPIRB 406: KEY 2"), "%s", screen[1]);
    g.cfg.hasTarget = 1; g.lastLineMs = 0; draw(5000);
    CHECK(!strcmp(screen[5], "GPS NO DATA"), "%s", screen[5]);
    CHECK(!strcmp(screen[1], "TARGET SAVED"), "%s", screen[1]);

    /* 6. screen saver: SAVER is not a key (no relight), WAKE redraws */
    g.prevKey = APP_KEY_INVALID;
    nextKey = APP_KEY_SAVER;
    CHECK(!keys() && g.saver && g.prevKey == APP_KEY_INVALID, "saver taken as a key");
    CHECK(!keys() && g.saver, "saver twice");
    nextKey = APP_KEY_WAKE;
    CHECK(keys() && !g.saver, "wake must redraw");
    nextKey = APP_KEY_INVALID;
    CHECK(!keys(), "idle");

    /* 7. MENU: help with the GPS settings; 1 and 2 still apply; EXIT closes */
    g.prevKey = APP_KEY_INVALID; g.running = true; g.help = 0; g.cfg.baud = 1; g.cfg.pin = 0;
    nextKey = APP_KEY_MENU; CHECK(keys() && g.help, "menu opens help");
    memset(screen, 0, sizeof screen); draw(1500);
    CHECK(!strcmp(screen[0], "GPS 9600 8N1 D+"), "help line |%s|", screen[0]);
    CHECK(!strcmp(tinyId, "ID 9C7FEC2AACD3591"), "help id |%s|", tinyId);
    nextKey = APP_KEY_INVALID; keys();
    nextKey = APP_KEY_2; keys(); nextKey = APP_KEY_INVALID; keys();
    nextKey = APP_KEY_1; keys(); draw(1500);
    CHECK(!strcmp(screen[0], "GPS 19200 8N1 D-") && g.help, "help after 1, 2 |%s|", screen[0]);
    nextKey = APP_KEY_EXIT; CHECK(keys() && !g.help && g.running, "EXIT closes help only");
    nextKey = APP_KEY_INVALID; keys();
    nextKey = APP_KEY_MENU; keys(); nextKey = APP_KEY_INVALID; keys();
    nextKey = APP_KEY_MENU; CHECK(keys() && !g.help, "menu closes help");

    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
