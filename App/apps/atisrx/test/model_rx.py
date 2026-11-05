#!/usr/bin/env python3
"""Pure-Python model of the ATIS receive chain, and the integer demodulator the
radio app runs (written to be transliterated to C: ints, shifts, no division).
The channel and the front end are those of aprsrx/test/model_rx.py with the
ATIS tones; keep atisrx_app.c in step with class Demod and class Slicer.

Chain: source (Flipper square-wave FSK from the .sub durations, or a real
transmitter's sine FSK with a given twist) -> discriminator output in Hz +
offset + white noise -> radio audio path (RAW: 5 kHz low-pass; STD: 300 Hz
high-pass, 750 us de-emphasis, 3 kHz low-pass) -> 12-bit ADC on PA4 around 2048
at 9.6 kHz (optional clock error) -> Demod -> identities.

Demod (per ADC sample):
  DC removal -> band-pass -> 4 sliding correlators over one bit (8 samples):
  I/Q at 1300 Hz (Y, bit 1) and 2100 Hz (B, bit 0) -> magnitudes -> per-tone
  peak AGC -> 3 slicers, decision sign(wa*My*Pb - wb*Mb*Py), each with its own
  DPLL (16-bit phase per bit, nudged 1/8 toward mid-bit on each transition:
  APRS RX's 1/4 lost half the noisy sequences here, the symbols having few
  transitions; 1/8 still follows a +-1 % clock, 1/16 does not) and its own
  sequence decoder:
    hunt   the last 30 bits hold DX 125, RX 111..104, DX 125 (or RX n, DX 125,
           RX n-1): symbol phase and position in the sequence are known
    read   one symbol every 10 bits up to the 34th, then the DX/RX choice, the
           format specifier, the end of sequence and the error check (atis.py)

  model_rx.py            runs the test sweep, prints a table
"""
import math, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atis
from flipper_atis import tones, edges, test_bits, F_Y, F_B

FS_SIM = 96000
FS_ADC = 9600
BIAS = 2048
LSB_PER_HZ = 0.065      # measured on the K1 (EPIRB 406 lab): ~2.3 kHz -> ~150 LSB
FLIPPER_DEV = 2380.0


# ---------------------------------------------------------------- channel ----

def flipper_wave(bits):
    """Discriminator output (Hz) of the Flipper square-wave FSK, box-averaged
    per simulation sample from the exact (rounded to us, as in the .sub) edges."""
    ed, end = edges(tones(bits))
    ed = [float(round(t)) for t in ed]
    n = int(end * 1e-6 * FS_SIM)
    T = 1e6 / FS_SIM
    out, lv, k = [], 1, 0
    for i in range(n):
        t0, t1 = i * T, (i + 1) * T
        acc, t = 0.0, t0
        while k < len(ed) and ed[k] < t1:
            acc += (ed[k] - t) * (1 if lv else -1)
            t = ed[k]
            lv ^= 1
            k += 1
        acc += (t1 - t) * (1 if lv else -1)
        out.append(FLIPPER_DEV * acc / T)
    return out


def sine_wave(bits, dev=2000.0, twist_db=0.0):
    """A type-approved transmitter: phase-continuous sine FSK after a 100 ms
    carrier, the 2100 Hz tone twist_db louder (TX pre-emphasis, modulation
    index 1: +4.2 dB before the receiver's de-emphasis)."""
    tn = tones(bits)
    spb = FS_SIM / 1200.0
    out, ph = [], 0.0
    a_b = dev * 10 ** (twist_db / 20)
    for i in range(int(len(tn) * spb)):
        f = tn[int(i / spb)]
        ph += f / FS_SIM
        out.append((dev if f == F_Y else a_b) * math.sin(2 * math.pi * ph))
    return [0.0] * int(0.1 * FS_SIM) + out


def biquad_lp(x, fc):
    w = math.tan(math.pi * fc / FS_SIM)
    q = 1 / math.sqrt(2)
    n = 1 / (1 + w / q + w * w)
    b0 = w * w * n; b1 = 2 * b0; a1 = 2 * (w * w - 1) * n; a2 = (1 - w / q + w * w) * n
    y, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
    for v in x:
        o = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y.append(o)
    return y


