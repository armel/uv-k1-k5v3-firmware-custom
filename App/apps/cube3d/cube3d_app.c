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
 * Cube3D — overlay app. A real-time rotating solid on the 1-bit 128x64 LCD.
 * Vertices are spun by three axis rotations in Q14 fixed point (Cortex-M0+ has
 * no FPU and no hardware divide), then perspective-projected with a single
 * divide per vertex. Two looks, toggled with F:
 *   - SOLID: hidden-line removal by back-face culling (a face is drawn only when
 *     the signed area of its projected polygon shows it facing us).
 *   - WIRE : every edge, with the far hemisphere dotted for a depth cue.
 * Edges use a self-clipped Bresenham writing the full 64 rows directly, since the
 * resident pixel helper does not bound-check. The eight solids' faces (with a
 * uniform outward winding) are generated offline by a convex-hull extractor, so
 * nothing here has to be hand-wound. Pure compute, no radio.
 *
 * Keys: UP/DOWN speed · 1-8 shape · STAR next shape · F solid/wire ·
 *       MENU pause · EXIT quit.
 */

#include <stdint.h>
#include <stdbool.h>
#include "../app_api.h"

#define W       128
#define H       64
#define CX      64          /* projection centre x */
#define CY      32          /* projection centre y */
#define DIST    150         /* camera distance along +z (keeps zc > 0)   */
#define FOCAL   80          /* focal length / field-of-view scale        */
#define MAXV    12          /* largest vertex count across the solids     */

static const app_api_t *A;

/* Q14 sine: round(16384 * sin(2*pi*i/256)). cos(i) == SIN[(i + 64) & 255]. */
static const int16_t SIN[256] = {
         0,   402,   804,  1205,  1606,  2006,  2404,  2801,  3196,  3590,  3981,
      4370,  4756,  5139,  5520,  5897,  6270,  6639,  7005,  7366,  7723,  8076,
      8423,  8765,  9102,  9434,  9760, 10080, 10394, 10702, 11003, 11297, 11585,
     11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256,
     14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893,
     15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384, 16379,
     16364, 16340, 16305, 16261, 16207, 16143, 16069, 15986, 15893, 15791, 15679,
     15557, 15426, 15286, 15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842,
     13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866, 11585, 11297, 11003,
     10702, 10394, 10080,  9760,  9434,  9102,  8765,  8423,  8076,  7723,  7366,
      7005,  6639,  6270,  5897,  5520,  5139,  4756,  4370,  3981,  3590,  3196,
      2801,  2404,  2006,  1606,  1205,   804,   402,     0,  -402,  -804, -1205,
     -1606, -2006, -2404, -2801, -3196, -3590, -3981, -4370, -4756, -5139, -5520,
     -5897, -6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765, -9102, -9434,
     -9760,-10080,-10394,-10702,-11003,-11297,-11585,-11866,-12140,-12406,-12665,
    -12916,-13160,-13395,-13623,-13842,-14053,-14256,-14449,-14635,-14811,-14978,
    -15137,-15286,-15426,-15557,-15679,-15791,-15893,-15986,-16069,-16143,-16207,
    -16261,-16305,-16340,-16364,-16379,-16384,-16379,-16364,-16340,-16305,-16261,
    -16207,-16143,-16069,-15986,-15893,-15791,-15679,-15557,-15426,-15286,-15137,
    -14978,-14811,-14635,-14449,-14256,-14053,-13842,-13623,-13395,-13160,-12916,
    -12665,-12406,-12140,-11866,-11585,-11297,-11003,-10702,-10394,-10080, -9760,
     -9434, -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639, -6270, -5897,
     -5520, -5139, -4756, -4370, -3981, -3590, -3196, -2801, -2404, -2006, -1606,
     -1205,  -804,  -402,
};

/* A face is a polygon of up to 6 vertex indices, wound CCW as seen from outside
 * (generated offline by the hull extractor, so the signed-area cull sign is the
 * same for every solid). Unused slots are 0-padded and ignored (n gives length). */
typedef struct { uint8_t n; uint8_t v[6]; } face_t;

