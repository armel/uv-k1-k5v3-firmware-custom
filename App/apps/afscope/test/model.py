#!/usr/bin/env python3
"""Model of the AF Scope app (afscope_app.c), function by function, with the
C's integer arithmetic: block statistics, the two-pass zero-crossing frequency
counter (mean period, then the periods over and under it), the steady-tone
test, the TUNE average, the trigger, the TUNE fold and the audio path per
demodulation. Checked on synthetic signals; render.py draws the screens. The C's tables and constants are read
from afscope_app.c, so the two cannot drift apart silently. Keep the
arithmetic in step by hand.

  model.py            runs the checks
  model.py --png DIR  also writes the screens (render.py)
"""
import math, os, random, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.dont_write_bytecode = True
from gen_assets import a as ASSETS, TONES

BLOB, FIELDS, _ = ASSETS.build()
OFF = {name: (off, size) for name, off, size in FIELDS}
CONST = dict(ASSETS.extra)

C_SRC = open(os.path.join(HERE, "..", "afscope_app.c")).read()


def cdef(name):
    m = re.search(r"#define\s+%s\s+\(?(-?\d+)" % name, C_SRC)
    return int(m.group(1))


def ctable(name):
    m = re.search(r"%s\[[^\]]*\]\s*=\s*\{([^}]*)\}" % name, C_SRC)
    return [int(v) for v in re.findall(r"-?\d+", m.group(1))]


NMAX, TRIG_WIN, TUNE_N, TUNE_FSI = cdef("NMAX"), cdef("TRIG_WIN"), cdef("TUNE_N"), cdef("TUNE_FSI")
CLIP_LO, CLIP_HI = 16 - 2048, 4079 - 2048
HYST_MIN, SPREAD_SH = cdef("HYST_MIN"), cdef("SPREAD_SH")
SPAN, TB_FSI, TB_DSH = ctable("SPAN"), ctable("TB_FSI"), ctable("TB_DSH")
TB_COUNT = CONST["TB_COUNT"]
V_SCOPE, V_TUNE = range(2)
M_FM, M_AM, M_USB = range(3)


def chex(name):
    m = re.search(r"#define\s+%s\s+\(?(0x[0-9A-Fa-f]+|\d+)" % name, C_SRC)
    return int(m.group(1), 0)


AF_USB, AF_BYPASS, APP_AF_FM = cdef("AF_USB"), cdef("AF_BYPASS"), 1
RAW_2B, RAW_73 = chex("RAW_2B"), chex("RAW_73")


_AF_NAMES = {"APP_AF_FM": APP_AF_FM, "AF_BYPASS": AF_BYPASS, "AF_USB": AF_USB}
AF_OUT = [_AF_NAMES[t] for t in re.findall(r"\w+", re.search(r"AF_OUT\[[^\]]*\]\s*=\s*\{([^}]*)\}",
                                                                C_SRC).group(1))]


def path(mode, raw, r2B=0, r73=0):
    """the C's path(): (REG_2B, REG_73, AF output)"""
    m = 0xFFFF if raw else 0
    return r2B | (m & RAW_2B), r73 | (m & RAW_73), AF_OUT[mode * 2 + raw]


def nearest(table, f):
    """the needle's search: the first of the nearest values, and the distance"""
    best, dd = 0, f - table[0]
    for i in range(1, len(table)):
        e = f - table[i]
        if abs(e) < abs(dd):
            best, dd = i, e
    return best, dd


def cdiv(n, d):
    """C division: truncated toward zero."""
    q = abs(n) // abs(d)
    return q if (n < 0) == (d < 0) else -q


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


