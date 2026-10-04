/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../App/apps/aprstx/ui.h"
#include "../../App/apps/aprstx/tx.h"
static aprs_model_t model(void)
{
    aprs_model_t m;
    aprs_defaults(&m);
    memcpy(m.call, "N0CALL", 7);
    m.ssid = 9; m.lat = 334500; m.lon = 225700; m.valid = 1;
    return m;
}
static int8_t nav(uint8_t k) { return k == APP_KEY_UP ? 1 : (k == APP_KEY_DOWN ? -1 : 0); }
static void keys(aprs_ui_t *u, const char *s)
{
    app_api_t a = { .nav_dir = nav };
    for (; *s; ++s) aprs_ui_key(&a, u, *s - '0');
}
static void protocol_tests(void)
{
    static const uint8_t vector[] = {
        0x82,0xA0,0xB4,0x9E,0xAC,0x62,0xE0,0x9C,0x60,0x86,0x82,0x98,0x98,0x73,3,0xF0,
        0x21,0x35,0x35,0x34,0x35,0x2E,0x30,0x30,0x4E,0x2F,0x30,0x33,0x37,0x33,0x37,0x2E,
        0x30,0x30,0x45,0x3E,0x54,0x45,0x53,0x54,0xD6,0xFD
    };
    aprs_model_t m = model(), decoded;
    uint8_t raw[16], damaged[16], f[128], bits[2048], b;
    assert((aprs_crc16((const uint8_t *)"123456789", 9) ^ 0xFFFF) == 0x906E);
    uint16_t length = aprs_frame_build(&m, "TEST", f);
    assert(length == sizeof(vector) && !memcmp(f, vector, length));
    assert(aprs_crc16(f, length) == 0xF0B8);
    aprs_hdlc_t h;
    aprs_hdlc_init(&h, f, length, 0, 0);
    unsigned n = 0;
    while (aprs_hdlc_next(&h, &b)) bits[n++] = b;
    assert(n == 339);
    /* Independent unstuffing round trip, including byte boundaries. */
    uint8_t out[128] = {0}; unsigned at = 0, ones = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (ones == 5) { assert(bits[i] == 0); ones = 0; continue; }
        out[at / 8] |= bits[i] << (at % 8); ++at;
        ones = bits[i] ? ones + 1 : 0;
    }
    assert(at == length * 8 && !memcmp(out, f, length));
    uint8_t last_ones[] = {0xF8};
    aprs_hdlc_init(&h, last_ones, 1, 1, 1); n = 0;
    while (aprs_hdlc_next(&h, &b)) bits[n++] = b;
    assert(n == 25 && bits[16] == 0); /* pending stuffed zero before tail */
    for (unsigned i = 0; i < 8; ++i) {
        assert(bits[i] == ((0x7E >> i) & 1));
        assert(bits[17+i] == ((0x7E >> i) & 1));
    }
    for (uint8_t path = 0; path < 4; ++path) {
        m.path = path; length = aprs_frame_build(&m, "", f);
        unsigned addresses = path == 2 ? 4 : (path ? 3 : 2);
        for (unsigned i = 0; i < addresses; ++i) {
            assert((f[i*7+6] & 1) == (i + 1 == addresses));
            assert((f[i*7+6] & 0x80) == (i ? 0 : 0x80));
        }
        assert(aprs_crc16(f, length) == 0xF0B8);
    }
    m.path = 2;
    assert(aprs_frame_build(&m, "1234567890123456789012345678901234567890123", f) == 95);
    assert(!aprs_frame_build(&m, "12345678901234567890123456789012345678901234", f));
    assert(!aprs_frame_build(&m, "non ASCII\xC3\xA9", f));
    const int32_t latitude[] = {-540000,-1,0,334500,540000};
    const int32_t longitude[] = {-1080000,-1,0,225700,1080000};
    for (unsigned i = 0; i < 5; ++i) for (unsigned j = 0; j < 5; ++j) {
        m.lat = latitude[i]; m.lon = longitude[j];
        aprs_cfg_encode16(&m, raw); assert(aprs_cfg_decode16(raw, &decoded));
        assert(!memcmp(&m, &decoded, sizeof(m)));
        for (unsigned k = 0; k < 16; ++k) for (unsigned bit = 0; bit < 8; ++bit) {
            memcpy(damaged, raw, 16); damaged[k] ^= 1u << bit;
            assert(!aprs_cfg_decode16(damaged, &decoded));
        }
    }
    char c[10];
    aprs_coordinate(-540000, false, c); assert(!strcmp(c, "9000.00S"));
    aprs_coordinate(1080000, true, c); assert(!strcmp(c, "18000.00E"));
    aprs_coordinate(0, false, c); assert(!strcmp(c, "0000.00N"));
    m.lat = 540001; assert(!aprs_frame_build(&m, "", f));
    assert(aprs_call_valid("A") && aprs_call_valid("N0CALL"));
    assert(!aprs_call_valid("") && !aprs_call_valid("TOOLONG") && !aprs_call_valid("A/B"));
}
static void ui_tests(void)
{
    app_api_t a = { .nav_dir = nav };
    aprs_ui_t u = { .model = model() };
    aprs_ui_init(&u); aprs_ui_key(&a, &u, APP_KEY_MENU);
    u.field = 2; aprs_ui_key(&a, &u, APP_KEY_MENU);
    keys(&u, "906000"); aprs_ui_key(&a, &u, APP_KEY_MENU);
    assert(u.editing && u.model.lat == 334500); /* minutes 60 rejected */
    aprs_ui_key(&a, &u, APP_KEY_EXIT); aprs_ui_key(&a, &u, APP_KEY_MENU);
    keys(&u, "900001"); aprs_ui_key(&a, &u, APP_KEY_MENU); assert(u.editing);
    aprs_ui_key(&a, &u, APP_KEY_EXIT); aprs_ui_key(&a, &u, APP_KEY_MENU);
    keys(&u, "900000"); aprs_ui_key(&a, &u, APP_KEY_F);
    aprs_ui_key(&a, &u, APP_KEY_MENU); assert(!u.editing && u.model.lat == -540000);
    u.field = 3; aprs_ui_key(&a, &u, APP_KEY_MENU); keys(&u, "1800000");
    aprs_ui_key(&a, &u, APP_KEY_MENU); assert(!u.editing && u.model.lon == 1080000);
    u.field = 1; aprs_ui_key(&a, &u, APP_KEY_MENU); keys(&u, "16");
    aprs_ui_key(&a, &u, APP_KEY_MENU); assert(u.editing && u.model.ssid == 9);
    aprs_ui_key(&a, &u, APP_KEY_EXIT); u.field = 0;
    aprs_ui_key(&a, &u, APP_KEY_MENU); keys(&u, "222");
    assert(!strcmp(u.edit, "C"));
    for (unsigned i = 0; i < 35; ++i) aprs_ui_tick(&u);
    keys(&u, "2"); assert(!strcmp(u.edit, "CA"));
    aprs_ui_key(&a, &u, APP_KEY_F); keys(&u, "9"); assert(!strcmp(u.edit, "CA9"));
    aprs_ui_key(&a, &u, APP_KEY_EXIT); assert(!strcmp(u.model.call, "N0CALL"));
    u.field = 5; aprs_ui_key(&a, &u, APP_KEY_MENU);
    aprs_ui_key(&a, &u, APP_KEY_UP); aprs_ui_key(&a, &u, APP_KEY_F);
    aprs_ui_key(&a, &u, APP_KEY_MENU); assert(u.model.symbol == '?' && u.model.table == 1);
}
/* Exercise the actual application lifecycle with fake RF callbacks. The
 * physical PTT is held on entry; only two later fresh presses may send. */
