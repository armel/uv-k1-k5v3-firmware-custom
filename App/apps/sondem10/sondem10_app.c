/* Copyright 2026 — Apache-2.0
 * Sonde M10: decode standard Meteomodem M10 frames on PA4 at 19.2 kHz.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "sondem10_assets.h"
#include "sonde_info.h"
#include "decm10.c"

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *D = d; const uint8_t *S = s;
    while (n--) *D++ = *S++;
    return d;
}

/* PA4 audio capture and the cycle clock are resident (API level 3). */
#define FS 19200u
#define CYC_PER_SAMPLE (48000000u / FS)
#define REG_2B 0x2B
#define REG_73 0x73
#define REG_43 0x43
/* Preserve the proven M20 wide receive profile for the near-identical M10 signal. */
#define REG_43_M10 0x7E2Cu
#define REG_47 0x47
/* AF select 9 bypasses the speech filter, which is too narrow for 9615 chips/s. */
#define REG_47_M10 0x6942u
#define REG_38 0x38
#define REG_39 0x39
#define STEP_10HZ 500
#define OFF_MAX 10
/* Keys: a poll every 40 ms catches any press; a poll alone holds the last
 * sample over its ~0.2 ms (no redraw, no rearm). A held sample may flip a
 * header bit, so while frames come (once a second) no poll falls in
 * GUARD_LO .. + GUARD_W after the last one, around the next header. Redraw
 * on a frame or a key, and every 240 ms (RSSI) only without a frame for 2 s. */
#define KEY_EVERY 768u
#define REFRESH_POLLS 6u
#define TRACK_CYC (2000u * 48000u)
#define GUARD_LO (780u * 48000u)
#define GUARD_W (80u * 48000u)
#define LED_STEP (FS / 10u)
#define CFG_MAGIC 0x10u
#define BEEP_X 47u
#define SPK_X 59u
#define BEEP_HZ 1536u
#define BEEP_MS 80u
#define BK_REG_13 0x13
#define ATT_SETTLE 40

typedef struct { bool spk; uint8_t att, beep; } app_cfg_t;

/* Words, then bytes, then halfwords, the frame last: every byte field stays
 * within ldrb's 0..31 immediate offsets. offSteps is a word: Thumb-1 has no
 * ldrsb with an immediate offset. */
static struct {
    const app_api_t *A;
    uint32_t vfoFreq;
    int32_t offSteps;
    uint32_t tFrame;          /* cycles_48mhz at the last decoded frame */
    uint8_t prevKey, led;
    bool fArm, track;
    app_cfg_t cfg;
    int16_t rssi;
    uint16_t nOk, ledCnt;
    sonde_info_t info;
} g;

uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d){ return g.A->uidivmod(n, d); }
uint64_t __aeabi_uidiv(uint32_t n, uint32_t d) __attribute__((alias("__aeabi_uidivmod")));
uint64_t __aeabi_idivmod(int32_t n, int32_t d){ return g.A->idivmod(n, d); }
uint64_t __aeabi_idiv(int32_t n, int32_t d) __attribute__((alias("__aeabi_idivmod")));

static char *put(char *o, const char *s){ while (*s) *o++ = *s++; return o; }
static unsigned subu(uint32_t *v, uint32_t d){ unsigned q = 0; while (*v >= d) { *v -= d; q++; } return q; }
/* The resident formatter (API level 3). One local stub keeps every call a
 * plain bl: the service lies past the ldr immediate range of the table. */
__attribute__((noinline))
static char *puti(char *o, int32_t v){ return g.A->append_i32(o, v); }
/* "48.8000N" from centi-arcseconds (360000 per degree), rounded to 0.0001 deg. */
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
    o = puti(o, v10 / 10); *o++ = '.'; *o++ = (char)('0' + v10 % 10);
    return o;
}
/* Vertical speed: +5.2, -35.1; whole m/s from 100 m/s on (-105), so the ALT
 * row stays within its 18 columns at any altitude. */
