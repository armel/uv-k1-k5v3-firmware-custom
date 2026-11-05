/* Host test for the ToneCall tone sequence builder (../tonecall_seq.c).
 *   cc -o /tmp/test_tonecall test/test_tonecall_seq.c tonecall_seq.c && /tmp/test_tonecall
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../tonecall_seq.h"

static int fails;

static void check(const char *name, uint8_t type, const char *code, const uint16_t *want, uint8_t wantN)
{
    uint16_t got[TC_MAX_DIGITS] = {0};
    const uint8_t n = TC_BuildTones(type, code, got);
    int ok = n == wantN && (n == 0 || memcmp(got, want, n * sizeof(uint16_t)) == 0);
    printf("%s %-34s", ok ? "ok  " : "FAIL", name);
    for (uint8_t i = 0; i < n; i++) printf(" %u", got[i]);
    printf("\n");
    fails += !ok;
}

static void checkMs(const char *name, uint8_t type, uint16_t want)
{
    const uint16_t got = TC_ToneMs(type);
    printf("%s %-34s %u ms\n", got == want ? "ok  " : "FAIL", name, got);
    fails += got != want;
}

static void expect(const char *name, int ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    fails += !ok;
}

/* Pack then unpack: same type, same digits (leading zeros kept). */
static int roundTrip(uint8_t type, const char *code)
{
    char back[TC_MAX_DIGITS + 1u];
    uint8_t t;
    const uint32_t v = TC_PackPreset(type, code);
    const uint8_t n = TC_UnpackPreset(v, &t, back);
    return v != 0 && v < (1u << 29) && n == strlen(code) && t == type && strcmp(back, code) == 0;
}

