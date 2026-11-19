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

/*
 * Scan 406 (RX only) - watches the Cospas-Sarsat 406 MHz channels for a beacon
 * burst, then hands the channel over to EPIRB 406, which decodes the next
 * burst (about 50 s later).
 *
 * Channels (C/S T.012, Annex H, Table H.2): the centres a beacon can transmit
 * on, B 406.025, C .028, D .031, F .037, G .040, S .076 (open since 2025), and
 * the channels kept for future assignments, J .049, K .052, N .061, O .064,
 * R .073. Not scanned: A .022 (system beacons) and the reserved ones.
 *
 * At launch, beam_prepare selects a temporary UHF FM receive profile; the
 * loader restores the user's VFO after the app exits. Per channel: write the
 * frequency (REG_38/39), restart RX (tones_off_rx: REG_30 cycle, AF muted),
 * wait STEP_MS, read RSSI. Each channel keeps its own noise floor. A reading
 * TRIG_DB above it starts a localisation pass: a beacon close by leaks into
 * every channel (bench, 03/11/2026: up to 11 dB above the floor 48 kHz away,
 * while its own channel reads more than 20 dB above the leak), so the trigger
 * can come from the wrong one. The pass reads all channels once more (about
 * 35 ms) and parks the receiver on the one furthest above its floor.
 *
 * Verification: the RX audio is captured on PA4 at 9.6 kHz (RAW receive, as
 * in EPIRB 406) and fed to EPIRB 406's decoder (dec406.c, included). A burst
 * is detected within about 35 ms of its start and localised within about 75
 * ms, inside its 160 ms of carrier, so the bit sync and the frame sync that
 * follow are captured:
 *   GEN1 OK  frame sync, then BCH-1 (bits 25-106) and, on a long message,
 *            BCH-2 (bits 107-144) correct: a genuine first-generation frame;
 *   GEN1 ERR frame sync, a BCH check fails (weak signal, or a badly coded
 *            beacon: EPIRB 406 still shows what it can);
 *   GEN1 CUT frame sync, the frame outlasts the capture (CAP_CYC);
 *   TST      added when the frame sync is the self-test one (011010000).
 * A carrier lost in the middle of the frame leaves the decoder on noise: BCH
 * fails, GEN1 ERR. While searching for the frame sync, RSSI is read every
 * POLL_CYC (between two samples, as EPIRB 406 reads the keys): without a frame
 * sync once the carrier is gone, or 400 ms into the capture, the burst is
 * timed until RSSI falls back under floor + REARM_DB:
 *   MIN_1G_MS .. MIN_2G_MS   GEN1 ?: a first-generation length, not verified;
 *   MIN_2G_MS .. LONG_MS     GEN2: second generation (1 s, not decodable here);
 *   LONG_MS and more         a continuous carrier (interferer): the
 *                            channel's floor takes its level, so it does not
 *                            trigger again (the others it lifted took theirs
 *                            in the localisation pass); no alarm;
 *   shorter than MIN_1G_MS   ignored.
 * Every other case raises the alarm. Then, for any 1G, AUTO puts the VFO on
 * the channel (vfo_stage, FM wide) and opens EPIRB 406 (handoff); WATCH, or
 * EPIRB 406 not installed, goes on scanning. A carrier still there after the
 * alarm is under the trigger: the localisation pass gave the channel's floor
 * the burst's level.
 *
 * Alarm: a beep every second, the flashlight (PC13, driven directly: no API
 * service) and the green LED blinking, the screen inverted, every ALARM_MS.
 * Any key acknowledges it (that key does nothing else). For a first-generation
 * burst in AUTO, it also ends after AUTO_ALARM_MS so EPIRB 406 is listening
 * before the next burst, about 50 s after the one detected. WATCH and GEN2
 * still wait for a key. The flashlight is left as it was found.
 *
 * TEST mode (key 0, saved): the same channels on 433 MHz, 406.0xx MHz
 * becoming 433.619 + xx kHz: B 433.644, D 433.650 (the usual generator
 * frequency), G 433.659 ... S 433.695. Bench tests never transmit on 406 MHz.
 * A hand-off in TEST mode tunes EPIRB 406 to the 433 MHz channel.
 *
 * Screen: one bar per channel, its highest level above the floor since the
 * last frame (1 px per dB, the dotted line is the trigger), the channel's kHz
 * under it, the last burst (kind, channel, age up to 999 s) and the GEN1 /
 * GEN2 burst counts.
 * Keys: * AUTO / WATCH · 0 406 MHz / TEST 433 MHz · 1 121.5 / 121.375 siren watch
 * (all saved; see the MODE_ELT notes below) · MENU help
 * (no scanning meanwhile) · EXIT quit.
 * The loader re-runs RADIO_SetupRegisters on exit: frequency and audio are
 * restored.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "scan406_assets.h"    /* generated by gen_assets.py */
