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

/*
 * VFO Test - exercises vfo_stage (API level 3). Each key stages one VFO
 * setting, starting from the current VFO (beam_get) so that the fields it does
 * not set keep their values, then returns: the loader applies it and the Apps
 * menu hands back to the main screen, where the result shows.
 *
 *   1  145.500 MHz FM, wide
 *   2  433.500 MHz FM, narrow
 *   3  121.500 MHz AM, then hand-off to Sig Finder: it must open on 121.500
 *   4  145.500 MHz staged twice: the second call must return false
 *   5  3.000 MHz, out of range: must return false, the app stays
 *   6  145.500 MHz FM wide, CTCSS 88.5 Hz on RX and TX
 *   7  145.500 MHz FM wide, DCS 023N on RX and TX
 *   8  RX 145.600 MHz, TX 600 kHz below (145.000 MHz): split, same band
 *   9  RX 145.500 MHz, TX 288 MHz above (433.500 MHz): split, other band
 *   0  read the current VFO back: RX, TX, offset, codes, power, step
 *   F  page 2 (and back), where:
 *      1  145.5125 MHz with a 25 kHz step: is the frequency kept or rounded?
 *      2  CTCSS 88.5 Hz on TX only (a repeater's access tone)
 *      3  CTCSS 67.0 Hz on RX, 88.5 Hz on TX
 *      4  DCS 023I (inverted) on RX and TX
 *      5  output power MID
 *      6  144.300 MHz USB
 *   EXIT  quit without staging: the VFO must not change
 *
 * Like Handoff A/B, an API test app: not built by "compile-app.sh all".
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    while (n--)
        *p++ = (uint8_t)c;
    return d;
}

static const app_api_t *A;
static bool page2;

static void line(const char *s, uint8_t row)
{
    A->print_normal(s, 0, 127, row);
}

static void show(const char *l1, const char *l2)
{
    A->display_clear();
    A->print_bold("VFO TEST", 0, 127, 0);
    if (l1) line(l1, 2);
    if (l2) line(l2, 3);
    if (page2) {
        A->print_tiny("P2 1 STEP 2 TXTONE 3 2TONES", 0, 41, false, true);
        A->print_tiny("4 DCSI 5 PWR 6 USB 0 RD F P1", 0, 49, false, true);
    } else {
        A->print_tiny("1 FMW 2 FMN 3 AM+SF 4 2X 5 OUT", 0, 41, false, true);
        A->print_tiny("6 CTC 7 DCS 8 SPL 9 XB 0 RD F P2", 0, 49, false, true);
    }
    A->blit_status();
    A->blit_full();
}

/* The current VFO with a new frequency, modulation, bandwidth, offset
 * (10 Hz units; direction 0 off, 1 up, 2 down) and code (type 0 off,
 * 1 CTCSS, 2 DCS; index in the firmware's table), on RX and TX. */
static bool stage_full(uint32_t f_10hz, uint8_t mod, uint8_t bw,
                       uint32_t offset, uint8_t dir, uint8_t ctype, uint8_t code)
{
    app_beam_channel_t ch;
    A->beam_get(&ch);
    ch.rx_frequency = f_10hz;
    ch.tx_offset_frequency = offset;
    ch.tx_offset_direction = dir;
    ch.modulation = mod;
    ch.channel_bandwidth = bw;
    ch.rx_codetype = ch.tx_codetype = ctype;
    ch.rx_code = ch.tx_code = code;
    return A->vfo_stage(&ch);
}

/* Page 2: codes per side, power and step (0xFF = keep the current one). */
static bool stage_ext(uint32_t f_10hz, uint8_t mod, uint8_t rxt, uint8_t rxc,
                      uint8_t txt, uint8_t txc, uint8_t power, uint8_t step)
{
    app_beam_channel_t ch;
    A->beam_get(&ch);
    ch.rx_frequency = f_10hz;
    ch.tx_offset_frequency = 0;
    ch.tx_offset_direction = 0;
    ch.modulation = mod;
    ch.channel_bandwidth = 0;
    ch.rx_codetype = rxt; ch.rx_code = rxc;
    ch.tx_codetype = txt; ch.tx_code = txc;
    if (power != 0xFFu) ch.output_power = power;
    if (step != 0xFFu) ch.step_setting = step;
    return A->vfo_stage(&ch);
}

static bool stage(uint32_t f_10hz, uint8_t mod, uint8_t bw)
{
    return stage_full(f_10hz, mod, bw, 0, 0, 0, 0);
}

/* "145.50000" from 10 Hz units: MHz, a point, then five digits */
static char *put_f(char *o, uint32_t f)
{
    uint64_t qr = A->uidivmod(f, 100000u);
    uint32_t r = (uint32_t)(qr >> 32);
    o = A->append_u32(o, (uint32_t)qr);
    *o++ = '.';
    for (uint32_t d = 10000u; d; d /= 10u) {
        uint64_t x = A->uidivmod(r, d);
        *o++ = (char)('0' + (uint32_t)x);
        r = (uint32_t)(x >> 32);
    }
    return o;
}

