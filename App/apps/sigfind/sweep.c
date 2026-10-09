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
 * ELT / homer swept-tone detector. Included by sigfind_app.c and by the host
 * test (test/sweep_test.c), so it uses no API call and no division.
 *
 * A 121.5 / 243 MHz homing signal is AM modulated by an audio tone sweeping
 * DOWN over at least 700 Hz between 1600 and 300 Hz, 2 to 4 times a second
 * (ICAO Annex 10 Vol. III, C/S T.001 for the 406 beacons' homer).
 *
 * Samples come from PA4 at 9.6 kHz in windows of SW_WIN (25 ms). Per sample:
 * DC removal and a Schmitt trigger whose hysteresis follows the amplitude, so
 * the transitions count the tone's zero crossings whatever its level (the PA4
 * coupling is close to a differentiator: the low end of the sweep arrives
 * weaker, but at the right frequency). Per window: f = transitions x 20 Hz.
 *
 * Per cycle: a sweep shows as windows going down, then a jump back up. A jump
 * ends a cycle, which counts as valid when:
 *   - its period is 200..600 ms;
 *   - its range is at least SW_SPAN and reaches below SW_LOMAX (noise reads
 *     1.3..2 kHz, a homer's bottom is at most 900 Hz);
 *   - it went up at most once and down in at least half of its windows (a
 *     sweep falls a little in every window, a voice-like tone that holds then
 *     drops does not). A jump that straddles two windows shows as two up
 *     steps, neither of which counts.
 * Consecutive valid cycles whose periods are within SW_PTOL and whose tops are
 * within SW_HTOL of each other raise the score; SW_DETECT in a row mean "swept
 * tone". Windows without a tone (silence, AF muted, gated modulation) are
 * skipped. test/sweep_test.c checks the legal sweeps (2..4 Hz, 700..1300 Hz)
 * are found within about 2 s, and that noise, a steady tone, voice-like hops
 * and an upward sweep never are.
 */

#define SW_FS       9600u
#define SW_WIN      240u        /* samples per window: 25 ms                  */
#define SW_HZ_FLIP  20u         /* Hz per transition in a window (2 per cycle) */
#define SW_HMIN     6           /* hysteresis floor, ADC LSB (ADC noise)      */
#define SW_AMIN     16u         /* tone amplitude below this: no tone, LSB    */
#define SW_FMIN     200u
#define SW_FMAX     2000u
#define SW_JUMP     400u        /* rise above the cycle's minimum = new sweep */
#define SW_EPS      60u         /* rise counted as an "up" step               */
#define SW_SPAN     400u        /* minimum range of a valid cycle             */
#define SW_LOMAX    1050u       /* a homer sweeps >= 700 Hz inside 1600..300: */
                                /* its bottom is <= 900 Hz (+ window average) */
#define SW_PMIN     200u        /* ms: 5 Hz                                   */
#define SW_PMAX     600u        /* ms: 1.7 Hz                                 */
#define SW_PTOL     100u        /* ms between two consecutive periods         */
#define SW_HTOL     200u        /* Hz between two consecutive sweep tops      */
#define SW_TIMEOUT  800u        /* ms without a jump: score back to 0         */
#define SW_DETECT   3u
#define SW_SCOREMAX 8u

typedef struct {
    int32_t  dcQ;               /* DC level, Q8                               */
    uint32_t envQ;              /* amplitude envelope, Q8                     */
    uint8_t  st, flips;         /* Schmitt state, transitions this window     */
    uint8_t  ups, n, score;     /* this cycle: up steps, windows with a tone  */
    uint8_t  lastUp;            /* the last window counted as an up step      */
    uint8_t  downs;             /* this cycle: windows lower than the last    */
    uint16_t f, prevF, hi, lo;  /* last window (0 = no tone), cycle range     */
    uint16_t amp;               /* last window's amplitude, LSB               */
    uint16_t per;               /* last valid period, ms                      */
    uint16_t shHi, shLo;        /* range of the last valid cycle              */
    uint32_t tJump;             /* ms of the last jump, 0 = none yet          */
} sw_t;

static inline void sw_push(sw_t *s, uint16_t x)
{
    s->dcQ += (((int32_t)x << 8) - s->dcQ) >> 6;          /* corner ~24 Hz */
    int32_t y = (int32_t)x - (s->dcQ >> 8);
    uint32_t a = (uint32_t)(y < 0 ? -y : y) << 8;
    if (a > s->envQ) s->envQ = a; else s->envQ -= s->envQ >> 8;   /* ~27 ms */
    int32_t h = (int32_t)(s->envQ >> 10);                  /* env / 4 */
    if (h < SW_HMIN) h = SW_HMIN;
    if (s->st) { if (y < -h) { s->st = 0; s->flips++; } }
    else       { if (y >  h) { s->st = 1; s->flips++; } }
}

/* End of a window, t = its time in ms. */
static void sw_window(sw_t *s, uint32_t t)
{
    uint16_t f = (uint16_t)(s->flips * SW_HZ_FLIP);
    s->flips = 0;
    s->amp = (uint16_t)(s->envQ >> 8);
    if (s->tJump && t - s->tJump > SW_TIMEOUT) s->score = 0;
    if (s->amp < SW_AMIN || f < SW_FMIN || f > SW_FMAX) { s->f = 0; return; }
    s->f = f;

    bool up = f > s->prevF + SW_EPS;
    if (s->n >= 2u && f >= s->lo + SW_JUMP && up) {
        /* the up step just before, if any, was the first half of this jump */
        uint32_t p = t - s->tJump;
        if (s->tJump && p >= SW_PMIN && p <= SW_PMAX &&
            (uint16_t)(s->hi - s->lo) >= SW_SPAN && s->lo <= SW_LOMAX &&
            (uint8_t)(s->ups - s->lastUp) <= 1u && s->n >= 3u &&
            s->downs * 2u >= s->n) {
            /* same period and same top as the last valid cycle: a homer */
            uint32_t dp = p > s->per ? p - s->per : s->per - p;
            uint16_t dh = s->hi > s->shHi ? s->hi - s->shHi : s->shHi - s->hi;
            if (dp > SW_PTOL || dh > SW_HTOL) s->score = 1;
            else if (s->score < SW_SCOREMAX) s->score++;
            s->per = (uint16_t)p; s->shHi = s->hi; s->shLo = s->lo;
        } else s->score = 0;
        s->tJump = t; s->ups = 0; s->downs = 0; s->n = 0;
    }
    /* an up step right after a jump is its second half: not counted */
    s->lastUp = up && s->n > 1u;
    if (!s->n) { s->hi = s->lo = f; }
    else {
        s->ups += s->lastUp;
        s->downs += f < s->prevF;
        if (f > s->hi) s->hi = f;
        if (f < s->lo) s->lo = f;
    }
    if (s->n < 255u) s->n++;
    s->prevF = f;
}

static inline bool sw_detected(const sw_t *s) { return s->score >= SW_DETECT; }