#define DEC406_LEAN            /* no idData / stdLoc: not used */
#include "../epirb406/dec406.c"

#define N_CH        11u
#define F406        40600000u  /* 406.000 MHz, 10 Hz units                */
#define F_TEST      43361900u  /* TEST: 406.000 becomes 433.619 MHz      */
#define MODE_WATCH  1u         /* g.mode bits, saved                      */
#define MODE_TEST   2u
#define MODE_ELT    4u         /* key 1: the 121.5 MHz watch              */
#define REG_38      0x38       /* RX frequency, 10 Hz units, low 16 bits  */
#define REG_39      0x39       /* RX frequency, high 16 bits              */

#define STEP_MS     3u         /* synthesizer relock and RSSI per channel */
#define TRIG_DB     10
#define REARM_DB    5
#define MIN_1G_MS   300u
#define MIN_2G_MS   750u
#define LONG_MS     1300u
#define DRAW_SWEEPS 6u         /* about 250 ms between frames             */
#define BEEP_HZ     1536u      /* 3 << 9: a cheap Thumb constant          */
#define BEEP_MS     150u
#define ALARM_MS    250u       /* flashlight, LED and screen toggle; a beep every 4 */
#define AUTO_ALARM_MS 30000u   /* leave AUTO time to catch the next ~50 s burst      */

/* PA4 capture, from the localisation (the cycle clock and the capture are API
 * level 3). It starts 40 to 75 ms after the carrier. A T.001 frame sync ends
 * 220 ms after the carrier start and a long frame 520 ms after it; a 70 cm
 * test transmitter (BK4829 FSK modem) measured on 15/10/2026 sent its frame
 * sync 365 to 385 ms and ended its frame 665 to 685 ms after it. The windows
 * take both. */
#define CYC_PER_SAMPLE (48000000u / DEC406_FS)      /* 5000 */
#define SYNC_CYC    (48000000u / 1000u * 400u)      /* no frame sync by then: not a 1G frame */
#define POLL_CYC    (48000000u / 1000u * 10u)       /* RSSI while searching: the burst ended? */
#define CAP_CYC     (48000000u / 1000u * 750u)      /* the end of the frame */
#define REG_2B      0x2B
#define REG_73      0x73

/* 121.5 MHz watch (MODE_ELT), measured on the bench of 03/11/2026 with a
 * LimeSDR siren (AM on 144.100 MHz through the VHF LNA, and on 433.695 MHz):
 * once a second, the VHF LNA, the AM demodulator and 121.500 MHz (REG_33,
 * REG_31 bit 0, REG_42, REG_2A: RADIO_SetModulation's BK4829 values and
 * BK4819_PickRXFilterPathBasedOnFrequency's); the RSSI settles 16 to 18.5 ms
 * after the jump, 7 ms after the jump back. A reading ELT_TRIG_DB above the
 * point's own floor starts ELT_NW x 20 ms of PA4 audio: band-passed (300 Hz
 * high-pass, 1700 Hz Butterworth low-pass), zero crossings counted per 20 ms,
 * the series correlated with a falling sawtooth of 240 to 500 ms (a homing
 * siren sweeps 1600 to 300 Hz, 2 to 4 times a second). r^2 >= 0.40 over a
 * sweep of 300 Hz or more, twice in a row: ELT alarm (bench: the siren found
 * 3 to 5 dB above the RSSI floor, noise 7 to 12 %). Anything else there gives
 * the point's floor its level, as a continuous carrier on a 406 channel. The
 * RF filter stays the beam_prepare one (FM narrow, REG_43 0x4048): an ELT
 * may be 6 kHz off, which the stock AM filter (0x345C) loses. Two points:
 * 121.500 MHz (distress) and 121.375 MHz (SAR exercise beacons), 125 kHz
 * apart, each with its own floor; the second jump stays on the VHF LNA and
 * gets ELT_STEP_MS. TEST: one point, the siren on channel S (433.695 MHz) in
 * FM. */