static char *putVs(char *o, int32_t v)
{
    if (v >= 0) *o++ = '+';
    if (v > -1000 && v < 1000) return put1(o, v);
    return puti(o, (v + (v < 0 ? -5 : 5)) / 10);
}
static void tiny(char *buf, uint8_t x, uint8_t y, char *end)
{ *end = '\0'; g.A->print_tiny(buf, x, y, false, true); }

/* RX audio: the wide AF select and the speaker as configured. Shared by the
 * start-up, key 1, tune() and the decode beep (tones_off_rx mutes the AF). */
__attribute__((noinline))
static void rxAudio(void)
{
    g.A->bk_write(REG_47, REG_47_M10);
    g.A->audio_path(g.cfg.spk);
}
static void tune(void)
{
    uint32_t f = (uint32_t)((int32_t)g.vfoFreq + g.offSteps * STEP_10HZ);
    g.A->bk_write(REG_38, (uint16_t)(f & 0xFFFFu));
    g.A->bk_write(REG_39, (uint16_t)(f >> 16));
    g.A->tones_off_rx();         /* REG_30 to 0 then RX: VCO recalibration, relock */
    rxAudio();
}
static void decodeBeep(void)
{
    g.A->play_tone(BEEP_HZ, BEEP_MS);
    g.A->tones_off_rx();
    rxAudio();
}
static void applyAtt(void)
{
    uint16_t reg = g.A->bk_read(BK_REG_13), gain;
    g.A->asset_read((uint16_t)(ATT_REG13 + g.cfg.att * 2u), &gain, sizeof gain);
    g.A->bk_write(BK_REG_13, (uint16_t)((reg & ~0x03FFu) | gain));
    g.A->set_agc(false);
}
/* Whole %RH: the decoder's linear value plus m10mod's terms below 0 C
 * (+ -T / 5.5) and below -30 C (x (1 + (-30 - T) / 75)), clamped to 0..100;
 * the linear value alone without a temperature. */
static int32_t m10Rh(const sonde_info_t *in)
{
    int32_t rh = in->rh10, t = in->temp_c10;
    if (in->flags & SONDE_HAS_TEMP) {
        if (t < 0) rh += -t * 2 / 11;
        if (t < -300) rh += rh * (-300 - t) / 750;
    }
    if (rh < 0) rh = 0;
    if (rh > 1000) rh = 1000;
    return (rh + 5) / 10;
}
static void m10Temp(sonde_info_t *in)
{
    uint16_t tab[T_N], a = in->tRaw;
    uint8_t sc = (uint8_t)(a >> 12);
    a &= 0x0FFFu;
    if (!a || sc >= 3u) return;
    g.A->asset_read((uint16_t)(TEMP_TAB + sc * (T_N * 2u)), tab, sizeof tab);
    for (unsigned i = 0; i + 1u < T_N; i++) {
        if (a <= tab[i] && a > tab[i + 1u]) {
            in->temp_c10 = (int16_t)(i * 50 - (int)T_OFS10 +
                                     50 * (tab[i] - a) / (tab[i] - tab[i + 1u]));
            in->flags |= SONDE_HAS_TEMP;
            return;
        }
    }
}

static void emit(char *buf, char *end, unsigned line)
{
    *end = '\0';
    g.A->print_normal(buf, 0, 0, line);
}

static void attCycle(int8_t dir)
{
    if (dir < 0)
        g.cfg.att = g.cfg.att ? (uint8_t)(g.cfg.att - 1u) : (uint8_t)(ATT_COUNT - 1u);
    else if (++g.cfg.att >= ATT_COUNT)
        g.cfg.att = 0;
    applyAtt();
    g.A->delay_ms(ATT_SETTLE);
}