static const int8_t CUBE_V[8][3]={{-26,-26,-26},{26,-26,-26},{26,26,-26},{-26,26,-26},{-26,-26,26},{26,-26,26},{26,26,26},{-26,26,26}};
static const face_t CUBE_F[6]={{4,{2,1,0,3,0,0}},{4,{4,0,1,5,0,0}},{4,{7,3,0,4,0,0}},{4,{5,1,2,6,0,0}},{4,{6,2,3,7,0,0}},{4,{7,4,5,6,0,0}}};

static const int8_t OCTAHEDRON_V[6][3]={{38,0,0},{-38,0,0},{0,38,0},{0,-38,0},{0,0,38},{0,0,-38}};
static const face_t OCTAHEDRON_F[8]={{3,{4,0,2,0,0,0}},{3,{2,0,5,0,0,0}},{3,{3,0,4,0,0,0}},{3,{5,0,3,0,0,0}},{3,{2,1,4,0,0,0}},{3,{5,1,2,0,0,0}},{3,{4,1,3,0,0,0}},{3,{3,1,5,0,0,0}}};

static const int8_t TETRAHEDRON_V[4][3]={{28,28,28},{28,-28,-28},{-28,28,-28},{-28,-28,28}};
static const face_t TETRAHEDRON_F[4]={{3,{2,0,1,0,0,0}},{3,{1,0,3,0,0,0}},{3,{3,0,2,0,0,0}},{3,{2,1,3,0,0,0}}};

static const int8_t DIAMOND_V[8][3]={{30,0,0},{14,26,0},{-14,26,0},{-30,0,0},{-14,-26,0},{14,-26,0},{0,0,40},{0,0,-40}};
static const face_t DIAMOND_F[12]={{3,{6,0,1,0,0,0}},{3,{1,0,7,0,0,0}},{3,{5,0,6,0,0,0}},{3,{7,0,5,0,0,0}},{3,{6,1,2,0,0,0}},{3,{2,1,7,0,0,0}},{3,{6,2,3,0,0,0}},{3,{3,2,7,0,0,0}},{3,{6,3,4,0,0,0}},{3,{4,3,7,0,0,0}},{3,{6,4,5,0,0,0}},{3,{5,4,7,0,0,0}}};

static const int8_t ICOSAHEDRON_V[12][3]={{0,18,29},{0,18,-29},{0,-18,29},{0,-18,-29},{18,29,0},{18,-29,0},{-18,29,0},{-18,-29,0},{29,0,18},{29,0,-18},{-29,0,18},{-29,0,-18}};
static const face_t ICOSAHEDRON_F[20]={{3,{8,0,2,0,0,0}},{3,{2,0,10,0,0,0}},{3,{6,0,4,0,0,0}},{3,{4,0,8,0,0,0}},{3,{10,0,6,0,0,0}},{3,{3,1,9,0,0,0}},{3,{11,1,3,0,0,0}},{3,{4,1,6,0,0,0}},{3,{9,1,4,0,0,0}},{3,{6,1,11,0,0,0}},{3,{5,2,7,0,0,0}},{3,{8,2,5,0,0,0}},{3,{7,2,10,0,0,0}},{3,{7,3,5,0,0,0}},{3,{5,3,9,0,0,0}},{3,{11,3,7,0,0,0}},{3,{9,4,8,0,0,0}},{3,{8,5,9,0,0,0}},{3,{10,6,11,0,0,0}},{3,{11,7,10,0,0,0}}};

static const int8_t CUBOCTA_V[12][3]={{-24,-24,0},{-24,24,0},{24,-24,0},{24,24,0},{-24,0,-24},{-24,0,24},{24,0,-24},{24,0,24},{0,-24,-24},{0,-24,24},{0,24,-24},{0,24,24}};
static const face_t CUBOCTA_F[14]={{4,{4,0,5,1,0,0}},{4,{2,9,0,8,0,0}},{3,{8,0,4,0,0,0}},{3,{5,0,9,0,0,0}},{4,{10,1,11,3,0,0}},{3,{4,1,10,0,0,0}},{3,{11,1,5,0,0,0}},{4,{3,7,2,6,0,0}},{3,{6,2,8,0,0,0}},{3,{9,2,7,0,0,0}},{3,{10,3,6,0,0,0}},{3,{7,3,11,0,0,0}},{4,{8,4,10,6,0,0}},{4,{7,11,5,9,0,0}}};

