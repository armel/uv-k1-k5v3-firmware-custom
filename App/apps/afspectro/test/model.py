#!/usr/bin/env python3
"""Model of the AF Spectro app (afspectro_app.c), function by function, with
the C's integer arithmetic: Hann window, 256-point FFT in block floating point,
levels in tenths of dB, peaks and parabolic interpolation, the smoothed
spectrum, the steady-tone MIN / MAX, the Bell 202 detection and twist, the
the frame (peak hold, sonogram). Checked on synthetic signals; render.py draws its
screens. The C's tables and constants are read from afspectro_app.c, so the
two cannot drift apart silently. Keep the arithmetic in step by hand.

  model.py            runs the checks
  model.py --png DIR  also writes the screens (render.py)
"""
import math, os, random, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.dont_write_bytecode = True
from gen_assets import a as ASSETS, SINQ, TICK_X

BLOB, FIELDS, _ = ASSETS.build()
OFF = {name: (off, size) for name, off, size in FIELDS}
CONST = dict(ASSETS.extra)

# ---- the C's constants and tables ----
C_SRC = open(os.path.join(HERE, "..", "afspectro_app.c")).read()


def cdef(name):
    m = re.search(r"#define\s+%s\s+\(?(-?\d+)" % name, C_SRC)
    return int(m.group(1))


def ctable(name):
    m = re.search(r"%s\[[^\]]*\]\s*=\s*\{([^}]*)\}" % name, C_SRC)
    return [int(v) for v in re.findall(r"-?\d+", m.group(1))]


N, BINS = cdef("N"), cdef("BINS")
CYC_9K6, BLOCKS_9K6 = cdef("CYC_9K6"), cdef("BLOCKS_9K6")
CLIP_LO, CLIP_HI = 16 - 2048, 4079 - 2048
FFT_LIMIT = cdef("FFT_LIMIT")
PEAK_LO, PEAK_HI, PEAK_GUARD = cdef("PEAK_LO"), cdef("PEAK_HI"), cdef("PEAK_GUARD")
PURE_DB, TWO_DB, OVER_DB = cdef("PURE_DB"), cdef("TWO_DB"), cdef("OVER_DB")
VALLEY_DB, FLOOR_DB = cdef("VALLEY_DB"), cdef("FLOOR_DB")
BELL_DB, BAND, TW_SHIFT = cdef("BELL_DB"), cdef("BAND"), cdef("TW_SHIFT")
EDGE_DB, BELL_HOLD = cdef("EDGE_DB"), cdef("BELL_HOLD")
BAR_H, SON_PAGES, STRIP_PAGES = cdef("BAR_H"), cdef("SON_PAGES"), cdef("STRIP_PAGES")
RANGE, KHI = ctable("RANGE"), ctable("KHI")
LOGT = list(BLOB[OFF["LOGT"][0]:OFF["LOGT"][0] + OFF["LOGT"][1]])
FS = [9600 << i for i in range(3)]
V_SPECTRO, V_SONO = range(2)
M_FM, M_AM, M_USB = range(3)


def cdiv(n, d):
    """C division: truncated toward zero."""
    q = abs(n) // abs(d)
    return q if (n < 0) == (d < 0) else -q


def sn(k):
    k &= 255
    s = 1
    if k >= 128:
        k -= 128; s = -1
    if k > 64:
        k = 128 - k
    return s * SINQ[k]


def lg(p):
    if not p:
        return 0
    e = 0
    while p >> (e + 1):
        e += 1
    return e * 30 + ((e * 13) >> 7) + LOGT[((p << (31 - e)) & 0xFFFFFFFF) >> 26 & 31]


