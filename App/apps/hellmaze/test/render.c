/* Host check of the Hell Maze renderer: draws the view from a given position
 * and heading into a PBM file, with a stub API (framebuffer, dividers, assets), with the demons
 * at their start positions, the gun and the HUD (status line on top).
 *   clang -I.. -I../../.. '-D_Static_assert(...)=' '-Dsection(x)=used' -o render render.c
 * pt(), pi() and pn() mirror GUI_DisplaySmallest, GUI_DisplaySmallestInverse
 * and UI_PrintStringSmall / UI_PrintStringBuffer from App/ui/helper.c (Dual
 * Tachyon and contributors, Apache 2.0); the fonts are the firmware's own,
 * included from App/font.c.
 *   ./render <assets.bin> <x_q8> <y_q8> <ang> <out.pbm> [help] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define main hm_unused_main
#include "../hellmaze_app.c"
#include "../../../font.c"
#undef main

static uint8_t fb[7][128], sl[128], blob[4096];
static size_t blobLen;
static uint64_t ud(uint32_t n, uint32_t d) { return d ? n / d : 0; }
static char *au(char *o, uint32_t v)
{
    return o + sprintf(o, "%u", v);
}
/* GUI_DisplaySmallest, as the resident draws it (y in pixels). */
static void pt(const char *str, uint8_t x, uint8_t y, bool statusbar, bool fill)
{
    for (; *str; str++, x += 4)
        for (int i = 0; i < 3; i++)
            for (int j = 0, px = gFont3x5[*str - 0x20][i]; j < 6; j++, px >>= 1) {
                if (!(px & 1) || x + i >= 128)
                    continue;
                const int yy = y + j;
                uint8_t *b = statusbar ? &sl[x + i] : &fb[yy >> 3][x + i];
                const uint8_t bit = (uint8_t)(1u << (statusbar ? yy : yy & 7));
                *b = fill ? (uint8_t)(*b | bit) : (uint8_t)(*b & ~bit);
            }
}
/* GUI_DisplaySmallestInverse. */
static void pi(const char *str, uint8_t x, uint8_t line, bool statusbar, bool fill, uint8_t end)
{
    pt(str, x, (uint8_t)(line * 8 + 1), statusbar, fill);
    uint8_t *buf = statusbar ? sl : fb[line];
    const uint8_t start = (uint8_t)(x - 2);
    buf[start] ^= 0x3E;
    for (uint8_t i = start + 1; i < end; i++)
        buf[i] ^= 0x7F;
    buf[end] ^= 0x3E;
}
/* UI_PrintStringSmallNormal (packed 6x7 font). */
static void pn(const char *str, uint8_t start, uint8_t end, uint8_t line)
{
    const size_t n = strlen(str);
    if (end > start)
        start += (uint8_t)((((end - start) - n * (FONT_SMALL_WIDTH + 1u)) + 1) / 2);
    for (size_t i = 0; i < n; i++) {
        if (str[i] <= ' ' || str[i] >= 127)
            continue;
        unsigned bit = (unsigned)(str[i] - ' ' - 1) * FONT_SMALL_WIDTH * 7u;
        for (unsigned c = 0; c < FONT_SMALL_WIDTH; c++, bit += 7u) {
            const unsigned byte = bit >> 3;
            const unsigned packed = gFontSmallPacked[byte] | (gFontSmallPacked[byte + 1u] << 8);
            const unsigned x = start + i * (FONT_SMALL_WIDTH + 1u) + 1u + c;
            if (x < 128)
                fb[line][x] = (uint8_t)((packed >> (bit & 7u)) & 0x7Fu);
        }
    }
}
static uint64_t sd(int32_t n, int32_t d) { return (uint32_t)(d ? n / d : 0); }
static uint16_t ar(uint16_t off, void *buf, uint16_t len)
{
    if (off >= blobLen) return 0;
    if (off + len > blobLen) len = (uint16_t)(blobLen - off);
    memcpy(buf, blob + off, len);
    return len;
}

int main(int argc, char **argv)
{
    if (argc != 6 && argc != 7) return 2;
    FILE *f = fopen(argv[1], "rb");
    blobLen = fread(blob, 1, sizeof(blob), f);
    fclose(f);
    static app_api_t api;
    api.fb = fb;
    api.status_line = sl;
    api.uidivmod = ud;
    api.idivmod = sd;
    api.append_u32 = au;
    api.print_tiny = pt;
    api.print_inverse = pi;
    api.print_normal = pn;
    api.asset_read = ar;
    A = &api;
    A->asset_read(MAP, g.map, sizeof(g.map));
    reset();
    g.px = atoi(argv[2]);
    g.py = atoi(argv[3]);
    g.ang = (uint16_t)(atoi(argv[4]) << 4);
    g.dx = isin((uint16_t)((g.ang >> 4) + ANG_Q));
    g.dy = isin((uint16_t)(g.ang >> 4));
    g.now = 1000;
    render();
    drawFoes();
    drawGun(false);
    if (argc == 7)              /* any 6th argument: the help page */
        drawHelp();
    hud();
    FILE *o = fopen(argv[5], "w");
    fprintf(o, "P1\n128 64\n");
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 128; x++)
            fputc(((y < 8 ? sl[x] >> y : fb[(y - 8) >> 3][x] >> (y & 7)) & 1) ? '1' : '0', o);
        fputc('\n', o);
    }
    fclose(o);
    return 0;
}