/* Out of line: inlined, it adds register pressure (spills) to the sample loop. */
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
        /* Fixed rows, the same in every sonde app: 1 the position (4 decimals,
         * 11 m, keep "89.9999S 179.9999W" within the 18 columns), 2 altitude and
         * vertical speed, 3 horizontal speed and heading, 4 the sonde's own
         * sensors. A missing field leaves its row blank. */
        if (in->flags & SONDE_HAS_POS) {
            o = putPos(str, in->latS, 'N', 'S'); *o++ = ' ';
            o = putPos(o, in->lonS, 'E', 'W');
        } else o = put(str, s + T_NOPOS);
        emit(str, o, 1);
        if (in->flags & SONDE_HAS_ALT) {
            o = put(str, s + T_ALT); o = puti(o, in->alt_m); o = put(o, s + T_M);
            if (in->flags & SONDE_HAS_VEL) {
                o = put(o, s + T_VS); o = putVs(o, in->vs_dm);
            }
            emit(str, o, 2);
        }
        if (in->flags & SONDE_HAS_VEL) {
            o = put(str, s + T_HS); o = puti(o, in->hs_ms);
            o = put(o, s + T_HDG); o = puti(o, in->hdg);
            emit(str, o, 3);
        }
        o = str;
        if (in->flags & SONDE_HAS_TEMP) {
            o = put(o, s + T_TEMP); o = put1(o, in->temp_c10); o = put(o, s + T_C);
        }
        if (in->flags & SONDE_HAS_RH) { o = put(o, s + T_RH); o = puti(o, m10Rh(in)); *o++ = '%'; }
        if (o != str) emit(str, o, 4);
        g.A->print_bold(in->id, 0, 0, 0);
    }
    for (unsigned x = 0; x < 128u; x += 2u) g.A->fb[5][x] = 0x01u;   /* row 5 still blank */

    o = putFreq(str, g.vfoFreq + (uint32_t)(g.offSteps * STEP_10HZ));
    if (g.offSteps) {
        *o++ = ' '; if (g.offSteps > 0) *o++ = '+';
        o = puti(o, g.offSteps * 5); o = put(o, s + T_KHZ);
    }
    tiny(str, 0, 43, o);
    o = puti(str, g.rssi); o = put(o, s + T_DBM);
    tiny(str, (uint8_t)(128u - (unsigned)(o - str) * 4u), 43, o);
    o = put(str, s + T_FRAME); o = puti(o, g.nOk);
    tiny(str, 0, 49, o);
    g.A->asset_read((uint16_t)(ATT_LBL + g.cfg.att * ATT_REC), str, ATT_REC);
    for (o = str; *o; o++) {}
    tiny(str, (uint8_t)(128u - (unsigned)(o - str) * 4u), 49, o);
}

/* One new key press (EXIT is handled by listen). */
static void handleKey(uint8_t key)
{
    if (key == APP_KEY_F) { g.fArm = !g.fArm; return; }
    const bool reverse = g.fArm;
    g.fArm = false;
    switch (key) {
        case APP_KEY_1: g.cfg.spk = !g.cfg.spk; rxAudio(); break;
        case APP_KEY_2: {
            int32_t n = g.offSteps + (reverse ? -1 : 1);
            if (n >= -OFF_MAX && n <= OFF_MAX) { g.offSteps = n; tune(); }
            break; }
        case APP_KEY_3: attCycle(reverse ? -1 : 1); break;
        case APP_KEY_4: g.offSteps = 0; tune(); break;
        case APP_KEY_5: g.cfg.beep ^= 1u; break;
        default: break;
    }
}

