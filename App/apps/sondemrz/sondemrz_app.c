/* Copyright 2026 — Apache-2.0
 * Sonde MRZ: decode MRZ-N1 / MP3-H1 frames on PA4 at 19.2 kHz.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "../geonav_handoff.h"
#include "sondemrz_assets.h"
#include "sonde_info.h"
#include "decmrz.c"

GEONAV_ASSERT_SONDE_PREFIX(sonde_info_t);
_Static_assert(sizeof(sonde_info_t) == APP_HANDOFF_MAX,
               "every displayed field must travel through GeoNav and back");

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *D = d; const uint8_t *S = s;
    while (n--) *D++ = *S++;
    return d;
}

#define FS 19200u
#define CYC_PER_SAMPLE (48000000u / FS)
#define REG_2B 0x2B
#define REG_73 0x73
#define REG_13 0x13
#define REG_43 0x43
#define REG_43_MRZ 0x7E2Cu
#define REG_47 0x47
#define REG_47_MRZ 0x6942u
#define REG_38 0x38
#define REG_39 0x39
#define STEP_10HZ 500
#define OFF_MAX 10
#define KEY_EVERY 768u
#define REFRESH_POLLS 50u
#define LED_STEP (FS / 10u)
#define CFG_MAGIC 0xA0u
#define BEEP_X 47u
#define SPK_X 59u
#define BEEP_HZ 1536u
#define BEEP_MS 80u
#define ATT_SETTLE 40

typedef struct { bool spk; uint8_t att, beep; } app_cfg_t;

static struct {
    const app_api_t *A;
    uint32_t vfoFreq;
    int32_t offSteps;
    uint8_t prevKey, led;
    bool fArm;
    app_cfg_t cfg;
    uint16_t nOk, ledCnt;
    sonde_info_t info;
} g;

uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d){ return g.A->uidivmod(n, d); }
uint64_t __aeabi_uidiv(uint32_t n, uint32_t d) __attribute__((alias("__aeabi_uidivmod")));
uint64_t __aeabi_idivmod(int32_t n, int32_t d){ return g.A->idivmod(n, d); }
uint64_t __aeabi_idiv(int32_t n, int32_t d) __attribute__((alias("__aeabi_idivmod")));

static char *put(char *o, const char *s){ while (*s) *o++ = *s++; return o; }
static unsigned subu(uint32_t *v, uint32_t d){ unsigned q = 0; while (*v >= d) { *v -= d; q++; } return q; }
__attribute__((noinline))
static char *puti(char *o, int32_t v){ return g.A->append_i32(o, v); }
static char *putPos(char *o, int32_t s, char pos, char neg)
{
    uint32_t a = (uint32_t)(s < 0 ? -s : s) + 18u;
    o = puti(o, subu(&a, 360000u)); *o++ = '.';
    for (unsigned k = 0; k < 4u; k++) { a *= 10u; *o++ = (char)('0' + subu(&a, 360000u)); }
    *o++ = s < 0 ? neg : pos;
    return o;
}
static char *putFreq(char *o, uint32_t f)
{
    uint16_t mhz = 0; f += 50u;
    while (f >= 100000u) { f -= 100000u; mhz++; }
    o = puti(o, mhz); *o++ = '.';
    *o++ = (char)('0' + subu(&f, 10000u));
    *o++ = (char)('0' + subu(&f, 1000u));
    *o++ = (char)('0' + subu(&f, 100u));
    return o;
}
static char *put1(char *o, int32_t v10)
{
    if (v10 < 0) { *o++ = '-'; v10 = -v10; }
    uint64_t qr = __aeabi_idivmod(v10, 10);
    o = puti(o, (int32_t)qr); *o++ = '.';
    *o++ = (char)('0' + (int32_t)(qr >> 32));
    return o;
}
static char *putVs(char *o, int32_t v)
{
    if (v >= 0) *o++ = '+';
    if (v > -1000 && v < 1000) return put1(o, v);
    return puti(o, (v + (v < 0 ? -5 : 5)) / 10);
}
static void tiny(char *buf, uint8_t x, uint8_t y, char *end)
{ *end = '\0'; g.A->print_tiny(buf, x, y, false, true); }
static void emit(char *buf, char *end, unsigned line)
{ *end = '\0'; g.A->print_normal(buf, 0, 0, line); }

__attribute__((noinline))
static void rxAudio(void)
{
    g.A->bk_write(REG_47, REG_47_MRZ);
    g.A->audio_path(g.cfg.spk);
}

static void tune(void)
{
    uint32_t f = (uint32_t)((int32_t)g.vfoFreq + g.offSteps * STEP_10HZ);
    g.A->bk_write(REG_38, (uint16_t)f);
    g.A->bk_write(REG_39, (uint16_t)(f >> 16));
    g.A->tones_off_rx();
    rxAudio();
}

static void applyAtt(void)
{
    uint16_t reg = g.A->bk_read(REG_13), gain;
    g.A->asset_read((uint16_t)(ATT_REG13 + g.cfg.att * 2u), &gain, sizeof gain);
    g.A->bk_write(REG_13, (uint16_t)((reg & ~0x03FFu) | gain));
    g.A->set_agc(false);
}

static void attCycle(bool reverse)
{
    if (reverse)
        g.cfg.att = g.cfg.att ? (uint8_t)(g.cfg.att - 1u) : (uint8_t)(ATT_COUNT - 1u);
    else if (++g.cfg.att >= ATT_COUNT)
        g.cfg.att = 0;
    applyAtt();
    g.A->delay_ms(ATT_SETTLE);
}

__attribute__((noinline))
static void draw(const char *s)
{
    char str[34]; char *o;
    const sonde_info_t *in = &g.info;
    g.A->display_clear(); g.A->status_clear();
    g.A->draw_battery();
    g.A->print_inverse(s + T_TITLE, 2, 0, true, true, (uint8_t)(2u + T_TITLE_CHARS * 4u));
    if (g.cfg.beep) g.A->asset_read(BMP_SIGNAL, g.A->status_line + BEEP_X, ICON_W);
    if (g.fArm || g.cfg.spk)
        g.A->asset_read(g.fArm ? BMP_F : BMP_SPK, g.A->status_line + SPK_X, ICON_W);

    if (g.nOk) {
        if (in->flags & SONDE_HAS_POS) {
            o = putPos(str, in->latS, 'N', 'S'); *o++ = ' ';
            o = putPos(o, in->lonS, 'E', 'W');
        } else o = put(str, s + T_NOPOS);
        emit(str, o, 1);
        o = put(str, s + T_ALT); o = puti(o, in->alt_m); o = put(o, s + T_M);
        o = put(o, s + T_VS); o = putVs(o, in->vs_dm); emit(str, o, 2);
        o = put(str, s + T_HS); o = puti(o, in->hs_ms); o = put(o, s + T_HDG);
        o = puti(o, in->hdg | (in->flags & SONDE_HDG_256) * (256u / SONDE_HDG_256));
        emit(str, o, 3);
        o = str;
        if (in->flags & SONDE_HAS_TEMP) {
            o = put(o, s + T_TEMP); o = put1(o, in->temp_c10); o = put(o, s + T_C);
        }
        if (in->flags & SONDE_HAS_RH) { o = put(o, s + T_RH); o = puti(o, in->rh); *o++ = '%'; }
        if (o != str) emit(str, o, 4);
        g.A->print_bold(in->id, 0, 0, 0);
    }
    for (unsigned x = 0; x < 128u; x += 2u) g.A->fb[5][x] = 0x01u;

    o = putFreq(str, g.vfoFreq + (uint32_t)(g.offSteps * STEP_10HZ));
    if (g.offSteps) {
        *o++ = ' '; if (g.offSteps > 0) *o++ = '+';
        o = puti(o, g.offSteps * 5); o = put(o, s + T_KHZ);
    }
    tiny(str, 0, 43, o);
    o = puti(str, g.A->rssi_dbm()); o = put(o, s + T_DBM);
    tiny(str, (uint8_t)(128u - (unsigned)(o - str) * 4u), 43, o);
    o = put(str, s + T_FRAME); o = puti(o, g.nOk);
    tiny(str, 0, 49, o);
    g.A->asset_read((uint16_t)(ATT_LBL + g.cfg.att * ATT_REC), str, ATT_REC);
    g.A->print_tiny(str + 1, (uint8_t)str[0], 49, false, true);
}

static bool handleKey(uint8_t key)
{
    if (key == APP_KEY_F) { g.fArm = !g.fArm; return false; }
    bool reverse = g.fArm;
    g.fArm = false;
    if (key == APP_KEY_MENU)
        return (g.info.flags & SONDE_HAS_POS) != 0u &&
               g.A->handoff(GEONAV_APP_NAME, &g.info, APP_HANDOFF_MAX);
    switch (key) {
        case APP_KEY_1: g.cfg.spk = !g.cfg.spk; rxAudio(); break;
        case APP_KEY_2: {
            int32_t n = g.offSteps + (reverse ? -1 : 1);
            if ((uint32_t)(n + OFF_MAX) <= 2u * OFF_MAX) { g.offSteps = n; tune(); }
            break;
        }
        case APP_KEY_3: attCycle(reverse); break;
        case APP_KEY_4: g.offSteps = 0; tune(); break;
        case APP_KEY_5: g.cfg.beep ^= 1u; break;
        default: break;
    }
    return false;
}

__attribute__((noinline))
static void listen(const char *ui)
{
    decmrz_t mrz;
    decmrz_init(&mrz);
    g.A->asset_read(MRZ_ARC, mrz.arc, MRZ_ARC_LEN);
    uint32_t (*const clk)(void) = g.A->cycles_48mhz;
    uint16_t (*const sample)(void) = g.A->af_capture_sample;
    uint32_t next = clk();
    unsigned cnt = 0, refresh = 0;
    bool wantUi = true;
    for (;;) {
        while ((int32_t)(clk() - next) < 0) {}
        next += CYC_PER_SAMPLE;
        uint16_t x = sample();
        if (decmrz_push(&mrz, x)) {
            sonde_info_t info;
            if (decmrz_parse(&mrz, &info)) {
                g.info = info;
                g.nOk++;
                g.led = 3u; g.ledCnt = 0; g.A->led(true);
                wantUi = true;
            }
        }
        if (decmrz_busy(&mrz)) continue;
        if (g.led && ++g.ledCnt >= LED_STEP) {
            g.ledCnt = 0; g.led--; g.A->led(g.led & 1u);
        }
        if (!wantUi && ++cnt < KEY_EVERY) continue;
        cnt = 0;
        uint8_t key = g.A->get_key();
        bool keyUi = key != g.prevKey && key != APP_KEY_INVALID;
        g.prevKey = key;
        if (keyUi) {
            if (key == APP_KEY_EXIT) break;
            if (handleKey(key)) break;
        }
        if (wantUi || keyUi || ++refresh >= REFRESH_POLLS) {
            refresh = 0;
            g.A->battery_sample();
            draw(ui);
            g.A->blit_status();
            g.A->blit_full();
            g.A->backlight_on();
            if (wantUi && g.cfg.beep) {
                g.A->play_tone(BEEP_HZ, BEEP_MS);
                g.A->tones_off_rx();
                rxAudio();
            }
            wantUi = false;
            /* Display work spans sample periods; resume from now. */
            decmrz_rearm(&mrz);
            next = clk() + CYC_PER_SAMPLE;
            continue;
        }
        /* A key poll alone keeps the sample clock. Hold the last sample over
         * the short gap instead of discarding a partially matched header. */
        while ((int32_t)(clk() - next) >= 0) {
            decmrz_push(&mrz, x);
            next += CYC_PER_SAMPLE;
        }
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    uint8_t cfg;
    char ui[UI_SIZE] __attribute__((aligned(4)));
    g.A = api; g.prevKey = APP_KEY_INVALID; api->led(false);
    api->asset_read(0, ui, UI_SIZE);
    api->af_capture_open();
    api->backlight_on();
    g.vfoFreq = api->rx_freq();
    api->bk_write(REG_2B, (uint16_t)((api->bk_read(REG_2B) | 0x0700u) & ~0x0007u));
    api->bk_write(REG_73, (uint16_t)(api->bk_read(REG_73) | 0x0010u));
    uint16_t r13 = api->bk_read(REG_13);
    uint16_t r43 = api->bk_read(REG_43);
    api->bk_write(REG_43, REG_43_MRZ);
    api->cfg_load(&cfg, 1);
    if ((cfg & 0xE0u) == CFG_MAGIC) {
        g.cfg.spk = cfg & 1u;
        g.cfg.beep = (cfg >> 1) & 1u;
        g.cfg.att = (cfg >> 2) & 7u;
        if (g.cfg.att >= ATT_COUNT) g.cfg.att = 3u;
    } else g.cfg.att = 3u;
    applyAtt();
    rxAudio();
    api->delay_ms(50);
    if (api->handoff_read(&g.info, APP_HANDOFF_MAX, NULL) == (int16_t)APP_HANDOFF_MAX)
        g.nOk = 1u;
    listen(ui);
    if (g.offSteps) { g.offSteps = 0; tune(); }
    api->bk_write(REG_13, r13);
    api->set_agc(true);
    api->bk_write(REG_43, r43);
    api->af_capture_close();
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
    api->led(false);
    cfg = (uint8_t)(CFG_MAGIC | g.cfg.spk | (g.cfg.beep << 1) | (g.cfg.att << 2));
    api->cfg_save(&cfg, 1);
}