def fft(re_, im):
    exp = 0
    j = 0
    for i in range(1, N):
        b = N >> 1
        while j & b:
            j ^= b; b >>= 1
        j |= b
        if i < j:
            re_[i], re_[j] = re_[j], re_[i]
    half, step = 1, N >> 1
    while half < N:
        m = 0
        for k in range(N):
            m |= abs(re_[k]) | abs(im[k])
        sh = 0
        while m >= FFT_LIMIT:
            m >>= 1; sh += 1
        if sh:
            for k in range(N):
                re_[k] >>= sh; im[k] >>= sh
            exp += sh
        for i in range(0, N, 2 * half):
            for jj in range(half):
                wr, wi = sn(jj * step + 64), -sn(jj * step)
                p, q = i + jj, i + jj + half
                xr, xi = re_[q], im[q]
                tr, ti = (wr * xr - wi * xi) >> 14, (wr * xi + wi * xr) >> 14
                for v in (re_[p] - tr, im[p] - ti, re_[p] + tr, im[p] + ti):
                    assert -32768 <= v < 32768, "int16 overflow"
                re_[q], im[q] = re_[p] - tr, im[p] - ti
                re_[p] += tr; im[p] += ti
        half <<= 1; step >>= 1
    return exp


def ratio(a, b):
    """10 log10(a / b) in tenths of dB, both cut to 32 bits together"""
    while (a | b) >> 32:
        a >>= 1; b >>= 1
    return lg(a) - lg(b)


def peak(L, ex):
    bk, best = PEAK_LO, -32768
    for k in range(PEAK_LO, PEAK_HI + 1):
        if ((k - ex + PEAK_GUARD) & 0xFFFFFFFF) <= 2 * PEAK_GUARD:
            continue
        if L[k] > best:
            best, bk = L[k], k
    return bk


def scroll(son, n, k, c):
    """bin k of a sonogram of n pages (BINS bytes each, LSB the top row) one
    row down, c its new top row"""
    for p in range(n):
        i = p * BINS + k
        v = son[i]
        son[i] = (v << 1 | c) & 0xFF
        c = v >> 7


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