#define F_ELT       12150000u
#define F_EX        12137500u  /* 121.375 MHz, exercises */
#define ELT_STEP_MS 10u        /* 121.500 to 121.375: a short jump, same LNA */
#define ELT_PEAK_DB 6          /* offset search: peak above the edges */
#define REG_7E      0x7E       /* AGC: <15> fixed gain, <14:12> table index */
#define ELT_EVERY_MS 1000u
#define ELT_VHF_MS  20u
#define ELT_TRIG_DB 5
#define ELT_NW      60u        /* 1.2 s of audio */
#define ELT_WS      192u       /* samples per 20 ms window */
#define SIREN_R2    102u       /* r^2 0.40, Q8 */
#define SIREN_SPAN  12u        /* crossings per 20 ms: 300 Hz */
#define REG_GPIO    0x33       /* LNA switches: VHF 0x04, UHF 0x08 */
#define REG_31      0x31
#define REG_42      0x42
#define REG_2A      0x2A

/* g.type: what the last burst was (T_KIND entries, in this order) */
enum { K_1G = 1, K_CUT, K_ERR, K_OK, K_2G, K_ELT };

/* The flashlight (PC13, an output from boot), its output data register. The
 * host test points it at a variable. */
#ifndef GPIOC_ODR
#define GPIOC_ODR   (*(volatile uint32_t *)0x50000814u)
#endif
#define TORCH       (1u << 13)

#define COL_X       3u         /* first bar column, 11 px per channel     */
#define COL_W       11u
#define BAR_W       8u
#define BASE_Y      31u        /* bar baseline, 1 px per dB above it      */
#define LABEL_Y     33u

/* kHz above 406.000 MHz, in scan order */
static const uint8_t CH[N_CH] = { 25, 28, 31, 37, 40, 49, 52, 61, 64, 73, 76 };

/* The app state in one struct: a function loads one base address instead of
 * one literal-pool word per global. The loader zeroes the overlay (.bss) at
 * every launch. */
static struct {
    const app_api_t *A;
    uint32_t base;             /* F406, or F_TEST in TEST mode            */
    uint32_t tLast;            /* ticks_ms of the last burst              */
    uint32_t tElt;             /* ticks_ms of the last 121.5 MHz check    */
    uint32_t eltF;             /* the last siren's frequency, 10 Hz units */
    uint8_t  eltLow;           /* this visit on the low fixed gain (1) or the AGC */
    uint16_t r7e;              /* REG_7E (AGC) kept over a VHF visit      */
    uint16_t n[2];             /* 1G and 2G bursts                        */
    uint8_t  mode;             /* MODE_WATCH | MODE_TEST (saved)          */
    uint8_t  type, last, st;   /* last burst: K_*, channel, self-test     */
    uint8_t  prevKey;
    bool     running;
    uint8_t  peak[N_CH];       /* highest dB above the floor this frame,  */
                               /* at most BASE_Y                          */
    int16_t  floorQ[N_CH + 4]; /* noise floor per channel, dBm x16; 0 at  */
                               /* launch, it falls to the first reading;  */
                               /* then the ELT points' (AGC / low gain)   */
} g;

static char *put(char *o, const char *s){ while (*s) *o++ = *s++; return o; }
static void tiny(const char *s, unsigned x, unsigned y){ g.A->print_tiny(s, (uint8_t)x, (uint8_t)y, false, true); }

static uint32_t freq(unsigned i){ return g.base + CH[i] * 100u; }

static void tuneF(uint32_t f, unsigned ms){
    const app_api_t *A = g.A;
    A->bk_write(REG_38, (uint16_t)f);
    A->bk_write(REG_39, (uint16_t)(f >> 16));
    A->tones_off_rx();         /* REG_30 to 0 then RX: VCO recalibration, relock */
    A->delay_ms(ms);
}
static void tune(unsigned i){ tuneF(freq(i), STEP_MS); }

/* AUTO: the VFO on f, then the app named. A 406 burst: channel i, FM wide,
 * EPIRB 406. An ELT siren: 121.500 MHz AM narrow (TEST: channel S, FM wide),
 * Sig Finder, given the launch VFO as EPIRB 406 key 8 gives it, so its EXIT
 * brings that VFO back. False when the firmware cannot stage the VFO or the
 * app is not installed. */