static const int8_t HEXPRISM_V[12][3]={{26,0,24},{12,22,24},{-12,22,24},{-26,0,24},{-12,-22,24},{12,-22,24},{26,0,-24},{12,22,-24},{-12,22,-24},{-26,0,-24},{-12,-22,-24},{12,-22,-24}};
static const face_t HEXPRISM_F[8]={{6,{4,5,0,1,2,3}},{4,{7,1,0,6,0,0}},{4,{6,0,5,11,0,0}},{4,{8,2,1,7,0,0}},{4,{9,3,2,8,0,0}},{4,{10,4,3,9,0,0}},{4,{11,5,4,10,0,0}},{6,{9,8,7,6,11,10}}};

static const int8_t PENTAGEM_V[7][3]={{28,0,0},{8,26,0},{-22,16,0},{-22,-16,0},{8,-26,0},{0,0,42},{0,0,-42}};
static const face_t PENTAGEM_F[10]={{3,{5,0,1,0,0,0}},{3,{1,0,6,0,0,0}},{3,{4,0,5,0,0,0}},{3,{6,0,4,0,0,0}},{3,{5,1,2,0,0,0}},{3,{2,1,6,0,0,0}},{3,{5,2,3,0,0,0}},{3,{3,2,6,0,0,0}},{3,{5,3,4,0,0,0}},{3,{4,3,6,0,0,0}}};

typedef struct {
    const int8_t (*v)[3];
    const face_t *f;
    uint8_t nv;
    uint8_t nf;
    const char *name;
    uint8_t namelen;
} shape_t;

#define NSHAPE 8
static const shape_t SHAPES[NSHAPE] = {
    { CUBE_V,        CUBE_F,         8,  6, "CUBE",         4 },
    { OCTAHEDRON_V,  OCTAHEDRON_F,   6,  8, "OCTAHEDRON",  10 },
    { TETRAHEDRON_V, TETRAHEDRON_F,  4,  4, "TETRAHEDRON", 11 },
    { DIAMOND_V,     DIAMOND_F,      8, 12, "DIAMOND",      7 },
    { ICOSAHEDRON_V, ICOSAHEDRON_F, 12, 20, "ICOSAHEDRON", 11 },
    { CUBOCTA_V,     CUBOCTA_F,     12, 14, "CUBOCTA",      7 },
    { HEXPRISM_V,    HEXPRISM_F,    12,  8, "HEXPRISM",     8 },
    { PENTAGEM_V,    PENTAGEM_F,     7, 10, "PENTAGEM",     8 },
};

/* Per-frame projected screen coords + rotated depth of each vertex. */
static int16_t px[MAXV], py[MAXV], pz[MAXV];

/* Set one pixel across the full 64 rows: 0..7 -> status line, 8..63 -> fb. */
static void set_pixel(int x, int y)
{
    if ((unsigned)x >= W || (unsigned)y >= H)
        return;
    const uint8_t bit = (uint8_t)(1u << (y & 7));
    if (y < 8)
        A->status_line[x] |= bit;
    else
        A->fb[(y >> 3) - 1][x] |= bit;
}