class App:
    """The C's g, its analysis and frame functions."""
    def __init__(self, fsi=0, view=V_SPECTRO, raw=False, spk=False, rng=1):
        self.fsi, self.view, self.raw, self.spk, self.range = fsi, view, raw, spk, rng
        self.held = False
        self.mode = M_FM
        self.sono = 0                       # frame parity: dotted rows
        self.sq = True                      # squelch open (the C's g.sq, REG_0C bit 1)
        self.S = [0] * BINS
        self.f1 = self.f2 = self.l1 = self.l2 = 0
        self.two = self.bell = False
        self.reset()

    def reset(self):
        self.fs = 9600 << self.fsi
        self.kHi = KHI[self.fsi]
        self.fresh, self.bellFor = 1, 0
        self.elo = self.ehi = 0
        self.hold = [0] * BINS
        self.son = [0] * (SON_PAGES * BINS)
        self.strip = [0] * (STRIP_PAGES * BINS)
        self.tmin, self.tmax = 0xFFFF, 0

    def interp(self, L, k):
        a, b, c = L[k - 1], L[k], L[k + 1]
        num, den, d8 = c - a, 2 * b - a - c, 0
        if den > 0:
            d8 = clamp(cdiv(num * 128, den), -128, 128)
        return ((k * 256 + d8) * self.fs + 32768) >> 16, b + cdiv(num * d8, 1024)

    def analyze(self, x):
        dc = sum(x) >> 8
        self.clip = sum(1 for v in x if v < CLIP_LO or v > CLIP_HI)
        re_ = [((v - dc) * ((16384 - sn(n + 64)) >> 1)) >> 12 for n, v in enumerate(x)]
        im = [0] * N
        exp = fft(re_, im)
        L = [0] * BINS
        elo = ehi = 0
        kLo = 32 >> self.fsi
        for k in range(BINS):
            p = re_[k] * re_[k] + im[k] * im[k]
            if abs(k - kLo) <= BAND:
                elo += p
            if abs(k - self.kHi) <= BAND:
                ehi += p
            l = lg(p) + exp * 60
            L[k] = l
            if self.fresh:
                self.S[k] = l
            self.S[k] += (l - self.S[k]) >> 2
        self.fresh = 0
        k1 = peak(L, 0xFF)
        if self.bellFor == BELL_HOLD:
            sh = TW_SHIFT + self.fsi
            self.elo = self.elo - (self.elo >> sh) + (elo << (2 * exp))
            self.ehi = self.ehi - (self.ehi >> sh) + (ehi << (2 * exp))
            assert self.elo < 1 << 64 and self.ehi < 1 << 64
        if L[k1] - L[peak(L, k1)] >= PURE_DB:
            f = self.interp(L, k1)[0]
            self.tmin, self.tmax = min(self.tmin, f), max(self.tmax, f)

    def twist(self):
        return ratio(self.ehi, self.elo)

    def px(self, l):
        return clamp(cdiv((l - (self.mean - FLOOR_DB)) * BAR_H, RANGE[self.range] * 10), 0, BAR_H)

    def frame(self):
        S = self.S
        k1 = peak(S, 0xFF)
        k2 = peak(S, k1)
        self.f1, self.l1 = self.interp(S, k1)
        self.f2, self.l2 = self.interp(S, k2)
        self.mean = cdiv(sum(S[PEAK_LO:PEAK_HI + 1]), PEAK_HI + 1 - PEAK_LO)
        self.two = self.l2 >= self.l1 - TWO_DB and self.l2 >= self.mean + OVER_DB and \
            self.l2 >= S[(k1 + k2) >> 1] + VALLEY_DB
        kLo = 32 >> self.fsi
        sl, sh = S[kLo], S[self.kHi]
        if sl >= self.l1 - BELL_DB and sh >= self.l1 - BELL_DB and \
                sl >= S[(kLo * 21) >> 5] + EDGE_DB and sh >= S[self.kHi + (self.kHi >> 2)] + EDGE_DB:
            self.bellFor = BELL_HOLD
        elif self.bellFor:
            self.bellFor -= 1
        self.bell = self.bellFor != 0
        if self.bell:
            self.f1, self.f2, self.two = 1200, 2200, True
        on = cdiv(RANGE[self.range] * 10, 4)
        half = (on * 21) >> 5
        self.sono ^= 1
        for k in range(BINS):
            v = self.px(S[k])
            if self.hold[k]:
                self.hold[k] -= 1
            if v > self.hold[k]:
                self.hold[k] = v
            v = S[k] - self.mean
            c = 1 if self.sq and (v >= on or (v >= half and self.sono and not k & 1)) else 0
            scroll(self.son, SON_PAGES, k, c)
            scroll(self.strip, STRIP_PAGES, k, c)

    def run(self, sig):
        """sig in frames of BLOCKS_9K6 << fsi blocks"""
        k = BLOCKS_9K6 << self.fsi
        for f0 in range(0, len(sig) // N - k + 1, k):
            for b in range(f0, f0 + k):
                self.analyze(sig[b * N:(b + 1) * N])
            self.frame()
        return self

    def two_tones(self):
        """(lo Hz, hi Hz, TW tenths) as header() shows them."""
        lo, hi = min(self.f1, self.f2), max(self.f1, self.f2)
        tw = self.twist() if self.bell else self.l2 - self.l1 if self.f1 < self.f2 else self.l1 - self.l2
        return lo, hi, tw


# ----------------------------------------------------------------- signals --
def adc(sig):
    """The PA4 capture, centred: codes - 2048 within the 12-bit range."""
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


def voice(fs, n, f0=140.0, seed=11, noise=10):
    """voice-like: harmonics of a wandering pitch under two moving formants"""
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


def hiss(fs, n, sigma=1050, seed=5):
    """the unsquelched FM discriminator noise after de-emphasis (one pole
    at 1 kHz), about 3500 ADC codes peak to peak"""
    rnd = random.Random(seed)
    a, y, out = math.exp(-2 * math.pi * 1000 / fs), 0.0, []
    for _ in range(n):
        y = a * y + (1 - a) * rnd.gauss(0, sigma)
        out.append(y)
    return adc(out)


def sweep(fs, n, f_hi=1600, f_lo=400, rate=3.0, amp=500, noise=15, seed=9):
    """a homing beacon's falling sweep"""
    rnd = random.Random(seed)
    out, ph = [], 0.0
    for i in range(n):
        t = i / fs
        f = f_hi - (f_hi - f_lo) * ((t * rate + 0.3) % 1.0)
        ph += f / fs
        out.append(amp * math.sin(2 * math.pi * ph) + rnd.gauss(0, noise))
    return adc(out)


# ------------------------------------------------------------------ checks --
def main():
    ok = True

    def check(name, good, detail=""):
        nonlocal ok
        ok &= bool(good)
        print("OK  " if good else "FAIL", name, detail)

    check("LOGT follows SINQ", OFF["LOGT"][0] == OFF["SINQ"][0] + OFF["SINQ"][1])
    check("assets", len(BLOB) <= 3840, "UI %d B, assets %d B" % (CONST["UI_SIZE"], len(BLOB)))
    check("LOGT is 10 log10(1 + i/32), tenths",
          LOGT == [int(round(100 * math.log10(1 + i / 32))) for i in range(32)])
    check("lg() within 0.2 dB", all(abs(lg(p) - 100 * math.log10(p)) <= 2
                                    for p in (1, 2, 3, 10, 999, 12345, 2 ** 20 + 7, 2 ** 31 - 1)))
    check("KHI = 2200 Hz bins", KHI == [int(2200 * 256 / f + 0.5) for f in FS])
    check("TICK_X = 1..4 kHz at 9.6 kHz", TICK_X == [int(i * 1000 * 256 / 9600 + 0.5) for i in (1, 2, 3, 4)])

    # steady tones at every rate: the peak and the MIN / MAX
    worst = 0.0
    for fsi, fs in enumerate(FS):
        for f in [300, 600, 1000, 1750, 2200, 3000, 4000] + ([9000] if fs > 9600 else []) + \
                ([15000] if fs > 19200 else []):
            app = App(fsi).run(tones(fs, [(f, 400, 0.3)], 24 * N, noise=15, seed=f))
            err = max(abs(app.tmin - f), abs(app.tmax - f)) / fs * 256
            worst = max(worst, err)
            check("tone %5d Hz @ %5d" % (f, fs), err < 0.06 and abs(app.f1 - f) / fs * 256 < 0.12
                  and not app.two, "MIN %d MAX %d, peak %d Hz" % (app.tmin, app.tmax, app.f1))
    print("     worst MIN / MAX error %.3f bin" % worst)

    app = App(0).run(tones(9600, [], 6 * N, noise=200, seed=6))
    check("noise: no tone pair, no Bell 202", not app.two and not app.bell and not app.tmax,
          "two %s MIN/MAX %d/%d" % (app.two, app.tmin, app.tmax))
    app = App(0).run(tones(9600, [(1200, 400, 0)], 6 * N, noise=15, seed=7))
    check("1200 Hz alone is not Bell 202", not app.bell and not app.two)
    app = App(0).run(tones(9600, [(1200, 400, 0), (2200, 300, 1)], 64 * N, noise=15, seed=7))
    check("1200 + 2200 Hz tones: Bell 202 bands, -2.5 dB", app.bell and abs(app.two_tones()[2] / 10 + 2.5) < 0.6,
          "TW %+.1f dB" % (app.two_tones()[2] / 10))

    # Bell 202 twist, continuous and between loud hiss, every rate
    for fsi, fs in enumerate(FS):
        for tw in (0.0, -5.1, 3.0):
            res = []
            for seed in (1, 2, 3):
                app = App(fsi).run(afsk(fs, tw, 64 * N << fsi, seed=seed))
                lo, hi, got = app.two_tones()
                res.append((app.two, lo, hi, got / 10))
            check("AFSK twist %+.1f dB @ %5d" % (tw, fs),
                  all(t and (lo, hi) == (1200, 2200) and abs(g - tw) <= 1.5 for t, lo, hi, g in res),
                  " ".join("%+.1f" % r[3] for r in res))
            res = []
            for seed in (1, 2, 3):
                sig = []
                for k in range(4):
                    sig += hiss(fs, 18 * N << fsi, seed=seed * 10 + k)
                    sig += afsk(fs, tw, 30 * N << fsi, seed=seed * 10 + k + 1)
                sig += hiss(fs, 18 * N << fsi, seed=seed)
                res.append(App(fsi).run(sig).twist() / 10)
            check("packets in hiss, twist %+.1f dB @ %5d" % (tw, fs),
                  all(abs(r - tw) <= 1.0 for r in res), " ".join("%+.1f" % r for r in res))

    # DTMF '1': both tones and their twist
    app = App(0).run(tones(9600, [(697, 350, 0), (1209, 250, 1)], 12 * N, noise=10, seed=8))
    lo, hi, tw = app.two_tones()
    check("DTMF 697 + 1209 (high group -2.9 dB)", app.two and not app.bell and abs(lo - 697) < 8 and
          abs(hi - 1209) < 8 and abs(tw / 10 + 2.92) < 0.5, "%d / %d Hz TW %+.1f dB" % (lo, hi, tw / 10))

    # voice is no Bell 202, at any moment of 3 s
    app, k, bells = App(0), BLOCKS_9K6, 0
    sig = voice(9600, 108 * N)
    for f0 in range(0, 108, k):
        for b in range(f0, f0 + k):
            app.analyze(sig[b * N:(b + 1) * N])
        app.frame()
        bells += app.bell
    check("voice: never Bell 202", bells == 0, "%d of 36 frames" % bells)

    # a falling sweep: MIN / MAX span it, never a tone pair
    app, pairs = App(0), 0
    sig = sweep(9600, 72 * N)
    for f0 in range(0, 72, BLOCKS_9K6):
        for b in range(f0, f0 + BLOCKS_9K6):
            app.analyze(sig[b * N:(b + 1) * N])
        app.frame()
        pairs += app.two
    check("sweep 1600 > 400 Hz: MIN / MAX, no tone pair", 380 < app.tmin < 520 and
          1480 < app.tmax < 1620 and not pairs, "MIN %d MAX %d pairs %d" % (app.tmin, app.tmax, pairs))

    # clipping
    app = App(0)
    app.analyze(tones(9600, [(1000, 3000, 0)], N))
    check("clipping counted", app.clip > 50, "clip %d" % app.clip)

    # sonogram rows, the newest on top: 12 frames of hiss with the squelch
    # closed (rows 12..23, blank), then a Bell 202 packet (rows 0..11): its
    # 1050..2350 Hz plateau, little outside it
    def rows(app):
        return [[app.son[(r >> 3) * BINS + k] >> (r & 7) & 1 for k in range(BINS)]
                for r in range(SON_PAGES * 8)]
    app = App(0, V_SONO)
    app.sq = False
    app.run(hiss(9600, 12 * BLOCKS_9K6 * N, seed=12))
    app.sq = True
    app.run(afsk(9600, -2.0, 12 * BLOCKS_9K6 * N, seed=13))
    r = rows(app)
    closed = sum(map(sum, r[12:24]))
    inside = [sum(row[30:60]) for row in r[0:10]]
    outside = sum(sum(row[8:24]) + sum(row[70:120]) for row in r[0:10])
    t = App(0, V_SONO)
    scroll(t.son, SON_PAGES, 40, 1)
    for _ in range(SON_PAGES * 8 - 1):
        scroll(t.son, SON_PAGES, 40, 0)
    check("scroll: a top pixel reaches the bottom row across the pages", rows(t)[-1][40] == 1 and
          sum(map(sum, rows(t))) == 1)
    check("squelch closed: blank rows; open: the Bell 202 plateau", closed == 0 and min(inside) >= 8 and
          outside <= sum(inside) // 10,
          "closed %d px, plateau %d..%d px of 30 a row, outside %d px" % (closed, min(inside), max(inside), outside))

    if "--png" in sys.argv:
        out = sys.argv[sys.argv.index("--png") + 1]
        import render
        for name in render.screens(out):
            print("     wrote", name)

    print("ALL OK" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
