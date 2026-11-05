#!/usr/bin/env python3
"""Pure-Python model of the APRS receive chain, and the integer demodulator the
radio app will run (written to be transliterated to C: ints, shifts, no division).

Chain: source (Flipper square-wave FSK from the .sub durations, or a real
station's sine AFSK with a given twist) -> discriminator output in Hz + offset +
white noise -> radio audio path (RAW: 5 kHz low-pass as in EPIRB 406; STD: 300 Hz
high-pass, 750 us de-emphasis, 3 kHz low-pass) -> 12-bit ADC on PA4 around 2048 at
9.6 kHz (optional clock error) -> Demod -> frames.

Demod (per ADC sample):
  DC removal -> band-pass -> 4 sliding correlators over one bit (8 samples):
  I/Q at 1200 and 2200 Hz -> magnitudes (max + 3/8 min) -> per-tone peak AGC
  -> 3 slicers, decision sign(wa*Mm*Ps - wb*Ms*Pm) with wa:wb = 2:3, 1:1, 3:2,
  each with its own DPLL (16-bit phase per bit, nudged 1/4 toward mid-bit on
  each transition), NRZI, HDLC (flag, destuff, abort), FCS and UI check.

  model_rx.py            runs the test sweep, prints a table
"""
import math, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ax25 import test_frames, hdlc_bits, crc16, decode
from flipper_aprs import nrzi_tones, edges

FS_SIM = 96000
FS_ADC = 9600
BIAS = 2048
LSB_PER_HZ = 0.065      # measured on the K1 (EPIRB 406 lab): ~2.3 kHz -> ~150 LSB
FLIPPER_DEV = 2380.0


# ---------------------------------------------------------------- channel ----

def flipper_wave(frame):
    """Discriminator output (Hz) of the Flipper square-wave FSK, box-averaged
    per simulation sample from the exact (rounded to us, as in the .sub) edges."""
    ed, end = edges(nrzi_tones(hdlc_bits(frame)))
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


def sine_wave(frame, dev=3000.0, twist_db=0.0):
    """A real station: phase-continuous sine AFSK, space tone twist_db louder."""
    tones = nrzi_tones(hdlc_bits(frame))
    spb = FS_SIM / 1200.0
    out, ph = [], 0.0
    lead = [0.0] * int(0.1 * FS_SIM)
    a_space = dev * 10 ** (twist_db / 20)
    for i in range(int(len(tones) * spb)):
        f = tones[int(i / spb)]
        ph += f / FS_SIM
        out.append((dev if f == 1200.0 else a_space) * math.sin(2 * math.pi * ph))
    return lead + out


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


def deemph(x, tau_us=750.0, ref_hz=1200.0):
    """1-pole de-emphasis, unity gain at ref_hz."""
    k = 1 - math.exp(-1 / (FS_SIM * tau_us * 1e-6))
    g = math.sqrt(1 + (2 * math.pi * ref_hz * tau_us * 1e-6) ** 2)
    y, acc = [], 0.0
    for v in x:
        acc += k * (v - acc)
        y.append(acc * g)
    return y


def channel(sig, mode="raw", noise_hz=0.0, off_hz=0.0, ppm=0.0, seed=1,
            gap_ms=150):
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

def _tab(f, fs):
    per = fs // math.gcd(fs, int(f))
    c = [int(round(127 * math.cos(2 * math.pi * f * n / fs))) for n in range(per)]
    s = [int(round(127 * math.sin(2 * math.pi * f * n / fs))) for n in range(per)]
    return c, s


def is_ui(f):
    """An APRS frame is a UI frame: the address field ends (bit 0 of an SSID
    byte) and is followed by control 0x03 and PID 0xF0. Rejects the rare noise
    burst that passes the 16-bit FCS in continuous decoding."""
    end = len(f) - 2
    e = 13
    while not (f[e] & 1) and e + 7 < end:
        e += 7
    return bool(f[e] & 1) and e + 2 < end and f[e + 1] == 0x03 and f[e + 2] == 0xF0