class App:
    """The C's g, setup, crossings and analyze."""
    def __init__(self, view=V_SCOPE, tb=2, raw=False, spk=False, gain=0, edge=0, pers=0, span=0):
        self.view, self.tb, self.raw, self.spk = view, tb, raw, spk
        self.gain, self.edge, self.pers, self.span = gain, edge, pers, span
        self.held = False
        self.mode = M_FM
        self.rssi, self.rf10 = -87, 14550000
        self.setup()

    def setup(self):
        v = self.view
        self.fsi = TUNE_FSI if v else TB_FSI[self.tb]
        self.dsh = 0 if v else TB_DSH[self.tb]
        self.n = TUNE_N if v else TRIG_WIN + (128 << self.dsh)
        self.fs = 9600 << self.fsi
        self.favg = 0
        self.persbuf = bytearray(5 * 128)

    def crossings(self, pbar):
        s = self.smp
        h = max(self.pp >> 3, HYST_MIN)
        prev, last, armed = s[0] - self.dc, -1, False
        for n in range(1, self.n):
            v = s[n] - self.dc
            if v < -h:
                armed = True
            elif armed and prev < 0 <= v:
                t = ((n - 1) << 8) + cdiv((-prev) << 8, v - prev)
                armed = False
                if not pbar:
                    if not self.cnt:
                        self.t0 = t
                    self.cnt += 1
                elif last >= 0:
                    p, e = t - last, pbar >> SPREAD_SH
                    if p > pbar + e:
                        self.slo += p; self.nlo += 1
                    elif p < pbar - e:
                        self.shi += p; self.nhi += 1
                last = t
            prev = v
        self.tl = last

    def analyze(self, x):
        assert len(x) == self.n
        self.smp = list(x)
        self.mn, self.mx = min(x), max(x)
        self.dc = cdiv(sum(x), self.n)
        self.pp = self.mx - self.mn
        self.clip = sum(1 for v in x if v < CLIP_LO or v > CLIP_HI)
        self.cnt = self.nlo = self.nhi = self.slo = self.shi = 0
        self.fmean = 0
        self.crossings(0)
        steady = False
        if self.cnt >= 3:
            c1, fs8 = self.cnt - 1, self.fs << 8
            self.p8 = cdiv(self.tl - self.t0 + (c1 >> 1), c1)
            self.fmean = cdiv(fs8 + (self.p8 >> 1), self.p8)
            self.crossings(self.p8)
            self.flo = cdiv(fs8, cdiv(self.slo, self.nlo)) if self.nlo else self.fmean
            self.fhi = cdiv(fs8, cdiv(self.shi, self.nhi)) if self.nhi else self.fmean
            steady = (self.nlo + self.nhi) << SPREAD_SH < self.cnt
        self.steady = steady
        prev = self.favg
        dv = (self.fmean << 4) - prev
        self.favg = (prev + (dv >> 2) if prev else self.fmean << 4) if steady else 0
        self.ftone = (self.favg + 8) >> 4
        self.stab = abs(dv) >> 4 if steady and prev else 0
        h = max(self.pp >> 3, HYST_MIN)
        sg = -1 if self.edge else 1
        prev, armed, self.i0 = (x[0] - self.dc) * sg, False, 0
        for n in range(1, TRIG_WIN):
            v = (x[n] - self.dc) * sg
            if v < -h:
                armed = True
            elif armed and prev < 0 <= v:
                self.i0 = n
                break
            prev = v

    def run(self, sig):
        """sig cut into blocks of self.n; the last block stays"""
        for b in range(len(sig) // self.n):
            self.analyze(sig[b * self.n:(b + 1) * self.n])
        return self


# ----------------------------------------------------------------- signals --
def adc(sig):
    return [clamp(int(round(v)), -2048, 2047) for v in sig]


def tones(fs, parts, n, noise=0.0, seed=1, dc=7):
    rnd = random.Random(seed)
    return adc([sum(a * math.sin(2 * math.pi * f * i / fs + p) for f, a, p in parts)
                + rnd.gauss(0, noise) + dc for i in range(n)])


def afsk(fs, twist_db, n, amp=600.0, seed=3, noise=20.0):
    rnd = random.Random(seed)
    a2 = amp * 10 ** (twist_db / 20)
    out, ph, space, bit_t = [], 0.0, False, 0.0
    for _ in range(n):
        bit_t += 1200 / fs
        if bit_t >= 1:
            bit_t -= 1
            if rnd.random() < 0.5:
                space = not space
        f, a = (2200, a2) if space else (1200, amp)
        ph += f / fs
        out.append(a * math.sin(2 * math.pi * ph) + rnd.gauss(0, noise))
    return adc(out)


def hiss(fs, n, sigma=1050, seed=5):
    rnd = random.Random(seed)
    a, y, out = math.exp(-2 * math.pi * 1000 / fs), 0.0, []
    for _ in range(n):
        y = a * y + (1 - a) * rnd.gauss(0, sigma)
        out.append(y)
    return adc(out)


def voice(fs, n, f0=140.0, seed=11, noise=10):
    rnd = random.Random(seed)
    out, ph = [], 0.0
    for i in range(n):
        t = i / fs
        f = f0 * (1 + 0.15 * math.sin(2 * math.pi * 2.3 * t))
        ph += f / fs
        f1, f2 = 600 + 250 * math.sin(2 * math.pi * 3.1 * t), 1700 + 500 * math.sin(2 * math.pi * 1.7 * t)
        v = 0.0
        for h in range(1, 24):
            fh = h * f
            g = math.exp(-((fh - f1) / 250) ** 2) + 0.6 * math.exp(-((fh - f2) / 350) ** 2) + 0.05
            v += g * math.sin(2 * math.pi * h * ph)
        out.append(250 * v + rnd.gauss(0, noise))
    return adc(out)


# ------------------------------------------------------------------ checks --
def main():
    ok = True

    def check(name, good, detail=""):
        nonlocal ok
        ok &= bool(good)
        print("OK  " if good else "FAIL", name, detail)

    check("assets", len(BLOB) <= 3840 and OFF["TONE_HZ"][0] % 4 == 0 and
          OFF["TONE_HZ"][0] + OFF["TONE_HZ"][1] == CONST["UI_SIZE"],
          "UI %d B, assets %d B" % (CONST["UI_SIZE"], len(BLOB)))
    check("timebases fit the block", all(TRIG_WIN + (128 << d) <= NMAX for d in TB_DSH) and TUNE_N <= NMAX)

    # TUNE: steady tones at 19.2 kHz, the reading after six blocks
    worst = 0.0
    for f in (300, 600, 697, 1000, 1209, 1750, 2200, 3000, 4500, 6000):
        app = App(V_TUNE).run(tones(19200, [(f, 400, 0.3)], 6 * TUNE_N, noise=15, seed=f))
        err = abs(app.ftone - f) / f
        worst = max(worst, err)
        check("TUNE %4d Hz" % f, app.favg and err < 0.001 and app.stab <= 2,
              "%d Hz +-%d" % (app.ftone, app.stab))
    print("     worst TUNE error %.3f %%" % (100 * worst))
    app = App(V_TUNE).run(tones(19200, [(1750, 60, 0)], 6 * TUNE_N, noise=12, seed=5))
    check("TUNE weak 1750 Hz (pp ~120, noise 12)", app.favg and abs(app.ftone - 1750) <= 3 and app.stab <= 5,
          "%d +-%d" % (app.ftone, app.stab))
    for name, sig in (("DTMF 697 + 1209", tones(19200, [(697, 350, 0), (1209, 250, 1)], 6 * TUNE_N, noise=10)),
                      ("AFSK", afsk(19200, 0, 6 * TUNE_N)), ("voice", voice(19200, 6 * TUNE_N)),
                      ("hiss", hiss(19200, 6 * TUNE_N))):
        app = App(V_TUNE).run(sig)
        check("TUNE %s: no reading" % name, not app.favg,
              "steady %s, %d / %d Hz" % (app.steady, app.flo, app.fhi))

    # SCOPE: the counter at every timebase
    for tb in range(TB_COUNT):
        app = App(V_SCOPE, tb=tb)
        fs = app.fs
        for f in [f for f in (300, 1000, 1750, 4000, 9000) if 3 * fs / app.n < f < fs / 4]:
            app.run(tones(fs, [(f, 400, 0.2)], app.n, noise=15, seed=f + tb))
            check("SCOPE tb %d (%5d Hz, n %d): %4d Hz" % (tb, fs, app.n, f),
                  app.steady and abs(app.fmean - f) / f < 0.01, "%d Hz" % app.fmean)
    for tb in (0, 2):
        app = App(V_SCOPE, tb=tb)
        res = []
        for seed in (1, 2, 3):
            app.run(afsk(app.fs, 0, app.n, seed=seed))
            res.append((app.steady, app.flo, app.fhi))
        check("SCOPE tb %d AFSK: two frequencies near 1200 / 2200" % tb,
              all(not st and 1100 < lo < 1400 and 2000 < hi < 2350 for st, lo, hi in res),
              " ".join("%d/%d" % (lo, hi) for _, lo, hi in res))

    # the trigger: a rising (falling) crossing in the window
    app = App(V_SCOPE, tb=2)
    app.run(tones(9600, [(1000, 500, 1.0)], app.n, noise=5, seed=3))
    v0, v1 = app.smp[app.i0 - 1] - app.dc, app.smp[app.i0] - app.dc
    check("rising trigger", 0 < app.i0 < TRIG_WIN and v0 < 0 <= v1, "i0 %d (%d, %d)" % (app.i0, v0, v1))
    app = App(V_SCOPE, tb=2, edge=1)
    app.run(tones(9600, [(1000, 500, 1.0)], app.n, noise=5, seed=3))
    v0, v1 = app.smp[app.i0 - 1] - app.dc, app.smp[app.i0] - app.dc
    check("falling trigger", 0 < app.i0 < TRIG_WIN and v0 > 0 >= v1, "i0 %d (%d, %d)" % (app.i0, v0, v1))

    app = App(V_SCOPE, tb=2)
    app.run(tones(9600, [(1000, 3000, 0)], app.n))
    check("clipping counted", app.clip > 50, "clip %d, pp %d" % (app.clip, app.pp))

    # the audio path: the discriminator bypass in FM only, the filters off in RAW
    want = {(M_FM, 0): 1, (M_FM, 1): 9, (M_AM, 0): 1, (M_AM, 1): 1, (M_USB, 0): 5, (M_USB, 1): 5}
    got = {(m, r): path(m, r)[2] for m in (M_FM, M_AM, M_USB) for r in (0, 1)}
    check("AF output per mode and path (AM RAW keeps the AM audio)", got == want,
          " ".join("%s%s:%d" % ("FM AM USB".split()[m], "/RAW" if r else "", a) for (m, r), a in got.items()))
    check("RAW turns the filters and AFC off, in every mode",
          all(path(m, 1)[:2] == (RAW_2B, RAW_73) and path(m, 0)[:2] == (0, 0) for m in (M_FM, M_AM, M_USB)))


    if "--png" in sys.argv:
        out = sys.argv[sys.argv.index("--png") + 1]
        import render
        for name in render.screens(out):
            print("     wrote", name)

    print("ALL OK" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
