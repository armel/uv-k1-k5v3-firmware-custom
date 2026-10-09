/* Host test of the swept-tone detector (../sweep.c).
 *
 *   cc -O2 -Wall -Wextra -o /tmp/sweep_test sweep_test.c -lm && /tmp/sweep_test
 *
 * Synthesizes what PA4 sees at 9.6 kHz: the AM-demodulated homing tone (a
 * sawtooth sweep), white noise, the PA4 coupling (first-order high-pass, 1 kHz
 * by default, as measured on the K1 with POCSAG), mid-scale bias and the 12-bit
 * ADC. It is sampled the way the app does: two 25 ms windows, then a gap while
 * the app draws (GAP_MS). Positive cases must be detected within a few
 * seconds; negative ones (noise, a steady tone, a voice-like tone that hops,
 * an upward sweep) must never be.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include "../sweep.c"

#define FS     9600.0
static unsigned GAP_MS = 15u;    /* draw time between two ticks, unknown on the radio: varied */

static uint32_t rng = 1u;
static double urand(void){ rng = rng * 1664525u + 1013904223u; return ((rng >> 8) + 0.5) / 16777216.0; }
static double gauss(void){ return sqrt(-2.0 * log(urand())) * cos(2.0 * M_PI * urand()); }

enum { SIG_SWEEP, SIG_NOISE, SIG_NOISE_LP, SIG_TONE, SIG_HOPS };

typedef struct {
    const char *name;
    int    kind;
    double fTop, fBot, rateHz;  /* sweep: from fTop to fBot, rateHz per second */
    double amp, noise, hpHz;    /* LSB peak, LSB rms, PA4 high-pass corner (0 = none) */
    double seconds;
    bool   expect;
} case_t;

typedef struct { double firstMs; double detFrac; uint32_t windows; } res_t;

static res_t run(const case_t *c, uint32_t seed)
{
    sw_t s = {0};
    res_t r = { -1.0, 0.0, 0 };
    uint32_t det = 0, after = 0;
    double lp1 = 0.0, lp2 = 0.0, phase = 0.0, hpY = 0.0, hpX = 0.0, f = c->fTop, hopLeft = 0.0;
    double a = c->hpHz > 0 ? 1.0 / (1.0 + 2.0 * M_PI * c->hpHz / FS) : 1.0;
    uint64_t total = (uint64_t)(c->seconds * FS);
    uint32_t perTick = 2u * SW_WIN + (uint32_t)(GAP_MS * FS / 1000.0);
    rng = seed;

    for (uint64_t k = 0; k < total; k++) {
        double t = (double)k / FS;
        switch (c->kind) {
        case SIG_SWEEP: {
            double ph = fmod(t * c->rateHz, 1.0);
            f = c->fTop + (c->fBot - c->fTop) * ph;
            break; }
        case SIG_TONE:  f = c->fTop; break;
        case SIG_HOPS:
            if ((hopLeft -= 1.0 / FS) <= 0) { f = 300.0 + 1200.0 * urand(); hopLeft = 0.05 + 0.25 * urand(); }
            break;
        default: break;
        }
        phase += 2.0 * M_PI * f / FS;
        double x = (c->kind == SIG_NOISE || c->kind == SIG_NOISE_LP ? 0.0 : c->amp * sin(phase)) + c->noise * gauss();
        if (c->kind == SIG_NOISE_LP) {        /* two poles at 800 Hz: noise that reads lower */
            double b = 1.0 - exp(-2.0 * M_PI * 800.0 / FS);
            lp1 += b * (x - lp1); lp2 += b * (lp1 - lp2); x = 3.0 * lp2;
        }
        double y = c->hpHz > 0 ? a * (hpY + x - hpX) : x;
        hpX = x; hpY = y;

        uint32_t pos = (uint32_t)(k % perTick);
        if (pos >= 2u * SW_WIN) continue;                 /* the app is drawing */
        double v = 2048.0 + y;
        sw_push(&s, (uint16_t)(v < 0 ? 0 : v > 4095 ? 4095 : v));
        if (pos == SW_WIN - 1u || pos == 2u * SW_WIN - 1u) {
            uint32_t ms = 1000u + (uint32_t)(t * 1000.0);
            sw_window(&s, ms - ms % 10u);                 /* ticks_ms: 10 ms steps */
            r.windows++;
            bool d = sw_detected(&s);
            if (d && r.firstMs < 0) r.firstMs = t * 1000.0;
            if (t >= 3.0) { after++; det += d; }
        }
    }
    r.detFrac = after ? (double)det / after : 0.0;
    return r;
}