class Slicer:
    """One decision threshold + its own DPLL and HDLC. The decision is
    sign(wa*Mm*Ps - wb*Ms*Pm): wa/wb != 1 favours one tone, which recovers
    frames whose twist the per-tone AGC does not fully cancel (Direwolf's
    multi-slicer idea)."""
    def __init__(self, dm, wa, wb):
        self.dm, self.wa, self.wb = dm, wa, wb
        self.dprev = 0; self.phase = 0; self.last = 0
        self.sr = 0; self.ones = 0; self.byte = 0; self.nb = 0
        self.buf = bytearray(); self.inframe = False
        self.good = 0

    def step(self, a, b):
        dm = self.dm
        d = a * self.wa - b * self.wb
        # DPLL: sample at phase wrap (mid-bit), transitions pulled to mid-phase
        self.phase += dm.step
        if (d > 0) != (self.dprev > 0):
            self.phase += (dm.ctr - self.phase) >> dm.pll
        self.dprev = d
        if self.phase >= 65536:
            self.phase -= 65536
            lv = 1 if d > 0 else 0
            self.bit(1 if lv == self.last else 0)
            self.last = lv

    def bit(self, b):
        self.sr = (self.sr >> 1) | (b << 7)
        if self.sr == 0x7E:
            if self.inframe and len(self.buf) >= 18 and \
                    crc16(self.buf[:-2]) == self.buf[-2] | (self.buf[-1] << 8) and \
                    is_ui(self.buf):
                self.good += 1
                self.dm.frame(bytes(self.buf))
            self.buf = bytearray(); self.inframe = True
            self.ones = self.byte = self.nb = 0
            return
        if b:
            self.ones += 1
            if self.ones > 6:                   # abort / noise: wait for a flag
                self.inframe = False
                return
        else:
            if self.ones == 5:                  # stuffed zero
                self.ones = 0
                return
            self.ones = 0
        if not self.inframe:
            return
        self.byte |= b << self.nb
        self.nb += 1
        if self.nb == 8:
            if len(self.buf) < 330:
                self.buf.append(self.byte)
            else:
                self.inframe = False
            self.byte = self.nb = 0