__attribute__((noinline)) static bool handOff(uint32_t f, bool elt){
    const app_api_t *A = g.A;
    /* vfo_stage (API level 3, appended late): absent from older firmware */
    if (A->api_size < offsetof(app_api_t, vfo_stage) + sizeof A->vfo_stage || !A->vfo_stage)
        return false;
    app_beam_channel_t ch;
    A->beam_get(&ch);          /* the current VFO: other fields kept */
    if (!(elt ? A->handoff("Sig Finder", &ch, offsetof(app_beam_channel_t, name))
              : A->handoff("EPIRB 406", NULL, 0))) return false;
    const bool am = elt && !(g.mode & MODE_TEST);
    ch.rx_frequency = f;
    ch.tx_offset_frequency = 0;
    ch.tx_offset_direction = 0;
    ch.modulation = am;        /* FM, or AM on 121.5 */
    ch.channel_bandwidth = am; /* wide, or narrow on 121.5 */
    ch.rx_codetype = ch.tx_codetype = 0;
    A->vfo_stage(&ch);
    g.running = false;
    return true;
}

static void alarm(bool autoExit);
static void draw(void);

/* Decode the burst from the trigger: K_OK, K_ERR or K_CUT, 0 without a frame
 * sync (lim: the end of the burst, dBm). Not inlined into burst(): smaller
 * that way. */
__attribute__((noinline)) static unsigned verify(int16_t lim){
    const app_api_t *A = g.A;
    dec406_t d;
    dec406_init(&d, true);     /* INT: integrate the discriminator pulses */
    /* The two per-sample services, held in registers: their table slots lie
     * past the ldr immediate range. */
    uint32_t (*const clk)(void) = A->cycles_48mhz;
    uint16_t (*const sample)(void) = A->af_capture_sample;
    A->set_af(APP_AF_FM);      /* PA4 follows the AF DAC; tones_off_rx mutes it again */
    const uint32_t t0 = clk();
    uint32_t poll = POLL_CYC;
    for (uint32_t next = 0; next < CAP_CYC; next += CYC_PER_SAMPLE) {
        while (clk() - t0 < next) {}
        if (dec406_push(&d, sample())) break;
        if (d.state == DEC406_SEARCH && next >= poll) {
            poll += POLL_CYC;
            if (next >= SYNC_CYC || A->rssi_dbm() < lim) return 0;
        }
    }
    g.st = d.selftest;
    if (d.state != DEC406_DONE) return d.state == DEC406_DATA ? K_CUT : 0;
    const uint8_t *b = d.bits;
    return syndrome(b, 25, 106, BCH1_GEN, 21) ||
           (bit(b, 25) && syndrome(b, 107, 144, BCH2_GEN, 12)) ? K_ERR : K_OK;
}

/* A reading above the trigger on channel i: the channel furthest above its
 * floor (a strong burst leaks into all of them), then verify the burst, time
 * it. Above the floor, not the highest RSSI: an interferer on another channel
 * has its floor at its own level.
 * The localisation pass, about 35 ms after the trigger, confirms it: nothing
 * left above the trigger, a noise spike, back to the sweep (it held the radio
 * up to 440 ms before 1.9). Every channel still above the trigger takes its
 * reading as the floor: an interferer lifting several channels then sets off
 * one hold, not one per channel (406 Link bench of 07/11/2026). A beacon's
 * floors fall back within about 0.5 s (sweep()), its next burst is ~50 s
 * away. */
__attribute__((noinline)) static void burst(unsigned i){
    const app_api_t *A = g.A;
    const uint32_t t0 = A->ticks_ms();
    int32_t top = INT32_MIN, fl = 0;
    for (unsigned j = 0; j < N_CH; j++) {
        tune(j);
        const int32_t r = A->rssi_dbm() * 16, d = r - g.floorQ[j];
        if (d > top) { top = d; i = j; fl = g.floorQ[j]; }
        if (d >= TRIG_DB * 16) g.floorQ[j] = (int16_t)r;
    }
    if (top < TRIG_DB * 16) return;
    tune(i);
    const int16_t lim = (int16_t)((fl >> 4) + REARM_DB);
    unsigned k = verify(lim);
    uint32_t d;
    int16_t r;
    do {                       /* to the end of the burst */
        A->delay_ms(2);
        r = A->rssi_dbm();
        d = A->ticks_ms() - t0;
    } while (r >= lim && d < LONG_MS);
    if (!k) {
        if (d >= LONG_MS) { g.floorQ[i] = (int16_t)(r * 16); return; }
        if (d < MIN_1G_MS) return;
        k = d >= MIN_2G_MS ? K_2G : K_1G;
    }
    g.type = (uint8_t)k;
    g.last = (uint8_t)i;
    g.tLast = t0 + d;          /* the end of the burst */
    g.n[k == K_2G]++;
    const bool autoExit = k != K_2G && !(g.mode & MODE_WATCH);
    alarm(autoExit);
    /* Still a carrier after the acknowledgement (a test transmitter keeping
     * its CW ID on the frame's carrier) does not time as a new burst: the
     * localisation pass gave the channel's floor the burst's level, and no
     * sweep ran during the alarm. It falls back once the carrier ends. */
    if (autoExit) handOff(freq(i), false);
}