def rc_hp(x, fc):
    a = math.exp(-2 * math.pi * fc / FS_SIM)
    y, acc, prev = [], 0.0, 0.0
    for v in x:
        acc = a * (acc + v - prev)
        prev = v
        y.append(acc)
    return y


def deemph(x, tau_us=750.0, ref_hz=1300.0):
    """1-pole de-emphasis, unity gain at ref_hz."""
    k = 1 - math.exp(-1 / (FS_SIM * tau_us * 1e-6))
    g = math.sqrt(1 + (2 * math.pi * ref_hz * tau_us * 1e-6) ** 2)
    y, acc = [], 0.0
    for v in x:
        acc += k * (v - acc)
        y.append(acc * g)
    return y


def channel(sig, mode="raw", noise_hz=0.0, off_hz=0.0, ppm=0.0, seed=1, gap_ms=150):
    rnd = random.Random(seed)
    gap = int(gap_ms * 1e-3 * FS_SIM)
    # no carrier around the burst: the discriminator outputs full-scale noise
    x = [rnd.gauss(0, 6000) for _ in range(gap)]
    x += [v + off_hz + rnd.gauss(0, noise_hz) for v in sig]
    x += [rnd.gauss(0, 6000) for _ in range(gap)]
    if mode == "raw":
        x = rc_hp(biquad_lp(x, 5000), 30)
    else:
        x = biquad_lp(deemph(rc_hp(x, 300)), 3000)
    step = FS_SIM / FS_ADC * (1 + ppm * 1e-6)
    out, p = [], 0.0
    while p < len(x) - 1:
        i = int(p); f = p - i
        v = x[i] * (1 - f) + x[i + 1] * f
        out.append(min(4095, max(0, int(round(BIAS + LSB_PER_HZ * v + rnd.gauss(0, 1))))))
        p += step
    return out


# ------------------------------------------------------ integer demodulator --

def cos_table(f, fs=FS_ADC):
    per = fs // math.gcd(fs, int(f))
    return [int(round(127 * math.cos(2 * math.pi * f * n / fs))) for n in range(per)]


def quarter(f, fs=FS_ADC):
    """Index offset that reads the sine in the cos table: 3/4 of a period ahead."""
    per = fs // math.gcd(fs, int(f))
    cyc = int(f) // math.gcd(fs, int(f))
    for n in range(per):
        if (cyc * n * 4) % (per * 4) == 3 * per:
            return n
    raise ValueError(f)


def sym(w):
    """Symbol of a 10-bit word (first bit received in bit 0), or 255 when the
    check bits do not hold the number of zeros of the 7 information bits."""
    v = w & 127
    zeros = 7
    for i in range(7):
        zeros -= (v >> i) & 1
    chk = ((w >> 7) & 1) * 4 + ((w >> 8) & 1) * 2 + ((w >> 9) & 1)
    return v if chk == zeros else 255