int main(void)
{
    static const case_t cases[] = {
        /* name                         kind       top   bot  rate  amp noise  hp   s     expect */
        { "ELT 2 Hz 1600>300",          SIG_SWEEP, 1600, 300, 2.0, 150,  5, 1000, 10,  true  },
        { "ELT 3 Hz 1600>300",          SIG_SWEEP, 1600, 300, 3.0, 150,  5, 1000, 10,  true  },
        { "ELT 4 Hz 1600>300",          SIG_SWEEP, 1600, 300, 4.0, 150,  5, 1000, 10,  true  },
        { "ELT 3 Hz 1600>900 (700 Hz)", SIG_SWEEP, 1600, 900, 3.0, 150,  5, 1000, 10,  true  },
        { "ELT 2 Hz 1600>900 (700 Hz)", SIG_SWEEP, 1600, 900, 2.0, 150,  5, 1000, 10,  true  },
        { "ELT 4 Hz 1000>300 (700 Hz)", SIG_SWEEP, 1000, 300, 4.0, 150,  5, 1000, 10,  true  },
        { "ELT 2 Hz 700 Hz SNR ~6 dB",  SIG_SWEEP, 1600, 900, 2.0, 150, 53, 1000, 10,  true  },
        { "ELT 3 Hz 1300>400",          SIG_SWEEP, 1300, 400, 3.0, 150,  5, 1000, 10,  true  },
        { "ELT 3 Hz no PA4 high-pass",  SIG_SWEEP, 1600, 300, 3.0, 150,  5,    0, 10,  true  },
        { "ELT 3 Hz weak (amp 40)",     SIG_SWEEP, 1600, 300, 3.0,  40,  5, 1000, 10,  true  },
        { "ELT 3 Hz SNR ~10 dB",        SIG_SWEEP, 1600, 300, 3.0, 150, 33, 1000, 10,  true  },
        { "ELT 3 Hz SNR ~6 dB",         SIG_SWEEP, 1600, 300, 3.0, 150, 53, 1000, 10,  true  },
        { "noise only",                 SIG_NOISE,    0,   0, 0.0,   0, 40, 1000, 600, false },
        { "noise only, no high-pass",   SIG_NOISE,    0,   0, 0.0,   0, 40,    0, 600, false },
        { "noise low-passed 800 Hz",    SIG_NOISE_LP, 0,   0, 0.0,   0, 40, 1000, 600, false },
        { "noise low-passed, no h-p",   SIG_NOISE_LP, 0,   0, 0.0,   0, 40,    0, 600, false },
        { "steady 1 kHz tone",          SIG_TONE,  1000,   0, 0.0, 150,  5, 1000, 60,  false },
        { "voice-like tone hops",       SIG_HOPS,     0,   0, 0.0, 150,  5, 1000, 600, false },
        { "upward sweep 300>1600 3 Hz", SIG_SWEEP,  300,1600, 3.0, 150,  5, 1000, 60,  false },
    };
    int fails = 0;
    static const unsigned GAPS[] = { 5u, 15u, 30u };
    for (unsigned gi = 0; gi < 3u; gi++) {
    GAP_MS = GAPS[gi];
    printf("-- draw gap %u ms between ticks\n", GAP_MS);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const case_t *c = &cases[i];
        unsigned seeds = c->expect ? 5u : 6u, ok = 0;
        double worstFirst = 0, minFrac = 1, maxFrac = 0;
        for (unsigned sd = 1; sd <= seeds; sd++) {
            res_t r = run(c, sd * 7919u);
            bool pass = c->expect ? (r.firstMs >= 0 && r.firstMs < 4000 && r.detFrac >= 0.8)
                                  : (r.firstMs < 0);
            ok += pass;
            if (r.firstMs > worstFirst) worstFirst = r.firstMs;
            if (r.detFrac < minFrac) minFrac = r.detFrac;
            if (r.detFrac > maxFrac) maxFrac = r.detFrac;
        }
        bool pass = ok == seeds;
        fails += !pass;
        if (c->expect)
            printf("%s %-28s %u/%u  first <= %5.0f ms  detected %3.0f%% (min) of the time after 3 s\n",
                   pass ? "PASS" : "FAIL", c->name, ok, seeds, worstFirst, 100 * minFrac);
        else
            printf("%s %-28s %u/%u  false detection %3.1f%% (max) of the time\n",
                   pass ? "PASS" : "FAIL", c->name, ok, seeds, 100 * maxFrac);
    }
    }
    printf("%s\n", fails ? "SOME CASES FAILED" : "ALL PASS");
    return fails != 0;
}