class Demod:
    def __init__(self, fs=FS_ADC, agc=True, bp=True, att=5, dec=10, pll=2, ctr=36864,
                 slicers=((2, 3), (1, 1), (3, 2))):
        self.ctr, self.bp, self.att, self.dec, self.pll = ctr, bp, att, dec, pll
        # 2nd-order band-pass (RBJ, constant 0 dB peak) centred on
        # sqrt(1200*2200) = 1625 Hz, Q = 0.9: coefficients in Q14
        w0 = 2 * math.pi * 1625 / fs
        al = math.sin(w0) / (2 * 0.9)
        a0 = 1 + al
        self.b0 = int(round(al / a0 * 16384))
        self.a1 = int(round(-2 * math.cos(w0) / a0 * 16384))
        self.a2 = int(round((1 - al) / a0 * 16384))
        self.x1 = self.x2 = self.y1 = self.y2 = 0
        self.N = fs // 1200                    # correlation window = one bit
        self.cm, _ = _tab(1200, fs)            # 8 entries at 9.6 kHz
        self.cs, _ = _tab(2200, fs)            # 48 entries at 9.6 kHz
        self.im = self.qm = self.is_ = self.qs = 0
        self.ring = [[0, 0, 0, 0] for _ in range(self.N)]
        self.r = 0; self.km = 0; self.ks = 0
        self.dc = BIAS << 4
        self.pm = self.ps = 64
        self.agc = agc
        self.step = (65536 * 1200 + fs // 2) // fs
        self.sl = [Slicer(self, wa, wb) for wa, wb in slicers]
        self.frames = []
        self.maxab = 0

    def frame(self, f):
        if f not in self.frames:                # the same frame from several slicers
            self.frames.append(f)

    @staticmethod
    def mag(i, q):
        a, b = abs(i), abs(q)
        if a < b: a, b = b, a
        return a + (b >> 2) + (b >> 3)

    def sample(self, adc):
        self.dc += ((adc << 4) - self.dc) >> 6
        x = adc - (self.dc >> 4)
        if self.bp:
            y = (self.b0 * (x - self.x2) - self.a1 * self.y1 - self.a2 * self.y2) >> 14
            self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
            x = y
        # sin = cos read 3/4 period ahead: +6 of 8 (1200 Hz), +12 of 48 (2200 Hz:
        # 11 cycles per 48 samples, 12 samples = 2.75 cycles)
        cm, cs = self.cm, self.cs
        p = ((x * cm[self.km]) >> 8, (x * cm[(self.km + 6) & 7]) >> 8,
             (x * cs[self.ks]) >> 8, (x * cs[(self.ks + 12) % 48]) >> 8)
        o = self.ring[self.r]
        self.im += p[0] - o[0]; self.qm += p[1] - o[1]
        self.is_ += p[2] - o[2]; self.qs += p[3] - o[3]
        self.ring[self.r] = p
        self.r = (self.r + 1) % self.N
        self.km = (self.km + 1) % len(self.cm)
        self.ks = (self.ks + 1) % len(self.cs)
        mm, ms = self.mag(self.im, self.qm), self.mag(self.is_, self.qs)
        if self.agc:
            self.pm += (mm - self.pm) >> self.att if mm > self.pm else -(self.pm >> self.dec)
            self.ps += (ms - self.ps) >> self.att if ms > self.ps else -(self.ps >> self.dec)
            if self.pm < 16: self.pm = 16
            if self.ps < 16: self.ps = 16
            a, b = mm * self.ps, ms * self.pm
        else:
            a, b = mm, ms
        if a > self.maxab: self.maxab = a
        if b > self.maxab: self.maxab = b
        for s in self.sl:
            s.step(a, b)


def run(adc, **kw):
    d = Demod(**kw)
    for s in adc:
        d.sample(s)
    return d.frames


# ------------------------------------------------------------------ sweep ----

def main():
    fr = test_frames()
    waves = {}
    def wave(name, src, twist=0.0):
        key = (name, src, twist)
        if key not in waves:
            waves[key] = flipper_wave(fr[name]) if src == "flipper" \
                else sine_wave(fr[name], twist_db=twist)
        return waves[key]

    cases = []
    for mode in ("raw", "std"):
        for name in ("pos", "long", "badfcs"):
            cases.append(("flipper", 0.0, mode, name, 0, 0, 0))
        for noise in (1500, 3000, 4500, 6000):
            cases.append(("flipper", 0.0, mode, "pos", noise, 0, 0))
        for off in (-4000, 4000):
            cases.append(("flipper", 0.0, mode, "pos", 1500, off, 0))
        for ppm in (-10000, 10000):
            cases.append(("flipper", 0.0, mode, "long", 1500, 0, ppm))
        for tw in (-6.0, 0.0, 6.0):
            cases.append(("sine", tw, mode, "pos", 1500, 0, 0))

    print("%-7s %5s %-4s %-6s %6s %6s %7s | %-7s %-7s" %
          ("source", "twist", "mode", "frame", "noise", "off", "ppm", "agc", "no-agc"))
    for src, tw, mode, name, noise, off, ppm in cases:
        oks = []
        for seed in (1, 2, 3):
            adc = channel(wave(name, src, tw), mode, noise, off, ppm, seed)
            for agc in (True, False):
                got = run(adc, agc=agc)
                want = [] if name == "badfcs" else [fr[name]]
                oks.append((agc, got == want))
        res = ["%d/3" % sum(ok for a, ok in oks if a == agc) for agc in (True, False)]
        print("%-7s %5.0f %-4s %-6s %6d %6d %7d | %-7s %-7s" %
              (src, tw, mode, name, noise, off, ppm, res[0], res[1]))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--one":
        f = test_frames()["pos"]
        got = run(channel(flipper_wave(f)))
        print([decode(g) for g in got])
    else:
        main()