__attribute__((noinline))
static void listen(const char *ui)
{
    decm10_t m10;
    decm10_init(&m10);
    /* The two per-sample services, held in registers: their table slots lie
     * past the ldr immediate range. */
    uint32_t (*const clk)(void) = g.A->cycles_48mhz;
    uint16_t (*const sample)(void) = g.A->af_capture_sample;
    uint32_t next = clk();
    unsigned cnt = 0, refresh = 0;
    bool wantUi = false;
    for (;;) {                   /* left on EXIT */
        while ((int32_t)(clk() - next) < 0) {}
        next += CYC_PER_SAMPLE;
        uint16_t x = sample();
        if (decm10_push(&m10, x)) {
            sonde_info_t info;
            info.flags = 0;
            if (decm10_parse(&m10, &info)) {
                m10Temp(&info);
                g.info = info;
                g.nOk++;
                g.led = 3u; g.ledCnt = 0; g.A->led(true);
                g.tFrame = clk(); g.track = true;
                wantUi = true;
            }
            decm10_rearm(&m10);
        }
        /* No poll in ST_DATA: get_key alone exceeds one sample period. */
        if (decm10_busy(&m10)) continue;
        if (g.led && ++g.ledCnt >= LED_STEP) {
            g.ledCnt = 0; g.led--; g.A->led(g.led & 1u);
        }
        if (!wantUi) {
            if (++cnt < KEY_EVERY) continue;
            if (g.track && (uint32_t)(clk() - g.tFrame - GUARD_LO) < GUARD_W) continue;
        }
        cnt = 0;
        uint8_t key = g.A->get_key();
        bool keyUi = key != g.prevKey && key != APP_KEY_INVALID;
        g.prevKey = key;
        if (keyUi) {
            if (key == APP_KEY_EXIT) break;
            handleKey(key);
        }
        if ((uint32_t)(clk() - g.tFrame) > TRACK_CYC) g.track = false;
        if (wantUi || keyUi || (!g.track && ++refresh >= REFRESH_POLLS)) {
            refresh = 0;
            g.rssi = g.A->rssi_dbm();
            g.A->battery_sample();       /* yields the capture's ADC itself */
            draw(ui);
            g.A->blit_status();
            g.A->blit_full();
            g.A->backlight_on();
            /* The beep blocks and relocks the RX: here, between frames, after
             * the redraw, and the slot end below resyncs the sample clock. */
            if (wantUi && g.cfg.beep) decodeBeep();
            wantUi = false;
            /* Key scans and UI work span sample periods; resume from now. */
            decm10_rearm(&m10);
            next = clk() + CYC_PER_SAMPLE;  /* skip the samples the slot took */
            continue;
        }
        /* A poll alone: the sample clock kept, the last sample held over it. */
        while ((int32_t)(clk() - next) >= 0) { decm10_push(&m10, x); next += CYC_PER_SAMPLE; }
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    uint8_t cfg[4];
    char ui[UI_SIZE] __attribute__((aligned(4)));
    g.A = api; g.prevKey = APP_KEY_INVALID; api->led(false);
    api->asset_read(0, ui, UI_SIZE);

    api->af_capture_open();
    api->backlight_on();
    g.vfoFreq = api->rx_freq();
    api->bk_write(REG_2B, (uint16_t)((api->bk_read(REG_2B) | 0x0700u) & ~0x0007u));
    api->bk_write(REG_73, (uint16_t)(api->bk_read(REG_73) | 0x0010u));
    uint16_t r13 = api->bk_read(BK_REG_13);
    uint16_t r43 = api->bk_read(REG_43);
    api->bk_write(REG_43, REG_43_M10);
    api->cfg_load(cfg, 4);
    if (cfg[0] == CFG_MAGIC) {
        g.cfg.spk = cfg[1] == 1u;
        g.cfg.beep = cfg[2] == 1u;
        if (cfg[3] < ATT_COUNT) g.cfg.att = cfg[3];
    } else g.cfg.att = 3u;
    applyAtt();
    rxAudio();
    api->delay_ms(50);
    listen(ui);
    if (g.offSteps) { g.offSteps = 0; tune(); }
    api->bk_write(BK_REG_13, r13);
    api->set_agc(true);
    api->bk_write(REG_43, r43);
    api->af_capture_close();
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
    api->led(false);
    cfg[0] = CFG_MAGIC; cfg[1] = g.cfg.spk; cfg[2] = g.cfg.beep; cfg[3] = g.cfg.att;
    api->cfg_save(cfg, 4);
}