/* One pass over the channels. A floor follows a fall by 1/4 per pass and a
 * rise by 1/16 (about 0.6 s): it sits near the noise, under its mean. It
 * followed a fall at once until 1.8: a single low reading took it 8 to 10 dB
 * under the noise, the noise itself then crossed TRIG_DB and held the radio
 * up to LONG_MS on a channel, again and again (406 Link, same scan, bench of
 * 07/11/2026: 11 bursts out of 20 missed, the radio deaf about 80 % of the
 * time). */
static void sweep(void){
    for (unsigned i = 0; i < N_CH && g.running; i++) {
        tune(i);
        const int32_t r = g.A->rssi_dbm() * 16;
        int32_t fl = g.floorQ[i];
        const int32_t d = (r - fl) >> 4;
        if (d > (int32_t)g.peak[i]) g.peak[i] = (uint8_t)(d > (int32_t)BASE_Y ? (int32_t)BASE_Y : d);
        if (d >= TRIG_DB) {
            /* confirmed 2 ms later: a beacon starts with 160 ms of carrier,
             * a noise spike does not last (and does not cost a localisation
             * pass, about 35 ms) */
            g.A->delay_ms(2);
            if ((g.A->rssi_dbm() * 16 - fl) >> 4 >= TRIG_DB) burst(i);
            continue;
        }
        g.floorQ[i] = (int16_t)(r < fl ? fl - ((fl - r) >> 2) : fl + ((r - fl) >> 4));
    }
}

/* ---- 121.5 MHz watch (MODE_ELT) ---- */

/* The VHF LNA and the AM demodulator, or back to the UHF LNA and FM. The
 * next tune() writes the frequency and restarts RX. */
static void front(bool vhf){
    static const uint16_t V[2][2] = { { 0x6b5a, 0x7400 }, { 0x6f5c, 0x7434 } };   /* REG_42, REG_2A */
    const app_api_t *A = g.A;
    A->bk_write(REG_GPIO, (uint16_t)((A->bk_read(REG_GPIO) & ~0x0Cu) | (vhf ? 0x04u : 0x08u)));
    A->bk_write(REG_31, (uint16_t)((A->bk_read(REG_31) & ~1u) | vhf));
    A->bk_write(REG_42, V[vhf][0]);
    A->bk_write(REG_2A, V[vhf][1]);
    /* VHF, one visit in two: the AGC fixed on its lowest LNA entry (index 0,
     * REG_10). FM broadcast overloads the front end on the airband (field
     * report of 03/11/2026); on the bench (two FM carriers whose third-order
     * product falls on the siren) it kept the siren under 20 dB more of them
     * than the AGC. The other visits keep the AGC, which still hears a siren
     * 6 kHz off that the low gain loses. Back to UHF: as it was. */
    if (vhf) g.r7e = A->bk_read(REG_7E);
    A->bk_write(REG_7E, vhf && g.eltLow ? (uint16_t)((g.r7e & 0x0FFFu) | 0x8000u) : g.r7e);
}

/* The siren's carrier within 10 kHz of f (an ELT may be 6 kHz off), with
 * the AGC and the watch's RF filter (bench: they found +3 and +6 kHz with 8
 * to 9 dB above the ends, where the stock AM filter or the low gain gave 2
 * to 5 dB): the strongest 1 kHz step, kept when it stands ELT_PEAK_DB above
 * the two ends, else f. */
__attribute__((noinline)) static uint32_t peak(uint32_t f){
    const app_api_t *A = g.A;
    A->bk_write(REG_7E, g.r7e);
    int32_t best = -32768, ends = 0;
    uint32_t at = f;
    for (uint32_t fi = f - 1000u; fi <= f + 1000u; fi += 100u) {
        tuneF(fi, ELT_STEP_MS);
        const int32_t r = A->rssi_dbm();
        if (fi == f - 1000u || fi == f + 1000u) ends += r;
        if (r > best) { best = r; at = fi; }
    }
    return 2 * best - ends >= 2 * ELT_PEAK_DB ? at : f;
}

/* z: zero crossings per 20 ms window. True when some period of 12 to 25
 * windows and phase fits a falling sawtooth with r^2 >= SIREN_R2 (Q8) over a
 * sweep of SIREN_SPAN crossings or more. Sums scaled by n (exact integers):
 * c = n C, vz = n^2 Vz, D = n Vt, so r^2 = C^2 / (Vz Vt) = c^2 n / (vz D),
 * both sides brought under 2^32 for the resident divider. */
