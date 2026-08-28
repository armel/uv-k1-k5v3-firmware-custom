/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
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
 * Plasma — overlay app. A full-screen demoscene plasma on the 1-bit 128x64 LCD.
 * The scalar field sums four scrolled sines: horizontal (x), vertical (y),
 * diagonal (x+y) and a radial ripple around a slowly drifting centre. Because
 * (x-cx)^2 + (y-cy)^2 is separable, the whole field is still built from per-column
 * and per-row tables, so each of the 8192 pixels costs only a handful of adds.
 * Rendered either with an ordered 4x4 Bayer dither (stipple) or as sweeping bands.
 * Pure compute + framebuffer, no radio — it just works.
 *
 * Keys: UP/DOWN speed · 1-5 pattern · STAR stipple/bands · F auto-cycle ·
 *       MENU pause · EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include "../app_api.h"

#define W 128
#define H 64

static const app_api_t *A;

/* 32 * sin(2*pi*i/256): four of these sum to -128..128. */
static const int8_t SIN[256] = {
      0,  1,  2,  2,  3,  4,  5,  5,  6,  7,  8,  9,  9, 10, 11, 12,
     12, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 20, 21, 21, 22,
     23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 27, 28, 28, 29, 29, 29,
     30, 30, 30, 30, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32, 32,
     32, 32, 32, 32, 32, 32, 32, 32, 31, 31, 31, 31, 31, 30, 30, 30,
     30, 29, 29, 29, 28, 28, 27, 27, 27, 26, 26, 25, 25, 24, 24, 23,
     23, 22, 21, 21, 20, 20, 19, 18, 18, 17, 16, 16, 15, 14, 14, 13,
     12, 12, 11, 10,  9,  9,  8,  7,  6,  5,  5,  4,  3,  2,  2,  1,
      0, -1, -2, -2, -3, -4, -5, -5, -6, -7, -8, -9, -9,-10,-11,-12,
    -12,-13,-14,-14,-15,-16,-16,-17,-18,-18,-19,-20,-20,-21,-21,-22,
    -23,-23,-24,-24,-25,-25,-26,-26,-27,-27,-27,-28,-28,-29,-29,-29,
    -30,-30,-30,-30,-31,-31,-31,-31,-31,-32,-32,-32,-32,-32,-32,-32,
    -32,-32,-32,-32,-32,-32,-32,-32,-31,-31,-31,-31,-31,-30,-30,-30,
    -30,-29,-29,-29,-28,-28,-27,-27,-27,-26,-26,-25,-25,-24,-24,-23,
    -23,-22,-21,-21,-20,-20,-19,-18,-18,-17,-16,-16,-15,-14,-14,-13,
    -12,-12,-11,-10, -9, -9, -8, -7, -6, -5, -5, -4, -3, -2, -2, -1,
};

/* 4x4 ordered-dither matrix, flattened: idx = (y&3)*4 + (x&3), values 0..15. */
static const uint8_t BAYER[16] = { 0,8,2,10, 12,4,14,6, 3,11,1,9, 15,7,13,5 };

/* Pattern presets: {x scale, y scale, diagonal scale, radial ring shift}. */
#define NVAR 5
static const uint8_t VAR[NVAR][4] = {
    {4,4,3,5}, {6,3,5,4}, {3,7,2,6}, {5,5,4,5}, {2,8,6,4},
};

/* Per-frame separable tables. */
static int8_t   colA[W];        /* horizontal sine, by column x */
static int8_t   rowA[H];        /* vertical sine, by row y       */
static int8_t   diagA[W + H];   /* diagonal sine, by (x + y)     */
static uint16_t sqx[W];         /* (x - cx)^2 for the radial term */
static uint16_t sqy[H];         /* (y - cy)^2                     */

static uint8_t prevKey;

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    A->backlight_on();
    A->status_clear();

    uint16_t t1 = 0, t2 = 0, t3 = 0, tr = 0, tc = 0, tc2 = 0;
    uint16_t autoCtr = 0;
    uint8_t  speed = 3;
    uint8_t  var   = 0;
    bool     bands = false;
    bool     autoc = false;
    bool     paused = false;
    bool     running = true;
    prevKey = APP_KEY_INVALID;

    while (running) {
        const uint8_t sx = VAR[var][0], sy = VAR[var][1];
        const uint8_t sd = VAR[var][2], rsh = VAR[var][3];

        /* drifting radial centre (gentle Lissajous), then the separable squares */
        const int cx = 64 + SIN[(uint8_t)tc];        /* 32..96 */
        const int cy = 32 + (SIN[(uint8_t)tc2] >> 1);/* 16..48 */

        for (uint8_t x = 0; x < W; x++) {
            colA[x] = SIN[(uint8_t)(x * sx + t1)];
            int dx = (int)x - cx; sqx[x] = (uint16_t)(dx * dx);
        }
        for (uint8_t y = 0; y < H; y++) {
            rowA[y] = SIN[(uint8_t)(y * sy + t2)];
            int dy = (int)y - cy; sqy[y] = (uint16_t)(dy * dy);
        }
        for (uint16_t s = 0; s < W + H; s++) diagA[s] = SIN[(uint8_t)(s * sd + t3)];

        /* render all 8 pages: page 0 -> status line (top), pages 1..7 -> fb[0..6] */
        for (uint8_t x = 0; x < W; x++) {
            const int8_t  cxv = colA[x];
            const uint16_t sxv = sqx[x];
            const uint8_t bx  = x & 3u;
            for (uint8_t p = 0; p < 8u; p++) {
                uint8_t byte = 0;
                for (uint8_t b = 0; b < 8u; b++) {
                    const uint8_t y = (uint8_t)(p * 8u + b);
                    int v = cxv + rowA[y] + diagA[x + y]
                          + SIN[(uint8_t)(((sxv + sqy[y]) >> rsh) + tr)];
                    v += 128;                                     /* 0..256 */
                    bool on;
                    if (bands) on = ((v >> 4) & 1u) != 0u;        /* sweeping stripes */
                    else {
                        int lvl = v >> 4; if (lvl > 15) lvl = 15;  /* 0..15 stipple */
                        on = lvl > (int)BAYER[((y & 3u) << 2) | bx];
                    }
                    if (on) byte = (uint8_t)(byte | (1u << b));
                }
                if (p == 0) A->status_line[x] = byte;
                else        A->fb[p - 1][x]   = byte;
            }
        }
        A->blit_status();
        A->blit_full();

        if (!paused) {
            t1 += speed; t2 += (uint16_t)(speed + 1u); t3 += 1u;
            tr += speed; tc += 1u; tc2 += 2u;
            if (autoc && ++autoCtr >= 400u) { autoCtr = 0; var = (uint8_t)((var + 1u) % NVAR); }
        }

        const uint8_t key = A->get_key();
        if (key != prevKey && key != APP_KEY_INVALID) {
            A->backlight_on();
            switch (key) {
                case APP_KEY_EXIT: running = false; break;
                case APP_KEY_UP:   if (speed < 8u) speed++; break;
                case APP_KEY_DOWN: if (speed > 1u) speed--; break;
                case APP_KEY_MENU: paused = !paused; break;
                case APP_KEY_STAR: bands = !bands; break;
                case APP_KEY_F:    autoc = !autoc; break;
                case APP_KEY_1: case APP_KEY_2: case APP_KEY_3:
                case APP_KEY_4: case APP_KEY_5:
                    var = (uint8_t)(key - APP_KEY_1); autoc = false; break;
                default: break;
            }
        }
        prevKey = key;
        A->backlight_update();
    }
}