static unsigned tick, sends, saves;
bool aprs_stock_ptt(void) { return tick < 5 || (tick >= 8 && tick < 15) || tick >= 18; }
bool aprs_stock_clock_valid(void) { return true; }
uint8_t aprs_stock_cca(const app_api_t *a) { (void)a; return APRS_SENT; }
uint8_t aprs_stock_send(const app_api_t *a, const uint8_t *f, uint16_t n, uint32_t freq)
{ (void)a; assert(aprs_crc16(f,n) == 0xF0B8 && freq == 14439000); ++sends; return APRS_SENT; }
void aprs_stock_cleanup(const app_api_t *a) { (void)a; }
static uint8_t key(void) { return tick == 30 ? APP_KEY_EXIT : APP_KEY_INVALID; }
static void delay(uint32_t ms) { assert(ms == 20); ++tick; }
static void nothing(void) {}
static void tiny(const char *s, uint8_t x, uint8_t y, bool status, bool fill)
{ (void)s;(void)x;(void)y;(void)status; assert(fill); }
static uint8_t allowed(void) { return 0; }
static uint32_t frequency(void) { return 14439000; }
static void load(uint8_t *r, uint8_t n) { aprs_model_t m = model(); assert(n == 16); aprs_cfg_encode16(&m,r); }
static void save(const uint8_t *r, uint8_t n) { (void)r;(void)n;++saves; }
void app_main(const app_api_t *api);
int main(void)
{
    protocol_tests(); ui_tests();
    app_api_t a = {.abi_major=1,.api_level=1,.api_size=sizeof(app_api_t),
        .get_key=key,.delay_ms=delay,.display_clear=nothing,.status_clear=nothing,
        .print_tiny=tiny,.draw_battery=nothing,.blit_full=nothing,.blit_status=nothing,
        .backlight_on=nothing,.backlight_update=nothing,.battery_sample=nothing,
        .cfg_load=load,.cfg_save=save,.tx_state=allowed,.tx_freq=frequency,
        .rx_freq=frequency,.nav_dir=nav};
    app_main(&a); assert(sends == 2 && saves == 0);
    puts("APRS protocol/config/editor/fresh-PTT tests passed");
}
