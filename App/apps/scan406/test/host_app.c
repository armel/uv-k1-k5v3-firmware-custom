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

/* Host test of the whole Scan 406 app on a simulated radio (run_tests.sh).
 *
 * Simulated time (microseconds) advances in delay_ms, play_tone, blit_full
 * (about the LCD transfer) and cycles_48mhz (2 us a call, so the capture's
 * busy wait moves on); ticks_ms has the resident 10 ms resolution. RSSI is a
 * -120 dBm floor with +-1 dB of noise, plus the strongest emitter active
 * within the receive window of the frequency written to REG_38/39. The PA4
 * samples are that emitter's audio (gen406.py output, from its carrier start)
 * when it has some, else discriminator noise. Each
 * scenario runs app_main until it returns: a hand-off, or EXIT pressed
 * from a given time. An alarm is acknowledged ackMs after its first beep
 * (key 1); the flashlight register is a variable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile uint32_t torch_odr;
#define GPIOC_ODR torch_odr

/* app_main's section is ELF-only: keep the "used", drop the section. */
#define section(x) used
#include "../scan406_app.c"
#undef section

typedef struct {
    uint32_t f; int dbm; uint32_t start, period, dur, count;   /* ms */
    const uint16_t *audio; uint32_t n;                           /* PA4 samples at 9.6 kHz */
} emitter_t;

static struct {
    emitter_t em[4];
    unsigned nEm;
    uint32_t win;              /* receive window, +- 10 Hz units */
    uint64_t us;
    uint32_t now, endMs, vfo, tuned, staged, stagedAt;   /* now: ms, from us */
    uint8_t  cfgIn, cfgOut, bw, mod, rxMod;
    bool uhfPath, prepared;
    bool installed, led, ledEver, handed, alarm, torchFlashed;
    unsigned beeps, acks;
    uint32_t ackMs, alarmAt, torch0;
    uint32_t seed;
    char lastText[2][40];      /* rows 43 and 49 of the last frame */
    char helpLine[40];         /* the first help row */
    struct { uint32_t at; uint8_t key; } press[4];   /* scripted keys, 60 ms each */
    unsigned nPress, tunes;
} S;

static uint8_t fb[7][128], status[128], assets[1024];
static size_t nAssets;

static void advance(uint64_t us){
    S.us += us;
    S.now = (uint32_t)(S.us / 1000u);
    if (S.now > 3000000u) { printf("FAIL simulated time limit (app never returned)\n"); exit(1); }
}
static uint32_t rnd(void){ S.seed = S.seed * 1103515245u + 12345u; return S.seed >> 16; }