static void storageTests(void)
{
    char code[TC_MAX_DIGITS + 1u], back[TC_MAX_DIGITS + 1u];
    uint8_t t;

    /* every code of 1 to 5 digits, every type */
    int bad = 0;
    long count = 0;
    for (uint8_t type = 0; type < TC_TYPE_COUNT; type++)
        for (uint8_t n = 1; n <= 5; n++) {
            long lim = 1;
            for (uint8_t k = 0; k < n; k++) lim *= 10;
            for (long c = 0; c < lim; c++, count++) {
                snprintf(code, sizeof(code), "%0*ld", n, c);
                bad += !roundTrip(type, code);
            }
        }
    printf("     round trip, all codes 1-5 digits x 4 types: %ld codes\n", count);
    expect("round trip 1-5 digits, exhaustive", bad == 0);

    /* 1M random codes of 6 to 8 digits */
    bad = 0;
    srand(4242);
    for (long i = 0; i < 1000000; i++) {
        const uint8_t n = (uint8_t)(6 + rand() % 3), type = (uint8_t)(rand() % TC_TYPE_COUNT);
        for (uint8_t k = 0; k < n; k++) code[k] = (char)('0' + rand() % 10);
        code[n] = '\0';
        bad += !roundTrip(type, code);
    }
    expect("round trip 6-8 digits, 1M random", bad == 0);

    expect("bounds: 0 (ZVEI-1)",        roundTrip(TC_ZVEI1, "0") && TC_PackPreset(TC_ZVEI1, "0") == 1u);
    expect("bounds: 99999999 (CCIR-2)", roundTrip(TC_CCIR2, "99999999") &&
                                        TC_PackPreset(TC_CCIR2, "99999999") == 444444440u);
    expect("bounds: 00000000 kept",     roundTrip(TC_ZVEI2, "00000000"));
    expect("max value fits 29 bits",    TC_PackPreset(TC_CCIR2, "99999999") < (1u << 29));
    expect("empty code packs to 0",     TC_PackPreset(TC_ZVEI1, "") == 0);
    expect("9 digits pack to 0",        TC_PackPreset(TC_ZVEI1, "123456789") == 0);
    expect("non-digit packs to 0",      TC_PackPreset(TC_ZVEI1, "12a") == 0);
    expect("bad type packs to 0",       TC_PackPreset(TC_TYPE_COUNT, "12") == 0);
    expect("0 unpacks empty",           TC_UnpackPreset(0, &t, back) == 0 && back[0] == '\0' && t == 0);
    expect("444444441 unpacks empty",   TC_UnpackPreset(444444441u, &t, back) == 0 && back[0] == '\0');
    expect("21414 ZVEI-1 round trip",   roundTrip(TC_ZVEI1, "21414"));
    expect("021414 differs from 21414", TC_PackPreset(TC_ZVEI1, "021414") != TC_PackPreset(TC_ZVEI1, "21414"));

    /* config block */
    uint32_t words[TC_PRESET_COUNT], in[TC_PRESET_COUNT], out[TC_PRESET_COUNT];
    uint8_t last;
    expect("config words are 16 bytes", sizeof(words) == TC_CFG_LEN);
    in[0] = TC_PackPreset(TC_ZVEI1, "21414");
    in[1] = TC_PackPreset(TC_ZVEI2, "21301");
    in[2] = 0;
    in[3] = TC_PackPreset(TC_CCIR2, "99999999");
    for (uint8_t l = 0; l < TC_PRESET_COUNT; l++) {
        TC_CfgPack(words, in, l);
        const int ok = TC_CfgUnpack(words, out, &last) && last == l && !memcmp(in, out, sizeof(in));
        printf("%s config round trip, last = %u\n", ok ? "ok  " : "FAIL", l);
        fails += !ok;
    }
    memset(words, 0xFF, sizeof(words));             /* erased flash */
    expect("erased flash: defaults", !TC_CfgUnpack(words, out, &last) && last == 0 &&
                                     !out[0] && !out[1] && !out[2] && !out[3]);
    const uint8_t old[TC_CFG_LEN] = { 0x7C, 2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    memcpy(words, old, sizeof(words));               /* ToneCall 0.2 config */
    expect("ToneCall 0.2 config: defaults", !TC_CfgUnpack(words, out, &last) && last == 0 && !out[0]);
    TC_CfgPack(words, in, 1);
    words[2] |= 0x1FFFFFFFu;                         /* preset value out of range */
    expect("invalid preset reads empty", TC_CfgUnpack(words, out, &last) && out[2] == 0 && out[1] == in[1]);
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
    const uint16_t z21414_2[] = { 1160, 1060, 1400, 1060, 1400 };     /* Canal E alarm */
    const uint16_t z111_1[]   = { 1060, 2600, 1060 };                 /* 1 R 1, short code */
    const uint16_t z8x_1[]    = { 2000, 2600, 2000, 2600, 2000, 2600, 2000, 2600 };
    /* CCIR */
    const uint16_t c12345[]   = { 1124, 1197, 1275, 1358, 1446 };
    const uint16_t c11223[]   = { 1124, 2110, 1197, 2110, 1275 };     /* 1 R 2 R 3 */
    const uint16_t c00000[]   = { 1981, 2110, 1981, 2110, 1981 };
    const uint16_t c06789[]   = { 1981, 1540, 1640, 1747, 1860 };

    check("ZVEI-1 12345",              TC_ZVEI1, "12345", z12345, 5);
    check("ZVEI-2 12345 (no repeat)",  TC_ZVEI2, "12345", z12345, 5);
    check("ZVEI-1 00000",              TC_ZVEI1, "00000", z00000_1, 5);
    check("ZVEI-2 00000",              TC_ZVEI2, "00000", z00000_2, 5);
    check("ZVEI-1 11223",              TC_ZVEI1, "11223", z11223_1, 5);
    check("ZVEI-2 11223",              TC_ZVEI2, "11223", z11223_2, 5);
    check("ZVEI-1 01234",              TC_ZVEI1, "01234", z01234, 5);
    check("ZVEI-1 98765",              TC_ZVEI1, "98765", z98765, 5);
    check("ZVEI-1 99999",              TC_ZVEI1, "99999", z99999_1, 5);
    check("ZVEI-2 21414",              TC_ZVEI2, "21414", z21414_2, 5);
    check("ZVEI-1 111 (3 digits)",     TC_ZVEI1, "111", z111_1, 3);
    check("ZVEI-1 88888888 (max)",     TC_ZVEI1, "88888888", z8x_1, 8);
    check("CCIR-1 12345",              TC_CCIR1, "12345", c12345, 5);
    check("CCIR-2 12345",              TC_CCIR2, "12345", c12345, 5);
    check("CCIR-1 11223",              TC_CCIR1, "11223", c11223, 5);
    check("CCIR-2 00000",              TC_CCIR2, "00000", c00000, 5);
    check("CCIR-1 06789",              TC_CCIR1, "06789", c06789, 5);
    check("type out of range refused", TC_TYPE_COUNT, "12345", NULL, 0);
    check("empty code refused",        TC_ZVEI1, "", NULL, 0);
    check("non-digit refused",         TC_ZVEI1, "12a45", NULL, 0);
    check("9 digits refused",          TC_CCIR1, "123456789", NULL, 0);

    checkMs("ZVEI-1 tone",   TC_ZVEI1, 70);
    checkMs("ZVEI-2 tone",   TC_ZVEI2, 70);
    checkMs("CCIR-1 tone",   TC_CCIR1, 100);
    checkMs("CCIR-2 tone",   TC_CCIR2, 70);
    checkMs("invalid tone",  TC_TYPE_COUNT, 0);

    storageTests();

    printf("type names:");
    for (uint8_t t = 0; t < TC_TYPE_COUNT; t++) printf(" %s", tcTypeNames[t]);
    printf("\n%s: %d failure(s)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
