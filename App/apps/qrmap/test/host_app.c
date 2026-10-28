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
 */

/* Host build of the whole QR Map app on a stub API (test/check_app.py).
 *
 *   host_app <assets.bin> <message hex | none> <keys> <cfg byte>
 *
 * keys: one letter per press, U(p) D(own) 1-4 X (EXIT); EXIT is added at the
 * end. Prints every text drawn ("T x y text"), every frame ("F" then 64 rows
 * of 128 '#'/'.' pixels, status line on top), the hand-off ("H name len") and
 * the config saved ("C byte"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* app_main's section is ELF-only: keep the "used", drop the section. */
#define section(x) used
#include "../qrmap_app.c"
#undef section

static uint8_t fb[7][128], status[128], assets[4096], msg[64], cfgByte;
static int msgLen = -1;
static const char *keys;
static unsigned keyPoll;

static void s_display_clear(void){ memset(fb, 0, sizeof fb); }
static void s_status_clear(void){ memset(status, 0, sizeof status); }
static void s_print_tiny(const char *s, uint8_t x, uint8_t y, bool sb, bool fill){
    (void)fill;
    printf("T %u %u %s%s\n", x, y, sb ? "(status) " : "", s);
}
static void s_blit_full(void){
    printf("F\n");
    for (unsigned y = 0; y < 64; y++) {
        const uint8_t *p = y < 8 ? status : fb[(y >> 3) - 1];
        for (unsigned x = 0; x < 128; x++) putchar((p[x] >> (y & 7)) & 1 ? '#' : '.');
        putchar('\n');
    }
}
static void s_nop(void){}
static void s_delay(uint32_t ms){ (void)ms; }
static uint8_t s_get_key(void){
    /* each press: 2 polls down, then 2 released */
    const unsigned i = keyPoll++ / 4, down = keyPoll % 4 <= 2 && keyPoll % 4 != 0;
    const char k = i < strlen(keys) ? keys[i] : 'X';
    if (!down) return APP_KEY_INVALID;
    switch (k) {
    case 'U': return APP_KEY_UP;
    case 'D': return APP_KEY_DOWN;
    case 'X': return APP_KEY_EXIT;
    default:  return (uint8_t)(k - '0');
    }
}
static int8_t s_nav_dir(uint8_t k){ return k == APP_KEY_UP ? 1 : k == APP_KEY_DOWN ? -1 : 0; }
static void s_cfg_load(uint8_t *b, uint8_t n){ (void)n; b[0] = cfgByte; }
static void s_cfg_save(const uint8_t *b, uint8_t n){ (void)n; printf("C %u\n", b[0]); }
static uint16_t s_asset_read(uint16_t off, void *b, uint16_t n){ memcpy(b, assets + off, n); return n; }
static uint64_t s_uidivmod(uint32_t n, uint32_t d){ return (uint64_t)(n % d) << 32 | n / d; }
static char *s_append_u32(char *o, uint32_t u){ return o + sprintf(o, "%u", u); }
static int16_t s_handoff_read(void *b, uint8_t max, char *from){
    if (msgLen < 0) return -1;
    memcpy(b, msg, (size_t)msgLen < max ? (size_t)msgLen : max);
    strcpy(from, "EPIRB 406");
    return (int16_t)msgLen;
}
static bool s_handoff(const char *name, const void *m, uint8_t n){ (void)m; printf("H %s %u\n", name, n); return true; }

int main(int argc, char **argv){
    if (argc != 5) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f || !fread(assets, 1, sizeof assets, f)) return 2;
    fclose(f);
    if (strcmp(argv[2], "none")) {
        msgLen = 0;
        for (const char *p = argv[2]; p[0] && p[1]; p += 2) { unsigned b; sscanf(p, "%2x", &b); msg[msgLen++] = (uint8_t)b; }
    }
    keys = argv[3];
    cfgByte = (uint8_t)atoi(argv[4]);

    static app_api_t api;
    api.fb = fb; api.status_line = status;
    api.display_clear = s_display_clear; api.status_clear = s_status_clear;
    api.print_tiny = s_print_tiny; api.blit_full = s_blit_full; api.blit_status = s_nop;
    api.get_key = s_get_key; api.delay_ms = s_delay; api.nav_dir = s_nav_dir;
    api.cfg_load = s_cfg_load; api.cfg_save = s_cfg_save;
    api.draw_battery = s_nop; api.battery_sample = s_nop; api.backlight_on = s_nop;
    api.asset_read = s_asset_read; api.uidivmod = s_uidivmod; api.append_u32 = s_append_u32;
    api.handoff_read = s_handoff_read; api.handoff = s_handoff;
    app_main(&api);
    return 0;
}