/* The strongest emitter heard now, its time into the burst (us). */
static const emitter_t *heard(uint64_t *into){
    const emitter_t *best = NULL;
    for (unsigned k = 0; k < S.nEm; k++) {
        const emitter_t *e = &S.em[k];
        const uint32_t df = S.tuned > e->f ? S.tuned - e->f : e->f - S.tuned;
        if (df > S.win || S.now < e->start) continue;
        const uint64_t t = S.us - (uint64_t)e->start * 1000u, per = (uint64_t)e->period * 1000u;
        if (t / per >= e->count || t % per >= (uint64_t)e->dur * 1000u) continue;
        if (!best || e->dbm > best->dbm) { best = e; *into = t % per; }
    }
    return best;
}
static int16_t s_rssi(void){
    uint64_t t;
    const emitter_t *e = heard(&t);
    const int noise = -120 + (int)(rnd() % 3u) - 1;
    return (int16_t)(e && e->dbm > noise ? e->dbm : noise);
}
static uint16_t s_sample(void){
    uint64_t t;
    const emitter_t *e = heard(&t);
    if (e && e->audio) {
        const uint64_t i = t * 9600u / 1000000u;
        if (i < e->n) return e->audio[i];
    }
    return (uint16_t)(2048u - 400u + rnd() % 801u);   /* no carrier: noise */
}
static uint32_t s_cycles(void){ advance(2); return (uint32_t)(S.us * 48u); }
static void s_bk_write(uint8_t reg, uint16_t v){
    if (reg == 0x38) { S.tuned = (S.tuned & 0xFFFF0000u) | v; S.tunes++; }
    if (reg == 0x39) S.tuned = (S.tuned & 0xFFFFu) | ((uint32_t)v << 16);
}
static void s_delay(uint32_t ms){ advance((uint64_t)ms * 1000u); }
static uint32_t s_ticks(void){ return S.now - S.now % 10u; }
static uint8_t s_get_key(void){
    for (unsigned k = 0; k < S.nPress; k++)
        if (S.now >= S.press[k].at && S.now < S.press[k].at + 60u) return S.press[k].key;
    if (S.alarm) {
        S.torchFlashed |= (torch_odr ^ S.torch0) != 0;
        if (S.now >= S.alarmAt + S.ackMs) { S.alarm = false; S.acks++; return APP_KEY_1; }
        return APP_KEY_INVALID;
    }
    /* EXIT pressed and released every 100 ms from endMs */
    return S.now >= S.endMs && S.now / 100u % 2u ? APP_KEY_EXIT : APP_KEY_INVALID;
}
static void s_play_tone(uint16_t hz, uint16_t ms){
    (void)hz;
    if (!S.alarm) { S.alarm = true; S.alarmAt = S.now; }
    advance((uint64_t)ms * 1000u); S.beeps++;
}
static void s_led(bool on){ S.led = on; S.ledEver |= on; }
static void s_nop(void){}
static void s_nop_b(bool b){ (void)b; }
static void s_blit_full(void){ advance(12000); }
static uint16_t s_bk_read(uint8_t reg){ (void)reg; return 0; }
static void s_set_af(uint8_t m){ (void)m; }
static uint32_t s_rx_freq(void){ return S.vfo; }
static void s_cfg_load(uint8_t *b, uint8_t n){ (void)n; *b = S.cfgIn; }
static void s_cfg_save(const uint8_t *b, uint8_t n){ (void)n; S.cfgOut = *b; }
static void s_display_clear(void){ memset(fb, 0, sizeof fb); }
static void s_status_clear(void){ memset(status, 0, sizeof status); }
static void s_print_tiny(const char *s, uint8_t x, uint8_t y, bool sb, bool fill){
    (void)fill;
    if (!sb && x == 0 && y == 0) snprintf(S.helpLine, sizeof S.helpLine, "%s", s);
    if (!sb && x == 0 && (y == 43 || y == 49))
        snprintf(S.lastText[y == 49], sizeof S.lastText[0], "%s", s);
}
static void s_print_inverse(const char *s, uint8_t x, uint8_t l, bool sb, bool f, uint8_t e){
    (void)s; (void)x; (void)l; (void)sb; (void)f; (void)e;
}
static uint16_t s_asset_read(uint16_t off, void *buf, uint16_t len){
    if (off >= nAssets) return 0;                  /* clamped, as the resident one */
    if (len > nAssets - off) len = (uint16_t)(nAssets - off);
    memcpy(buf, assets + off, len);
    return len;
}
static uint64_t s_uidivmod(uint32_t n, uint32_t d){
    return d ? ((uint64_t)(n % d) << 32) | (n / d) : (uint64_t)n << 32;
}
static char *s_append_u32(char *o, uint32_t u){
    return o + sprintf(o, "%u", u);
}
static bool s_handoff(const char *name, const void *m, uint8_t len){
    (void)m; (void)len;
    S.handed = S.installed && !strcmp(name, "EPIRB 406");
    return S.handed;
}
static void s_beam_get(app_beam_channel_t *ch){
    memset(ch, 0x5A, sizeof *ch);
    ch->rx_frequency = S.vfo;
}
static void s_beam_prepare(void){
    S.prepared = S.uhfPath = true;
    S.rxMod = 0;
    S.tuned = 43450000u;
}
static bool s_vfo_stage(const app_beam_channel_t *ch){
    S.staged = ch->rx_frequency;
    S.stagedAt = S.now;
    S.bw = ch->channel_bandwidth;
    S.mod = ch->modulation;
    return true;
}