class Slicer:
    """One decision threshold with its own DPLL and sequence decoder."""
    SLOTS = atis.SYMBOLS - atis.INFO0      # symbols 14..33 are kept

    def __init__(self, dm, wa, wb):
        self.dm, self.wa, self.wb = dm, wa, wb
        self.dprev = 0; self.phase = 0
        self.sr = 0                # the last 30 bits, the oldest in bit 0
        self.idx = 0               # index of the last symbol read; 0 = hunting
        self.nb = 0                # bits of the symbol being read
        self.buf = [255] * self.SLOTS

    def step(self, a, b):
        dm = self.dm
        d = a * self.wa - b * self.wb
        self.phase += dm.step
        if (d > 0) != (self.dprev > 0):
            self.phase += (dm.ctr - self.phase) >> dm.pll
        self.dprev = d
        if self.phase >= 65536:
            self.phase -= 65536
            self.bit(1 if d > 0 else 0)        # the lower tone (Y) is a 1

    def bit(self, b):
        self.sr = (self.sr >> 1) | (b << 29)
        s2 = sym(self.sr >> 20)
        if self.idx == 0:
            # Hunt. Phasing: DX 125 in the even positions, RX 111, 110, ... 104
            # in the odd ones; RX 111 - p closes pair p.
            if s2 == 255:
                return
            s0, s1 = sym(self.sr & 1023), sym((self.sr >> 10) & 1023)
            if s0 == 125 and s2 == 125 and 104 <= s1 <= 111:
                self.idx = 2 * (111 - s1) + 2          # s2 is the DX of the next pair
            elif s1 == 125 and 104 <= s2 <= 110 and s0 == s2 + 1:
                self.idx = 2 * (111 - s2) + 1          # s2 is the RX of its pair
            else:
                return
            self.nb = 0
            self.buf = [255] * self.SLOTS
            self.store(s2)
            return
        self.nb += 1
        if self.nb < 10:
            return
        self.nb = 0
        self.idx += 1
        self.store(s2)
        if self.idx == atis.SYMBOLS - 1:
            self.idx = 0
            self.finish()

    def store(self, v):
        if self.idx >= atis.INFO0:
            self.buf[self.idx - atis.INFO0] = v

    def finish(self):
        """Each information symbol from its DX copy, else its RX copy; then the
        format specifier, the end of sequence, the digits and the error check."""
        info, x = [], 0
        for k in range(8):
            v = self.buf[2 * k]
            if v == 255:
                v = self.buf[2 * k + atis.RX_LAG]
            if v == 255:
                return
            info.append(v)
            x ^= v
        if x or info[0] != atis.FORMAT or info[6] != atis.EOS:
            return
        if any(v > 99 for v in info[1:6]):
            return
        self.dm.found("".join("%02d" % v for v in info[1:6]))