static void readback(void)
{
    app_beam_channel_t ch;
    char a[24], b[24], *o;
    A->beam_get(&ch);
    o = put_f(a, A->rx_freq()); *o++ = ' '; *o++ = 'R'; *o = 0;
    o = put_f(b, A->tx_freq()); *o++ = ' '; *o++ = 'T'; *o = 0;
    show(a, b);
    o = a;
    *o++ = 'O'; *o++ = 'F'; *o++ = 'F'; *o++ = ' ';
    o = put_f(o, ch.tx_offset_frequency);
    *o++ = ' '; *o++ = "-+-"[ch.tx_offset_direction % 3u]; *o = 0;
    line(a, 4);
    o = b;
    *o++ = 'C'; *o++ = '0' + ch.rx_codetype; *o++ = '/'; o = A->append_u32(o, ch.rx_code);
    *o++ = ' '; *o++ = 'T'; *o++ = '0' + ch.tx_codetype; *o++ = '/'; o = A->append_u32(o, ch.tx_code);
    *o++ = ' '; *o++ = "FAU"[ch.modulation % 3u]; *o++ = "WN"[ch.channel_bandwidth & 1u];
    *o++ = ' '; *o++ = 'P'; o = A->append_u32(o, ch.output_power);
    *o++ = 'S'; o = A->append_u32(o, ch.step_setting); *o = 0;
    line(b, 5);
    A->blit_full();
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    if (api->api_size < offsetof(app_api_t, vfo_stage) + sizeof(api->vfo_stage) ||
        !api->vfo_stage || !api->beam_get) {
        show("NO VFO_STAGE", "IN THIS FIRMWARE");
        while (api->get_key() != APP_KEY_EXIT) api->delay_ms(20);
        return;
    }
    show("PICK A CASE", NULL);
    uint8_t prev = APP_KEY_INVALID;
    for (;;) {
        uint8_t k = api->get_key();
        if (k == prev) { api->delay_ms(20); continue; }
        prev = k;
        if (k == APP_KEY_F) { page2 = !page2; show(page2 ? "PAGE 2" : "PAGE 1", NULL); continue; }
        if (page2 && k >= APP_KEY_1 && k <= APP_KEY_6) {
            static const char *const name[] = {
                "STEP 25K 145.5125", "TX TONE 88.5", "RX 67.0 TX 88.5",
                "DCS 023I", "POWER MID", "144.300 USB" };
            bool ok;
            switch (k) {
            case APP_KEY_1: ok = stage_ext(14551250u, 0, 0, 0, 0, 0, 0xFFu, 5u); break;
            case APP_KEY_2: ok = stage_ext(14550000u, 0, 0, 0, 1, 8, 0xFFu, 0xFFu); break;
            case APP_KEY_3: ok = stage_ext(14550000u, 0, 1, 0, 1, 8, 0xFFu, 0xFFu); break;
            case APP_KEY_4: ok = stage_ext(14550000u, 0, 3, 0, 3, 0, 0xFFu, 0xFFu); break;
            case APP_KEY_5: ok = stage_ext(14550000u, 0, 0, 0, 0, 0, 6u, 0xFFu); break;
            default:        ok = stage_ext(14430000u, 2, 0, 0, 0, 0, 0xFFu, 0xFFu); break;
            }
            show(name[k - APP_KEY_1], ok ? "STAGED" : "REFUSED");
            api->delay_ms(1500);
            return;
        }
        switch (k) {
        case APP_KEY_EXIT:
            return;
        case APP_KEY_1:
            show(stage(14550000u, 0, 0) ? "145.500 FM W OK" : "145.500 REFUSED", NULL);
            api->delay_ms(1500);
            return;
        case APP_KEY_2:
            show(stage(43350000u, 0, 1) ? "433.500 FM N OK" : "433.500 REFUSED", NULL);
            api->delay_ms(1500);
            return;
        case APP_KEY_3: {
            bool ok = stage(12150000u, 1, 0);
            bool h = ok && api->handoff("Sig Finder", NULL, 0);
            show(ok ? "121.500 AM OK" : "121.500 REFUSED",
                 h ? "HANDOFF SIGFINDER" : "NO SIG FINDER");
            api->delay_ms(1500);
            return;
        }
        case APP_KEY_4: {
            bool a = stage(14550000u, 0, 0);
            bool b = stage(43350000u, 0, 1);
            show(a ? "1ST TRUE" : "1ST FALSE (BAD)", b ? "2ND TRUE (BAD)" : "2ND FALSE OK");
            api->delay_ms(2500);
            return;                   /* the VFO must be 145.500 */
        }
        case APP_KEY_5:
            show(stage(300000u, 0, 0) ? "3.000 TRUE (BAD)" : "3.000 FALSE OK", "STILL HERE");
            break;
        case APP_KEY_6:
            show(stage_full(14550000u, 0, 0, 0, 0, 1, 8) ? "CTCSS 88.5 OK" : "CTCSS REFUSED", NULL);
            api->delay_ms(1500);
            return;
        case APP_KEY_7:
            show(stage_full(14550000u, 0, 0, 0, 0, 2, 0) ? "DCS 023N OK" : "DCS REFUSED", NULL);
            api->delay_ms(1500);
            return;
        case APP_KEY_8:
            show(stage_full(14560000u, 0, 0, 60000u, 2, 0, 0) ? "145.6 -600K OK" : "SPLIT REFUSED", NULL);
            api->delay_ms(1500);
            return;
        case APP_KEY_9:
            show(stage_full(14550000u, 0, 0, 28800000u, 1, 0, 0) ? "145.5 +288M OK" : "XBAND REFUSED", NULL);
            api->delay_ms(1500);
            return;
        case APP_KEY_0:
            readback();
            break;
        default:
            break;
        }
    }
}
