/* Host harness: run dec406 over a file of little-endian uint16 ADC samples at
 * 9.6 kHz and print every decoded message.
 *
 *   host_dec406 [-p] samples.u16      (-p: input is phase-like, no integration)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dec406.h"

static void pos(const char *name, long s)
{
    long a = labs(s);
    printf("%s%ld.%05ld", s < 0 ? "-" : "", a / 3600, (a % 3600) * 100000 / 3600);
    (void)name;
}

static void show(dec406_t *d)
{
    dec406_info_t in;
    dec406_parse(d, &in);
    printf("message   : %s (%d bits), %s frame%s\n", in.longMsg ? "long" : "short",
           in.longMsg ? 144 : 112, in.selftest ? "self-test" : "normal", d->inv ? ", inverted" : "");
    printf("country   : %u\n", in.country);
    printf("protocol  : %s\n", dec406_proto_name(&in));
    if (in.stdLoc) printf("id data   : 0x%06X\n", (unsigned)in.idData);
    if (in.hasPos) {
        printf("position  : "); pos("lat", in.latS); printf(", "); pos("lon", in.lonS);
        printf("%s\n", in.hasFine ? "" : " (coarse)");
    } else printf("position  : none\n");
    if (in.longMsg) printf("source    : %s, homing %s\n", (in.internalPos & 1u) ? "internal" : "external", in.homing ? "yes" : "no");
    if (in.idRaw == 2) printf("cancel    : yes\n");
    printf("BCH       : %s / %s\n", in.bch1 ? "ok" : "FAIL", in.bch2 ? "ok" : "FAIL");
    printf("15-hex ID : %s%s\n\n", in.id, in.idRaw == 1 ? " (raw bits 26-85)" : "");
}

/* -x HEX: parse a frame given as hex (bits 1-112 or 1-144), no demodulation */
static int fromHex(const char *hex)
{
    dec406_t d;
    memset(&d, 0, sizeof d);
    size_t n = strlen(hex) * 4;
    if (n != 112 && n != 144) { fprintf(stderr, "frame must be 28 or 36 hex digits\n"); return 2; }
    unsigned sync = 0;
    for (unsigned i = 1; i <= n; i++) {
        char c = hex[(i - 1) / 4];
        unsigned v = (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        unsigned b = (v >> (3 - (i - 1) % 4)) & 1u;
        if (i >= 16 && i <= 24) sync = (sync << 1) | b;
        if (i >= 25) d.bits[(i - 25) >> 3] |= (uint8_t)(b << (7 - ((i - 25) & 7)));
    }
    d.selftest = sync == 0xD0u;      /* 011010000 */
    show(&d);
    return 0;
}

int main(int argc, char **argv)
{
    int integrate = 1, argi = 1;
    if (argc > 2 && !strcmp(argv[1], "-x")) return fromHex(argv[2]);
    if (argc > 1 && !strcmp(argv[1], "-p")) { integrate = 0; argi++; }
    if (argi >= argc) { fprintf(stderr, "usage: %s [-p] samples.u16 | -x HEX\n", argv[0]); return 2; }
    FILE *f = fopen(argv[argi], "rb");
    if (!f) { perror(argv[argi]); return 2; }

    dec406_t d;
    dec406_init(&d, integrate);
    unsigned char b[2];
    int frames = 0;
    while (fread(b, 1, 2, f) == 2) {
        if (!dec406_push(&d, (uint16_t)(b[0] | (b[1] << 8)))) continue;
        frames++;
        show(&d);
        dec406_rearm(&d);
    }
    fclose(f);
    printf("frames    : %d\n", frames);
    return frames ? 0 : 1;
}