# Decision weights (1300 Hz : 2100 Hz). The former 2:3, 1:1, 3:2 suit this
# model, but ADC samples captured on a UV-K1 (ATIS DSC RX shares this front end)
# put the right decisions around 4:3: 5:4, 4:3 and 7:5 keep 99.5 to 100 % of
# the bits right, 2:3, 1:1 and 3:2 fall to about 60, 70 and 84 %. On the radio
# ATIS RX went from 7 sequences in 40 to 39 (issue #705). The model cannot
# tell the two sets apart in the STD path; with these weights the flat RAW path
# no longer decodes, and the app does not use it.
class Demod:
    def __init__(self, fs=FS_ADC, att=5, dec=10, pll=3, ctr=36864,
                 slicers=((5, 4), (4, 3), (7, 5))):
        self.ctr, self.att, self.dec, self.pll = ctr, att, dec, pll
        # 2nd-order band-pass (RBJ, constant 0 dB peak) centred on
        # sqrt(1300*2100) = 1652 Hz, Q = 0.9: coefficients in Q14
        self.b0, self.a1, self.a2 = bandpass(fs)
        self.x1 = self.x2 = self.y1 = self.y2 = 0
        self.N = fs // 1200                    # correlation window = one bit
        self.cy, self.cb = cos_table(F_Y, fs), cos_table(F_B, fs)   # 96 and 32 entries
        self.qy, self.qb = quarter(F_Y, fs), quarter(F_B, fs)       # 72 and 8
        self.iy = self.qy_ = self.ib = self.qb_ = 0
        self.ring = [[0, 0, 0, 0] for _ in range(self.N)]
        self.r = 0; self.ky = 0; self.kb = 0
        self.dc = BIAS << 4
        self.py = self.pb = 64
        self.step = (65536 * 1200 + fs // 2) // fs
        self.sl = [Slicer(self, wa, wb) for wa, wb in slicers]
        self.ids = []

    def found(self, ident):
        if ident not in self.ids:               # the same burst from several slicers
            self.ids.append(ident)

    @staticmethod
    def mag(i, q):
        a, b = abs(i), abs(q)
        if a < b: a, b = b, a
        return a + (b >> 2) + (b >> 3)

    def sample(self, adc):
        self.dc += ((adc << 4) - self.dc) >> 6
        x = adc - (self.dc >> 4)
        y = (self.b0 * (x - self.x2) - self.a1 * self.y1 - self.a2 * self.y2) >> 14
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        cy, cb = self.cy, self.cb
        p = ((y * cy[self.ky]) >> 8, (y * cy[(self.ky + self.qy) % len(cy)]) >> 8,
             (y * cb[self.kb]) >> 8, (y * cb[(self.kb + self.qb) % len(cb)]) >> 8)
        o = self.ring[self.r]
        self.iy += p[0] - o[0]; self.qy_ += p[1] - o[1]
        self.ib += p[2] - o[2]; self.qb_ += p[3] - o[3]
        self.ring[self.r] = p
        self.r = (self.r + 1) % self.N
        self.ky = (self.ky + 1) % len(cy)
        self.kb = (self.kb + 1) % len(cb)
        my, mb = self.mag(self.iy, self.qy_), self.mag(self.ib, self.qb_)
        self.py += (my - self.py) >> self.att if my > self.py else -(self.py >> self.dec)
        self.pb += (mb - self.pb) >> self.att if mb > self.pb else -(self.pb >> self.dec)
        if self.py < 16: self.py = 16
        if self.pb < 16: self.pb = 16
        a, b = my * self.pb, mb * self.py
        for s in self.sl:
            s.step(a, b)


def bandpass(fs=FS_ADC, fc=math.sqrt(F_Y * F_B), q=0.9):
    w0 = 2 * math.pi * fc / fs
    al = math.sin(w0) / (2 * q)
    a0 = 1 + al
    return (int(round(al / a0 * 16384)), int(round(-2 * math.cos(w0) / a0 * 16384)),
            int(round((1 - al) / a0 * 16384)))


def run(adc, **kw):
    d = Demod(**kw)
    for s in adc:
        d.sample(s)
    return d.ids


# ------------------------------------------------------------------ sweep ----

def main():
    tb = test_bits()
    waves = {}
    def wave(name, src, twist=0.0):
        key = (name, src, twist)
        if key not in waves:
            b = tb[name][0]
            waves[key] = flipper_wave(b) if src == "flipper" else sine_wave(b, twist_db=twist)
        return waves[key]

    # the app reads the radio's standard FM audio (STD), not the flat RAW path
    cases = []
    for mode in ("std",):
        for name in ("pc8075", "mmsi", "badecc"):
            cases.append(("flipper", 0.0, mode, name, 0, 0, 0))
        for noise in (1500, 3000, 4500, 6000):
            cases.append(("flipper", 0.0, mode, "fm1234", noise, 0, 0))
        for off in (-4000, 4000):
            cases.append(("flipper", 0.0, mode, "fm1234", 1500, off, 0))
        for ppm in (-10000, 10000):
            cases.append(("flipper", 0.0, mode, "fm1234", 1500, 0, ppm))
        for tw in (-6.0, 0.0, 4.2, 8.0):
            cases.append(("sine", tw, mode, "pc8075", 1500, 0, 0))

    print("%-7s %5s %-4s %-7s %6s %6s %7s | %s" %
          ("source", "twist", "mode", "id", "noise", "off", "ppm", "ok"))
    bad = 0
    for src, tw, mode, name, noise, off, ppm in cases:
        ok = 0
        for seed in (1, 2, 3):
            got = run(channel(wave(name, src, tw), mode, noise, off, ppm, seed))
            want = [] if tb[name][1] is None else [tb[name][1]]
            ok += got == want
            bad += bool(got) and got != want
        print("%-7s %5.1f %-4s %-7s %6d %6d %7d | %d/3" % (src, tw, mode, name, noise, off, ppm, ok))
    # noise only: nothing may come out (60 s of open squelch per path)
    rnd = random.Random(7)
    for mode in ("std",):
        x = [rnd.gauss(0, 6000) for _ in range(60 * FS_SIM)]
        x = rc_hp(biquad_lp(x, 5000), 30) if mode == "raw" else biquad_lp(deemph(rc_hp(x, 300)), 3000)
        adc = [min(4095, max(0, int(round(BIAS + LSB_PER_HZ * x[i * 10])))) for i in range(len(x) // 10)]
        got = run(adc)
        bad += len(got)
        print("noise   60 s  %-4s -> %s" % (mode, got or "nothing"))
    print("wrong identities: %d" % bad)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--one":
        print(run(channel(flipper_wave(test_bits()["pc8075"][0]))))
    else:
        main()