static bool sawtooth(const uint8_t *z){
    const app_api_t *A = g.A;
    const int32_t n = ELT_NW;
    int32_t s = 0;
    for (unsigned w = 0; w < ELT_NW; w++) s += z[w];
    uint64_t vz = 0;
    for (unsigned w = 0; w < ELT_NW; w++) { const int32_t d = n * z[w] - s; vz += (uint64_t)((int64_t)d * d); }
    /* vz == 0 (a flat series): every c is 0, nothing passes below */
    for (int32_t P = 12; P <= 25; P++)
        for (int32_t ph = 0; ph < P; ph++) {
            int32_t c = 0, st = 0, st2 = 0, m = ph;
            for (unsigned w = 0; w < ELT_NW; w++) {
                const int32_t t = 2 * (P - 1 - m) - (P - 1);   /* falling ramp */
                c += (n * z[w] - s) * t;
                st += t;
                st2 += t * t;
                if (++m == P) m = 0;
            }
            if (c <= 0) continue;
            const uint32_t D = (uint32_t)(n * st2 - st * st);
            uint64_t nu = (((uint64_t)c * (uint32_t)c) << 8) * (uint32_t)n, de = vz * D;
            while ((nu >> 32) || (de >> 32)) { nu >>= 1; de >>= 1; }
            if (de && (uint32_t)A->uidivmod((uint32_t)nu, (uint32_t)de) >= SIREN_R2 &&
                (uint32_t)A->uidivmod((uint32_t)c * (uint32_t)(2 * (P - 1)), D) >= SIREN_SPAN)
                return true;
        }
    return false;
}

/* ELT_NW x 20 ms of PA4 audio on the current point: a siren? */
__attribute__((noinline)) static bool siren(void){
    const app_api_t *A = g.A;
    uint8_t z[ELT_NW];
    uint32_t (*const clk)(void) = A->cycles_48mhz;
    uint16_t (*const sample)(void) = A->af_capture_sample;
    A->set_af(APP_AF_FM);      /* PA4 follows the AF DAC (AM too on the BK4829) */
    int32_t dc = (int32_t)sample() << 4, hx = 0, hy = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0, m = 0;
    int st = 0;
    const uint32_t t0 = clk();
    uint32_t next = 0;
    for (unsigned w = 0; w < ELT_NW; w++) {
        unsigned k = 0;
        for (unsigned j = 0; j < ELT_WS; j++, next += CYC_PER_SAMPLE) {
            while (clk() - t0 < next) {}
            const int32_t x = (int32_t)sample() << 4;
            dc += (x - dc) >> 8;
            const int32_t d = x - dc;
            hy = (13466 * (hy + d - hx)) >> 14;           /* 300 Hz high-pass, Q14 */
            hx = d;
            const int32_t y = (2798 * (hy + x2) + 5597 * x1 + 8855 * y1 - 3665 * y2) >> 14;
            x2 = x1; x1 = hy; y2 = y1; y1 = y;            /* 1700 Hz Butterworth low-pass */
            m += ((y < 0 ? -y : y) - m) >> 6;
            if (st <= 0 && y > m >> 1) { st = 1; k++; }
            else if (st >= 0 && y < -(m >> 1)) { st = -1; k++; }
        }
        z[w] = (uint8_t)(k > 255u ? 255u : k);
    }
    return sawtooth(z);
}

/* One look at the ELT points: 121.500 then 121.375 MHz AM on the VHF LNA
 * (TEST: channel S in FM), each floor kept as a channel's; above it, two
 * listens. */
__attribute__((noinline)) static void eltCheck(void){
    const app_api_t *A = g.A;
    const bool vhf = !(g.mode & MODE_TEST);
    g.tElt = A->ticks_ms();
    g.eltLow ^= 1u;
    if (vhf) front(true);
    uint32_t f = 0;
    unsigned k = 0;
    bool hit = false;
    for (; k < 1u + vhf && !hit; k++) {
        f = vhf ? (k ? F_EX : F_ELT) : g.base + 7600u;
        tuneF(f, vhf ? (k ? ELT_STEP_MS : ELT_VHF_MS) : STEP_MS);
        int16_t *const flp = &g.floorQ[N_CH + 2u * g.eltLow + k];
        const int32_t r = A->rssi_dbm() * 16, fl = *flp;
        if ((r - fl) >> 4 < ELT_TRIG_DB) *flp = (int16_t)(r < fl ? r : fl + ((r - fl) >> 4));
        else {
            hit = siren() && siren();
            *flp = (int16_t)r;             /* a siren acknowledged, or voice, a carrier: not again */
            /* a siren: the other gain's floor takes its next reading, so the
             * alarm does not come twice; anything else: that gain may still
             * hear a siren under it (the AGC under FM overload, for one) */
            if (hit) g.floorQ[N_CH + 2u * !g.eltLow + k] = 0;
        }
    }
    if (hit && vhf) f = peak(f);      /* still on the VHF front end */
    if (vhf) front(false);
    if (!hit) return;
    g.type = K_ELT;
    g.st = 0;
    g.eltF = f;
    g.tLast = A->ticks_ms();
    const bool autoExit = !(g.mode & MODE_WATCH);
    alarm(autoExit);
    if (autoExit) handOff(f, true);
}