static app_api_t api = {
    .abi_major = APP_ABI_MAJOR, .api_level = APP_API_LEVEL, .api_size = sizeof(app_api_t),
    .fb = fb, .display_clear = s_display_clear, .status_clear = s_status_clear,
    .print_tiny = s_print_tiny, .print_inverse = s_print_inverse,
    .blit_full = s_blit_full, .blit_status = s_nop,
    .get_key = s_get_key, .delay_ms = s_delay, .play_tone = s_play_tone, .led = s_led,
    .rssi_dbm = s_rssi, .bk_write = s_bk_write, .audio_path = s_nop_b,
    .tones_off_rx = s_nop, .rx_freq = s_rx_freq,
    .cfg_load = s_cfg_load, .cfg_save = s_cfg_save,
    .draw_battery = s_nop, .battery_sample = s_nop, .backlight_on = s_nop,
    .backlight_update = s_nop, .status_line = status,
    .beam_prepare = s_beam_prepare, .beam_get = s_beam_get,
    .ticks_ms = s_ticks, .asset_read = s_asset_read,
    .uidivmod = s_uidivmod, .append_u32 = s_append_u32,
    .handoff = s_handoff, .vfo_stage = s_vfo_stage,
    .bk_read = s_bk_read, .set_af = s_set_af, .cycles_48mhz = s_cycles,
    .af_capture_open = s_nop, .af_capture_sample = s_sample, .af_capture_close = s_nop,
};

static unsigned fails;
#define CHECK(name, cond) do { if (!(cond)) { fails++; \
    printf("FAIL %-34s %s (staged %u, n %u/%u, t %u ms, beeps %u)\n", name, #cond, \
           S.staged, g.n[0], g.n[1], S.now, S.beeps); } } while (0)

static void setup(uint32_t vfo, uint8_t cfg, uint32_t endMs){
    memset(&S, 0, sizeof S);
    memset(&g, 0, sizeof g);   /* the loader zeroes the overlay */
    S.vfo = vfo; S.cfgIn = cfg; S.endMs = endMs; S.win = 150; S.installed = true;
    S.uhfPath = vfo >= 28000000u;
    S.seed = 1; S.ackMs = 2000;
    torch_odr = S.torch0 = 0x2000u * (cfg == 1u);   /* flashlight on before WATCH runs */
}
static void emit(uint32_t f, int dbm, uint32_t start, uint32_t period, uint32_t dur, uint32_t count){
    S.em[S.nEm++] = (emitter_t){ f, dbm, start, period, dur, count, NULL, 0 };
}
/* A burst with its audio (gen406.py, from the carrier start): its length is the file's */
typedef struct { uint16_t *s; uint32_t n; } clip_t;
static void emitAudio(uint32_t f, int dbm, uint32_t start, uint32_t period, uint32_t count, clip_t c){
    S.em[S.nEm++] = (emitter_t){ f, dbm, start, period, (uint32_t)(c.n * 1000u / 9600u), count, c.s, c.n };
}
static clip_t load(const char *dir, const char *name){
    char path[512];
    snprintf(path, sizeof path, "%s/%s.u16", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f) / 2;
    rewind(f);
    clip_t c = { malloc((size_t)n * 2u), (uint32_t)n };
    if (fread(c.s, 2, (size_t)n, f) != (size_t)n) { perror(path); exit(2); }
    fclose(f);
    return c;
}
#define F(khz) (40600000u + (khz) * 100u)
#define VFO406 F(31)
#define NEVER  0xFFFFFFFFu

static void run(void){ app_main(&api); }

