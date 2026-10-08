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

int main(int argc, char **argv)
{
    int integrate = 1, argi = 1;
    if (argc > 1 && !strcmp(argv[1], "-p")) { integrate = 0; argi++; }
    if (argi >= argc) { fprintf(stderr, "usage: %s [-p] samples.u16\n", argv[0]); return 2; }
    FILE *f = fopen(argv[argi], "rb");
    if (!f) { perror(argv[argi]); return 2; }

    dec406_t d;
    dec406_init(&d, integrate);
    unsigned char b[2];
    int frames = 0;
    while (fread(b, 1, 2, f) == 2) {
        if (!dec406_push(&d, (uint16_t)(b[0] | (b[1] << 8)))) continue;
        dec406_info_t in;
        dec406_parse(&d, &in);
        frames++;
        printf("message   : %s (%d bits), %s frame%s\n", in.longMsg ? "long" : "short",
               in.longMsg ? 144 : 112, in.selftest ? "self-test" : "normal", d.inv ? ", inverted" : "");
        printf("country   : %u\n", in.country);
        printf("protocol  : %s\n", dec406_proto_name(&in));
        if (in.stdLoc) printf("id data   : 0x%06X\n", (unsigned)in.idData);
        if (in.hasPos) {
            printf("position  : "); pos("lat", in.latS); printf(", "); pos("lon", in.lonS);
            printf("%s\n", in.hasFine ? "" : " (coarse)");
        } else printf("position  : none\n");
        if (in.longMsg) printf("source    : %s, homing %s\n", (in.internalPos & 1u) ? "internal" : "external", in.homing ? "yes" : "no");
        printf("BCH       : %s / %s\n", in.bch1 ? "ok" : "FAIL", in.bch2 ? "ok" : "FAIL");
        printf("15-hex ID : %s%s\n\n", in.id, in.idRaw ? " (raw bits 26-85)" : "");
        dec406_rearm(&d);
    }
    fclose(f);
    printf("frames    : %d\n", frames);
    return frames ? 0 : 1;
}
