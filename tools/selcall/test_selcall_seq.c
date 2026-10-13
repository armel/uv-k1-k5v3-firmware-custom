/* Host test for the selcall tone sequence builder (App/app/selcall_seq.c).
 *   cc -I App -o /tmp/test_selcall tools/selcall/test_selcall_seq.c App/app/selcall_seq.c && /tmp/test_selcall
 */
#include <stdio.h>
#include <string.h>
#include "app/selcall.h"

static int fails;

static void check(const char *name, uint8_t type, uint32_t code, const uint16_t *want, uint8_t wantN)
{
    uint16_t got[SELCALL_DIGITS] = {0};
    const uint8_t n = SELCALL_BuildTones(type, code, got);
    int ok = n == wantN && (n == 0 || memcmp(got, want, n * sizeof(uint16_t)) == 0);
    printf("%s %-34s", ok ? "ok  " : "FAIL", name);
    for (uint8_t i = 0; i < n; i++) printf(" %u", got[i]);
    printf("\n");
    fails += !ok;
}

static void checkMs(const char *name, uint8_t type, uint16_t want)
{
    const uint16_t got = SELCALL_ToneMs(type);
    printf("%s %-34s %u ms\n", got == want ? "ok  " : "FAIL", name, got);
    fails += got != want;
}

int main(void)
{
    /* ZVEI */
    const uint16_t z12345[]   = { 1060, 1160, 1270, 1400, 1530 };
    const uint16_t z00000_1[] = { 2400, 2600, 2400, 2600, 2400 };     /* 0 R 0 R 0 */
    const uint16_t z00000_2[] = { 2400,  970, 2400,  970, 2400 };
    const uint16_t z11223_1[] = { 1060, 2600, 1160, 2600, 1270 };     /* 1 R 2 R 3 */
    const uint16_t z11223_2[] = { 1060,  970, 1160,  970, 1270 };
    const uint16_t z01234[]   = { 2400, 1060, 1160, 1270, 1400 };     /* leading zero kept */
    const uint16_t z98765[]   = { 2200, 2000, 1830, 1670, 1530 };
    const uint16_t z99999_1[] = { 2200, 2600, 2200, 2600, 2200 };
    /* CCIR */
    const uint16_t c12345[]   = { 1124, 1197, 1275, 1358, 1446 };
    const uint16_t c11223[]   = { 1124, 2110, 1197, 2110, 1275 };     /* 1 R 2 R 3 */
    const uint16_t c00000[]   = { 1981, 2110, 1981, 2110, 1981 };
    const uint16_t c06789[]   = { 1981, 1540, 1640, 1747, 1860 };

    check("ZVEI-1 12345",              SELCALL_ZVEI1, 12345, z12345, 5);
    check("ZVEI-2 12345 (no repeat)",  SELCALL_ZVEI2, 12345, z12345, 5);
    check("ZVEI-1 00000",              SELCALL_ZVEI1, 0,     z00000_1, 5);
    check("ZVEI-2 00000",              SELCALL_ZVEI2, 0,     z00000_2, 5);
    check("ZVEI-1 11223",              SELCALL_ZVEI1, 11223, z11223_1, 5);
    check("ZVEI-2 11223",              SELCALL_ZVEI2, 11223, z11223_2, 5);
    check("ZVEI-1 01234",              SELCALL_ZVEI1, 1234,  z01234, 5);
    check("ZVEI-1 98765",              SELCALL_ZVEI1, 98765, z98765, 5);
    check("ZVEI-1 99999",              SELCALL_ZVEI1, 99999, z99999_1, 5);
    check("CCIR-1 12345",              SELCALL_CCIR1, 12345, c12345, 5);
    check("CCIR-2 12345",              SELCALL_CCIR2, 12345, c12345, 5);
    check("CCIR-1 11223",              SELCALL_CCIR1, 11223, c11223, 5);
    check("CCIR-2 00000",              SELCALL_CCIR2, 0,     c00000, 5);
    check("CCIR-1 06789",              SELCALL_CCIR1, 6789,  c06789, 5);
    check("type OFF refused",          SELCALL_OFF,   12345, NULL, 0);
    check("type out of range refused", SELCALL_TYPE_COUNT, 12345, NULL, 0);
    check("code NONE refused",         SELCALL_ZVEI1, SELCALL_CODE_NONE, NULL, 0);
    check("code > 99999 refused",      SELCALL_CCIR1, 123456, NULL, 0);

    checkMs("ZVEI-1 tone",   SELCALL_ZVEI1, 70);
    checkMs("ZVEI-2 tone",   SELCALL_ZVEI2, 70);
    checkMs("CCIR-1 tone",   SELCALL_CCIR1, 100);
    checkMs("CCIR-2 tone",   SELCALL_CCIR2, 70);
    checkMs("OFF tone",      SELCALL_OFF,   0);

    printf("type names:");
    for (uint8_t t = 0; t < SELCALL_TYPE_COUNT; t++) printf(" %s", gSelCallTypeNames[t]);
    printf("\n%s: %d failure(s)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