/* Set the pixels of row y (frame buffer coordinates) from x, n columns. */
static void row(unsigned x, unsigned y, unsigned n, unsigned dx){
    uint8_t *p = g.A->fb[y >> 3] + x;
    const uint8_t bit = (uint8_t)(1u << (y & 7u));
    for (; n; n--, p += dx) *p |= bit;
}

/* Wait up to ms for a key press: a key already held does not count. g.prevKey
 * follows the keys, so the main loop does not act on the key returned. */
static uint8_t newKey(unsigned ms){
    for (; ms >= 10u; ms -= 10u) {
        g.A->delay_ms(10);
        const uint8_t k = g.A->get_key(), was = g.prevKey;
        g.prevKey = k;
        if (k != was && k != APP_KEY_INVALID) return k;
    }
    return APP_KEY_INVALID;
}

/* MENU: the help page, until a key. */
static void help(void){
    char b[HELP_SIZE] __attribute__((aligned(4)));
    const char *p = b;
    g.A->asset_read(T_HELP, b, HELP_SIZE);
    g.A->display_clear();
    for (unsigned y = 0; y < HELP_LINES * 7u; y += 7u) {
        tiny(p, 0, y);
        while (*p++) {}
    }
    g.A->blit_full();
    while (newKey(1000) == APP_KEY_INVALID) {}
}

/* Until a key is pressed, or the AUTO deadline: beep, flash, invert. */
__attribute__((noinline)) static void alarm(bool autoExit){
    const app_api_t *A = g.A;
    const uint32_t torch = GPIOC_ODR & TORCH;
    draw();
    const uint32_t started = A->ticks_ms();
    uint8_t key = APP_KEY_INVALID;
    for (unsigned k = 0; key == APP_KEY_INVALID; k++) {
        unsigned t = 0;
        if (!(k & 3u)) { A->play_tone(BEEP_HZ, BEEP_MS); t = BEEP_MS; }   /* part of the wait */
        GPIOC_ODR ^= TORCH;
        A->led(!(k & 1u));
        uint8_t *p = A->status_line;   /* the status line, then the 7 frame lines */
        for (unsigned n = 0; n < 8u * 128u; n++, p++) {
            if (n == 128u) p = A->fb[0];
            *p = (uint8_t)~*p;
        }
        A->blit_status();
        A->blit_full();
        A->backlight_on();
        key = newKey(ALARM_MS - t);
        if (autoExit && A->ticks_ms() - started >= AUTO_ALARM_MS) break;
    }
    A->led(false);
    GPIOC_ODR = (GPIOC_ODR & ~TORCH) | torch;
}