int main(int argc, char **argv){
    if (argc != 3) { fprintf(stderr, "usage: host_app <assets.bin> <audio dir>\n"); return 2; }
    FILE *fa = fopen(argv[1], "rb");
    if (!fa || !(nAssets = fread(assets, 1, sizeof assets, fa)) || !feof(fa)) { perror(argv[1]); return 2; }
    fclose(fa);

    /* 1G short burst on the new channel S: hand-off on the first burst */
    setup(VFO406, 0xFF, NEVER);
    emit(F(76), -90, 5000, 50000, 440, 10);
    run();
    CHECK("1G 406.076 auto: hand-off", S.handed && S.staged == F(76));
    CHECK("1G 406.076 auto: FM wide", S.bw == 0 && S.mod == 0);
    CHECK("1G 406.076 auto: RX profile", S.prepared && S.uhfPath && S.rxMod == 0);
    CHECK("1G 406.076 auto: after the ack", S.stagedAt >= 5440 + 2000 && S.stagedAt < 5700 + 2300);
    CHECK("1G 406.076 auto: alarm, one ack", S.acks == 1 && S.beeps >= 2 && S.ledEver && !S.led);
    CHECK("1G 406.076 auto: flashlight flashed, restored", S.torchFlashed && torch_odr == S.torch0);
    CHECK("1G 406.076 auto: cfg AUTO saved", S.cfgOut == 0);

    /* every scanned channel, long message, beacon 600 Hz off its centre */
    static const uint8_t chans[] = { 25, 28, 31, 37, 40, 49, 52, 61, 64, 73, 76 };
    for (unsigned k = 0; k < sizeof chans; k++) {
        setup(VFO406, 0, NEVER);
        emit(F(chans[k]) + 60u, -100, 3000 + 77u * k, 50000, 520, 10);
        run();
        char n[40];
        snprintf(n, sizeof n, "1G long on 406.0%u", chans[k]);
        CHECK(n, S.handed && S.staged == F(chans[k]));
    }

    /* a continuous carrier on K (a sonde) must not trigger; a beacon on G does */
    setup(VFO406, 0, NEVER);
    emit(F(52), -70, 2000, NEVER, NEVER, 1);
    emit(F(40), -95, 9000, 50000, 440, 10);
    run();
    CHECK("carrier on K, 1G on G: G handed", S.handed && S.staged == F(40));
    CHECK("carrier on K, 1G on G: no 2G", g.n[1] == 0);

    /* a carrier already there at launch */
    setup(VFO406, 0, NEVER);
    emit(F(61), -60, 0, NEVER, NEVER, 1);
    emit(F(28), -100, 4000, 50000, 520, 10);
    run();
    CHECK("carrier at launch, 1G on C", S.handed && S.staged == F(28));

    /* 2G (1 s): counted, never handed off */
    setup(VFO406, 0, 65000);
    emit(F(49), -90, 3000, 30000, 1000, 2);
    run();
    CHECK("2G: counted twice", g.n[1] == 2 && g.n[0] == 0);
    CHECK("2G: alarm, no hand-off", S.acks == 2 && !S.handed && !S.staged && !S.led);
    CHECK("2G: shown", !strncmp(S.lastText[0], "GEN2 406.049", 12));

    /* WATCH: three bursts, no hand-off, LED on */
    setup(VFO406, 1, 160000);
    emit(F(25), -100, 3000, 50000, 440, 3);
    run();
    CHECK("watch: three 1G", g.n[0] == 3 && !S.handed && !S.staged);
    CHECK("watch: three alarms, LED off", S.acks == 3 && S.ledEver && !S.led);
    CHECK("watch: flashlight left on", S.torchFlashed && torch_odr == 0x2000u);
    CHECK("watch: cfg WATCH saved", S.cfgOut == 1);
    CHECK("watch: counts shown", !strcmp(S.lastText[1], "BURSTS GEN1 3 GEN2 0"));

    /* the flashlight comes back as it was, whatever the blink count at the ack */
    for (uint32_t ack = 300; ack <= 1100; ack += 260) {
        for (uint32_t on = 0; on <= 0x2000u; on += 0x2000u) {
            setup(VFO406, 1, 20000);
            S.ackMs = ack;
            torch_odr = S.torch0 = on | 0x11u;    /* other PC pins untouched */
            emit(F(25), -100, 3000, 50000, 440, 1);
            run();
            char n[40];
            snprintf(n, sizeof n, "flashlight %s, ack %u ms", on ? "on" : "off", ack);
            CHECK(n, S.acks == 1 && S.torchFlashed && torch_odr == S.torch0);
        }
    }

    /* EPIRB 406 not installed: as WATCH */
    setup(VFO406, 0, 20000);
    S.installed = false;
    emit(F(37), -100, 3000, 50000, 440, 1);
    run();
    CHECK("no EPIRB 406: alarm, no stage", g.n[0] == 1 && S.acks == 1 && !S.staged);

    /* AUTO needs no operator: it hands off after 30 s, before the next burst */
    setup(VFO406, 0, NEVER);
    S.ackMs = 60000;
    emit(F(31), -90, 3000, 50000, 440, 10);
    run();
    CHECK("AUTO timeout: hand-off at 30 s", !S.acks && S.handed && S.stagedAt >= 33000 && S.stagedAt < 35000);
    CHECK("AUTO timeout: a beep a second", S.beeps >= 29 && S.beeps <= 31);

    /* WATCH still waits for an acknowledgement, even after a minute */
    setup(VFO406, 1, 70000);
    S.ackMs = 60000;
    emit(F(31), -90, 3000, 50000, 440, 1);
    run();
    CHECK("WATCH: alarm waits 60 s", S.acks == 1 && !S.handed && S.beeps >= 59);

    /* MENU: the help page until a key, no scanning meanwhile, then the scan again */
    setup(VFO406, 0, NEVER);
    S.press[0].at = 2000; S.press[0].key = APP_KEY_MENU;
    S.press[1].at = 9000; S.press[1].key = APP_KEY_EXIT;   /* leaves the help, does not quit */
    S.nPress = 2;
    emit(F(64), -90, 20000, 50000, 440, 10);
    run();
    CHECK("help: shown", !strcmp(S.helpLine, "*     AUTO (EPIRB 406) / WATCH"));
    CHECK("help: EXIT there only leaves the help", S.handed && S.staged == F(64));
    setup(VFO406, 0, NEVER);
    S.press[0].at = 2000; S.press[0].key = APP_KEY_MENU;
    S.press[1].at = 30000; S.press[1].key = APP_KEY_1;
    S.nPress = 2;
    emit(F(64), -90, 40000, 50000, 440, 10);
    run();
    CHECK("help: back to the scan", S.handed && S.staged == F(64));
    /* help from 2 s to 20 s, EXIT at 20.5 s: about 0.6 + 0.6 s of scanning,
     * some 800 channel steps; scanning all along would be about 6400 */
    setup(VFO406, 0, NEVER);
    S.press[0].at = 2000;  S.press[0].key = APP_KEY_MENU;
    S.press[1].at = 20000; S.press[1].key = APP_KEY_1;
    S.press[2].at = 20500; S.press[2].key = APP_KEY_EXIT;
    S.nPress = 3;
    run();
    CHECK("help: no scanning while shown", S.tunes > 400u && S.tunes < 1500u);

    /* short blips (50 ms, 200 ms) are ignored */
    setup(VFO406, 0, 20000);
    emit(F(61), -80, 3000, 2000, 50, 5);
    emit(F(64), -80, 3500, 2000, 200, 5);
    run();
    CHECK("blips: nothing", !g.n[0] && !g.n[1] && !S.handed && !S.beeps);
    CHECK("blips: none shown", !strcmp(S.lastText[0], "NO BEACON YET"));

    /* threshold: 10 dB above the floor triggers, 8 dB does not */
    setup(VFO406, 0, NEVER);
    emit(F(31), -110, 3000, 50000, 440, 10);
    run();
    CHECK("10 dB above the floor", S.handed && S.staged == F(31));
    setup(VFO406, 0, 120000);
    emit(F(31), -112, 3000, 50000, 440, 10);
    run();
    CHECK("8 dB above the floor: ignored", !S.handed && !g.n[0]);

    /* TEST mode (cfg bit 1): 406.0xx MHz becomes 433.619 + xx kHz, whatever the VFO */
    setup(VFO406, 2, NEVER);
    emit(43365000u, -90, 3000, 50000, 520, 10);
    emit(F(31), -90, 3000, 50000, 520, 10);       /* ignored: not scanned */
    run();
    CHECK("TEST: 433.650 = D", S.handed && S.staged == 43365000u);
    CHECK("TEST: cfg kept", S.cfgOut == 2);
    setup(14550000u, 2, NEVER);
    emit(43369500u, -90, 3000, 50000, 520, 10);
    run();
    CHECK("TEST: 433.695 = S", S.handed && S.staged == 43369500u);
    setup(VFO406, 3, 60000);                      /* TEST + WATCH */
    emit(43364400u, -90, 3000, 50000, 440, 1);
    run();
    CHECK("TEST + WATCH: 433.644 = B, no hand-off", g.n[0] == 1 && !S.handed && S.cfgOut == 3);
    CHECK("TEST + WATCH: shown as 433.644", !strncmp(S.lastText[0], "GEN1 ? 433.644", 14));
    /* 406 mode never scans 433 */
    setup(43365000u, 0, 60000);
    emit(43365000u, -90, 3000, 50000, 520, 1);
    run();
    CHECK("406 mode, VFO on 433: nothing", !g.n[0] && !S.handed);

    /* A wide receive window may still make a 3 kHz neighbour answer first. */
    setup(VFO406, 0, NEVER);
    S.win = 400;
    emit(F(40), -90, 3000, 50000, 440, 10);
    run();
    CHECK("wide window: within 3 kHz", S.handed && (S.staged == F(40) || S.staged == F(37)));

    /* frame sync and BCH, on real burst audio (gen406.py, 160 ms of carrier first) */
    static const struct { const char *clip, *shown; uint8_t kind, st; } V[] = {
        { "long",   "GEN1 OK 406.076",     K_OK,  0 },   /* 144 bits, BCH-1 and BCH-2 */
        { "short",  "GEN1 OK 406.076",     K_OK,  0 },   /* 112 bits, BCH-1 */
        { "self",   "GEN1 OK TST 406.076", K_OK,  1 },   /* self-test frame sync */
        { "flip",   "GEN1 ERR 406.076",    K_ERR, 0 },   /* bit 50 flipped: BCH-1 */
        { "flip2",  "GEN1 ERR 406.076",    K_ERR, 0 },   /* bit 120 flipped: BCH-2 */
        { "inv",    "GEN1 OK 406.076",     K_OK,  0 },   /* inverted discriminator */
        { "foff",   "GEN1 OK 406.076",     K_OK,  0 },   /* carrier 3 kHz off */
        { "weak",   "GEN1 OK 406.076",     K_OK,  0 },   /* CNR 15 dB in 25 kHz */
        { "cut",    "GEN1 ERR 406.076",    K_ERR, 0 },   /* carrier lost at 300 ms: noise, BCH */
        { "nosync", "GEN1 ? 406.076",      K_1G,  0 },   /* 0.5 s of carrier, no frame */
    };
    for (unsigned k = 0; k < sizeof V / sizeof V[0]; k++) {
        const clip_t c = load(argv[2], V[k].clip);
        for (uint32_t start = 3000; start < 3040; start += 7) {   /* detection 0-35 ms in */
            setup(VFO406, 1, start + 3000);
            emitAudio(F(76), -95, start, 50000, 1, c);
            run();
            char n[48];
            snprintf(n, sizeof n, "%s, burst at %u ms", V[k].clip, start);
            CHECK(n, g.type == V[k].kind && g.st == V[k].st && g.n[0] == 1);
            CHECK(n, !strncmp(S.lastText[0], V[k].shown, strlen(V[k].shown)));
        }
        free(c.s);
    }

    /* Scan 406 must call beam_prepare before scanning from any VFO profile. */
    {
        static const struct { uint32_t vfo; uint8_t mod; const char *name; } P[] = {
            { 14550000u, 0, "VHF FM wide" },
            { VFO406,    1, "UHF AM wide" },
            { VFO406,    2, "UHF USB narrow" },
        };
        const clip_t c = load(argv[2], "long");
        for (unsigned k = 0; k < sizeof P / sizeof P[0]; k++) {
            setup(P[k].vfo, 1, 10000);
            S.rxMod = P[k].mod;
            emitAudio(F(76), -95, 3000, 50000, 1, c);
            run();
            CHECK(P[k].name, S.prepared && S.uhfPath && S.rxMod == 0 && g.type == K_OK);
        }
        free(c.s);
    }
    /* the longest last-burst row, after a long wait: 32 columns at most */
    {
        const clip_t c = load(argv[2], "flip");
        setup(VFO406, 1, 1300000);
        emitAudio(F(76), -95, 3000, 2000000, 1, c);
        run();
        CHECK("row width: age capped at 999 s", !strcmp(S.lastText[0], "GEN1 ERR 406.076 999S AGO"));
        CHECK("row width: fits 32 columns", strlen(S.lastText[0]) <= 32u);
        free(c.s);
    }

    /* AUTO hands a verified frame to EPIRB 406 */
    {
        const clip_t c = load(argv[2], "long");
        setup(VFO406, 0, NEVER);
        emitAudio(F(49), -95, 3000, 50000, 10, c);
        run();
        CHECK("verified 1G: hand-off", g.type == K_OK && S.handed && S.staged == F(49));
        free(c.s);
    }

    printf(fails ? "%u FAILED\n" : "all scenarios pass\n", fails);
    return fails != 0;
}