/* Integer Bresenham; dotted skips every other step for the depth cue. */
static void draw_edge(int x0, int y0, int x1, int y1, bool dotted)
{
    const int dx =  (x1 > x0 ? x1 - x0 : x0 - x1);
    const int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    const int sx =  (x0 < x1 ? 1 : -1);
    const int sy =  (y0 < y1 ? 1 : -1);
    int err = dx + dy;
    unsigned step = 0;
    for (;;) {
        if (!dotted || (step & 1u) == 0u)
            set_pixel(x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        step++;
    }
}

static void clear_screen(void)
{
    for (uint8_t x = 0; x < W; x++) {
        A->status_line[x] = 0;
        for (uint8_t p = 0; p < 7u; p++)
            A->fb[p][x] = 0;
    }
}

/* Signed area of a face's projected polygon (<0 == facing us, calibrated). */
static int face_area(const face_t *f)
{
    int sa = 0;
    for (uint8_t k = 0; k < f->n; k++) {
        const uint8_t a = f->v[k];
        const uint8_t b = f->v[(k + 1u == f->n) ? 0u : k + 1u];
        sa += (int)px[a] * py[b] - (int)px[b] * py[a];
    }
    return sa;
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    A = api;
    A->backlight_on();
    A->status_clear();

    uint16_t ax = 0, ay = 0, az = 0;   /* angles in half-units (512 = full turn) */
    uint8_t shape   = 0;
    uint8_t speed   = 6;               /* 1..16, in half-units (6 == old default 3) */
    bool    paused  = false;
    bool    wire    = false;           /* false = solid (hidden-line)       */
    bool    running = true;
    uint8_t prevKey = APP_KEY_INVALID;

    while (running) {
        const shape_t *s = &SHAPES[shape];
        const uint8_t ia = (uint8_t)(ax >> 1), ib = (uint8_t)(ay >> 1), ic = (uint8_t)(az >> 1);
        const int cx = SIN[(uint8_t)(ia + 64)], sxr = SIN[ia];
        const int cy = SIN[(uint8_t)(ib + 64)], syr = SIN[ib];
        const int cz = SIN[(uint8_t)(ic + 64)], szr = SIN[ic];

        for (uint8_t i = 0; i < s->nv; i++) {
            int x = s->v[i][0], y = s->v[i][1], z = s->v[i][2];
            int ny = (y * cx - z * sxr) >> 14;      /* Rx */
            int nz = (y * sxr + z * cx) >> 14;
            y = ny; z = nz;
            int nx = (x * cy + z * syr) >> 14;       /* Ry */
            nz     = (z * cy - x * syr) >> 14;
            x = nx; z = nz;
            nx = (x * cz - y * szr) >> 14;           /* Rz */
            ny = (x * szr + y * cz) >> 14;
            x = nx; y = ny;

            const int zc = z + DIST;                 /* always > 0 */
            px[i] = (int16_t)(CX + (x * FOCAL) / zc);
            py[i] = (int16_t)(CY + (y * FOCAL) / zc);
            pz[i] = (int16_t)z;
        }

        clear_screen();
        for (uint8_t i = 0; i < s->nf; i++) {
            const face_t *f = &s->f[i];
            if (!wire && face_area(f) >= 0)
                continue;                            /* hidden face culled */
            for (uint8_t k = 0; k < f->n; k++) {
                const uint8_t a = f->v[k];
                const uint8_t b = f->v[(k + 1u == f->n) ? 0u : k + 1u];
                const bool dotted = wire && (pz[a] + pz[b] > 0);   /* far half */
                draw_edge(px[a], py[a], px[b], py[b], dotted);
            }
        }

        /* Shape name: inverse label, top-left of the status bar (scan-list look). */
        const uint8_t end = (uint8_t)(2u + 4u * s->namelen);
        for (uint8_t i = 0; i <= end; i++)
            A->status_line[i] = 0;
        A->print_inverse(s->name, 2, 0, true, true, end);

        A->blit_status();
        A->blit_full();

        if (!paused) {
            ax += speed;                        /* half-units/frame (level 1..16) */
            ay += (uint16_t)(speed + 2u);       /* one full unit faster, as before */
            az += 2u;                           /* fixed 1 unit/frame tumble        */
        }

        const uint8_t key = A->get_key();
        if (key != prevKey && key != APP_KEY_INVALID) {
            A->backlight_on();
            switch (key) {
                case APP_KEY_EXIT:
                    running = false;
                    break;
                case APP_KEY_UP:
                case APP_KEY_DOWN: {
                    const int8_t dir = A->nav_dir(key);
                    if (dir > 0 && speed < 16u) speed++;
                    if (dir < 0 && speed > 1u) speed--;
                    break;
                }
                case APP_KEY_MENU:
                    paused = !paused;
                    break;
                case APP_KEY_STAR:
                    shape = (uint8_t)((shape + 1u) % NSHAPE);
                    break;
                case APP_KEY_F:
                    wire = !wire;
                    break;
                case APP_KEY_1: case APP_KEY_2: case APP_KEY_3: case APP_KEY_4:
                case APP_KEY_5: case APP_KEY_6: case APP_KEY_7: case APP_KEY_8:
                    if ((uint8_t)(key - APP_KEY_1) < NSHAPE)
                        shape = (uint8_t)(key - APP_KEY_1);
                    break;
                default:
                    break;
            }
        }
        prevKey = key;
        A->backlight_update();
    }
}