static void draw(void){
    const app_api_t *A = g.A;
    /* The UI texts in one asset read into this word-aligned stack block. */
    char s[UI_SIZE] __attribute__((aligned(4)));
    char str[32];              /* the longest row: "GEN1 ERR TST 406.076 999S AGO" */
    char *o;
    A->asset_read(0, s, UI_SIZE);
    A->display_clear();
    A->status_clear();
    A->print_inverse(s + T_TITLE, 2, 0, true, true, (uint8_t)(2u + T_TITLE_CHARS * 4u));
    A->print_tiny(s + T_AUTO + (g.mode & MODE_WATCH) * MODE_STRIDE, 42, 1, true, true);
    if (g.mode & MODE_TEST) A->print_tiny(s + T_TEST, 66, 1, true, true);
    A->draw_battery();

    for (unsigned i = 0, x = COL_X; i < N_CH; i++, x += COL_W) {
        const unsigned h = g.peak[i];
        g.peak[i] = 0;
        for (unsigned y = BASE_Y - h; y <= BASE_Y; y++) row(x, y, BAR_W, 1);
        *A->append_u32(str, CH[i]) = '\0';
        tiny(str, x, LABEL_Y);
    }
    row(0, BASE_Y - TRIG_DB, 64, 2);   /* the trigger, dotted */
    row(0, 40, 64, 2);                 /* separator above the text rows */
    const unsigned test = (g.mode & MODE_TEST) >> 1;
    if (g.mode & MODE_ELT) tiny(s + T_EM + test * EM_STRIDE, 0, 0);   /* the watch is on */

    if (g.type) {
        o = put(str, s + T_KIND + (g.type - 1u) * T_KIND_STRIDE);
        if (g.st) o = put(o, s + T_TST);
        if (g.type == K_ELT) {     /* the measured frequency: "121.504" */
            const uint64_t q = A->uidivmod(g.eltF, 100000u);
            o = A->append_u32(o, (uint32_t)q);
            *o++ = '.';
            /* kHz: 365 to 510 on the airband, 695 in TEST: always 3 digits */
            o = A->append_u32(o, (uint32_t)A->uidivmod((uint32_t)(q >> 32), 100u));
        }
        else {
            o = put(o, s + T_MHZ + test * MHZ_STRIDE);
            o = A->append_u32(o, CH[g.last] + test * ((F_TEST - 43360000u) / 100u));
        }
        *o++ = ' ';
        uint32_t age = (uint32_t)A->uidivmod(A->ticks_ms() - g.tLast, 1000u);
        o = A->append_u32(o, age > 999u ? 999u : age);   /* 3 digits: the row fits 32 */
        o = put(o, s + T_AGO);
    } else o = put(str, s + T_NONE);
    *o = '\0';
    tiny(str, 0, 43);

    o = put(str, s + T_BURSTS);
    o = A->append_u32(o, g.n[0]);
    *o++ = ' ';
    o = put(o, s + T_2G);
    o = A->append_u32(o, g.n[1]);
    *o = '\0';
    tiny(str, 0, 49);
    tiny(s + T_KEYS, 128u - T_KEYS_CHARS * 4u, 49);

    A->blit_status();
    A->blit_full();
}

/* The band of the mode; its floors start over (0: they fall to the first
 * readings). */
static void band(void){
    g.base = g.mode & MODE_TEST ? F_TEST : F406;
    for (unsigned i = 0; i < N_CH + 4u; i++) g.floorQ[i] = 0;
}

__attribute__((section(".text.entry"),used))
void app_main(const app_api_t *api){
    g.A = api;                 /* the rest of g starts at 0 (overlay zeroed by the loader) */
    api->cfg_load(&g.mode, 1);
    if (g.mode > (MODE_WATCH | MODE_TEST | MODE_ELT)) g.mode = 0;   /* erased flash: AUTO, 406 MHz */
    band();
    api->beam_prepare();        /* temporary UHF FM VFO; the loader restores the original */
    api->audio_path(false);
    api->af_capture_open();    /* PA4 bias and ADC channel 4, restored by close */
    /* RAW receive, as EPIRB 406: RX HPF300 / LPF3K / de-emphasis and AFC off */
    api->bk_write(REG_2B, (uint16_t)((api->bk_read(REG_2B) | 0x0700u) & ~0x0007u));
    api->bk_write(REG_73, (uint16_t)(api->bk_read(REG_73) | 0x0010u));
    api->backlight_on();

    g.prevKey = APP_KEY_INVALID;
    unsigned frame = 0;
    g.running = true;
    while (g.running) {
        sweep();
        if ((g.mode & MODE_ELT) && g.running && api->ticks_ms() - g.tElt >= ELT_EVERY_MS) eltCheck();
        const uint8_t key = api->get_key();
        if (key != g.prevKey) {
            g.prevKey = key;
            if (key != APP_KEY_INVALID) api->backlight_on();
            if (key == APP_KEY_EXIT) g.running = false;
            if (key == APP_KEY_STAR) g.mode ^= MODE_WATCH;
            if (key == APP_KEY_1) { g.mode ^= MODE_ELT; band(); }
            if (key == APP_KEY_0) { g.mode ^= MODE_TEST; band(); }
            if (key == APP_KEY_MENU) { help(); frame = DRAW_SWEEPS; }   /* redraw now */
            api->cfg_save(&g.mode, 1);   /* each key: kept across a switch-off */
        }
        if (g.running && ++frame >= DRAW_SWEEPS) {
            frame = 0;
            draw();
            api->battery_sample();
            api->backlight_update();
        }
    }

    api->af_capture_close();
}
