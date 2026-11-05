#!/usr/bin/env python3
"""Model of the DSC RX receive chain. The channel and the integer front end
(band-pass, 1300/2100 Hz sliding correlators, per-tone AGC, three slicers with
their DPLL) are ATIS RX's, imported from atisrx/test/model_rx.py: DSC and ATIS
share the modulation and the ten-unit code. Only the sequence decoder differs,
class Slicer below: keep atisdscrx_app.c in step with it.

  hunt    the last 30 bits hold DX 125, RX 111..104, DX 125 (or RX n, DX 125,
          RX n-1), as in ATIS: symbol phase and position are known
  read    one symbol every 10 bits into buf (stream index INFO0 onwards). The
          end of the call is found on the fly: once character k (k >= MIN_EOS)
          reads as an end of sequence (117, 122, 127), from its DX copy, or
          from its RX copy when the DX one failed its check bits, the stream
          ends with the RX copy of the ECC, at INFO0 + 2 (k + 1) + RX_LAG;
          the earliest such end wins
  finish  each character from its DX copy, else its RX copy; no end of
          sequence before the last one, a known format specifier and the
          error check (dsc.decode)

  model_dsc.py           runs the test sweep, prints a table
"""
import os, random, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "atisrx", "test"))
sys.dont_write_bytecode = True
import dsc
import model_rx as atis_model                 # atisrx/test/model_rx.py (the ATIS one)
from model_rx import (channel, flipper_wave, sine_wave, sym, rc_hp, biquad_lp,
                      deemph, FS_SIM, FS_ADC, BIAS, LSB_PER_HZ)

SLOTS = 64          # stream symbols kept from INFO0: 30 characters at most (A..ECC)
MIN_EOS = 8         # the shortest call has its EOS at character 8 or later
EOS_SET = dsc.EOS_SET
FORMATS = tuple(dsc.FORMATS)


class Slicer(atis_model.Slicer):
    """ATIS RX's slicer (decision, DPLL, hunt) with the DSC sequence decoder."""

    def __init__(self, dm, wa, wb):
        super().__init__(dm, wa, wb)
        self.buf = [255] * SLOTS
        self.end = 0

    def bit(self, b):
        self.sr = (self.sr >> 1) | (b << 29)
        s2 = sym(self.sr >> 20)
        if self.idx == 0:
            if s2 == 255:
                return
            s0, s1 = sym(self.sr & 1023), sym((self.sr >> 10) & 1023)
            if s0 == 125 and s2 == 125 and 104 <= s1 <= 111:
                self.idx = 2 * (111 - s1) + 2
            elif s1 == 125 and 104 <= s2 <= 110 and s0 == s2 + 1:
                self.idx = 2 * (111 - s2) + 1
            else:
                return
            self.nb = 0
            self.end = 0
            self.buf = [255] * SLOTS
        else:
            self.nb += 1
            if self.nb < 10:
                return
            self.nb = 0
            self.idx += 1
        r = self.idx - dsc.INFO0
        if r < 0:
            return
        self.buf[r] = s2
        if s2 in EOS_SET:
            # the DX copy of character r / 2, or the RX copy of character
            # (r - 5) / 2 when its DX copy failed: the ECC's RX copy is 7, or
            # 2, symbols later. The earliest end wins: the DX tail (EOS ECC
            # EOS EOS) sends an EOS at character k + 2 before the RX copy of
            # the EOS at k comes in.
            e = 0
            if not r & 1 and r >> 1 >= MIN_EOS:
                e = self.idx + 7
            elif r & 1 and r >= 2 * MIN_EOS + dsc.RX_LAG and self.buf[r - dsc.RX_LAG] == 255:
                e = self.idx + 2
            if e and (not self.end or e < self.end):
                self.end = e
        if self.idx == self.end:
            self.idx = 0
            self.finish(r)
        elif r == SLOTS - 1:
            self.idx = 0                         # no end of sequence: drop it

    def finish(self, r):
        """r: the RX copy of the ECC. The characters A..ECC, each from its DX
        copy, else its RX copy; a known format specifier, the error check."""
        n = (r - dsc.RX_LAG) // 2 + 1           # A..ECC
        chars, x = [], 0
        for k in range(n):
            v = self.buf[2 * k]
            if v == 255:
                v = self.buf[2 * k + dsc.RX_LAG]
            if v == 255:
                return
            if 0 < k < n - 2 and v in EOS_SET:
                return                           # not the first end of sequence
            chars.append(v)
            x ^= v
        if x or chars[0] not in FORMATS:
            return
        self.dm.found(tuple(chars[:-1]))


# Decision weights (1300 Hz : 2100 Hz). ATIS RX's 2:3, 1:1, 3:2 suit this
# model, but on six captures of real calls on a UV-K1 (0.16 LSB/Hz, the 2100 Hz
# tone 1.3 to 3 dB down, 1.5 to 3.5 kHz of deviation) the right decisions lie
# around 4:3: 5:4, 4:3 and 7:5 keep 99.5 to 100 % of the bits right in all of
# them, while 6:5 and 3:2 fall to 84 % in one, 1:1 to 70 % and 2:3 to 60 %.
SLICERS = ((5, 4), (4, 3), (7, 5))


class Demod(atis_model.Demod):
    def __init__(self, **kw):
        kw.setdefault("slicers", SLICERS)
        super().__init__(**kw)
        self.sl = [Slicer(self, s.wa, s.wb) for s in self.sl]


def run(adc, **kw):
    d = Demod(**kw)
    for s in adc:
        d.sample(s)
    return d.ids


def test_bits():
    """name -> (bits, characters A..EOS the app must show, or None)."""
    out = {n: (dsc.bits(c), tuple(c[:-1])) for n, c in dsc.TEST_CALLS.items()}
    out["badecc"] = (dsc.bits(dsc.TEST_CALLS["routine"], bad_ecc=True), None)
    return out


CAPTURES = os.path.join(HERE, "captures")


def real_captures():
    """The ADC samples captured on a UV-K1 (ATIS DSC RX diagnostic build,
    9.6 kHz, 8-LSB steps) while a distress call came in from a LimeSDR at
    1.5, 2.5 and 3.5 kHz of deviation, from 300 samples before the phasing
    was found: each slicer's bits against the call. Returns the failures
    (a slicer below 98 %)."""
    import numpy as np
    want = "".join(map(str, dsc.bits(dsc.TEST_CALLS["distress"])))
    fails = 0
    for f in sorted(os.listdir(CAPTURES)):
        adc = [int(v) for v in np.fromfile(os.path.join(CAPTURES, f), dtype="<u2")]
        d = Demod()
        outs = []
        for sl in d.sl:
            got = []
            outs.append(got)
            sl.bit = (lambda b, o=sl.bit, g=got: (g.append(b), o(b)))
        for v in adc:
            d.sample(v)
        row = []
        for sl, got in zip(d.sl, outs):
            g = "".join(map(str, got))[40:]          # the DPLL settles first
            ok, o = max((sum(a == b for a, b in zip(g, want[o:])), o)
                        for o in range(len(want) - len(g) + 1))
            pct = 100.0 * ok / min(len(g), len(want) - o)
            fails += pct < 98
            row.append("%d:%d %5.1f%%" % (sl.wa, sl.wb, pct))
        print("capture %-28s %s" % (f, "  ".join(row)))
    return fails


def main():
    tb = test_bits()
    waves = {}

    def wave(name, src, twist=0.0):
        key = (name, src, twist)
        if key not in waves:
            b = tb[name][0]
            waves[key] = flipper_wave(b) if src == "flipper" else sine_wave(b, twist_db=twist)
        return waves[key]

    # The app reads the radio's standard FM audio (STD); the flat RAW path of
    # POCSAG RX is not used, and the weights above do not suit it.
    cases = []
    for mode in ("std",):
        for name in tb:
            cases.append(("flipper", 0.0, mode, name, 0, 0, 0))
        for noise in (1500, 3000, 4500, 6000):
            cases.append(("flipper", 0.0, mode, "distress", noise, 0, 0))
        for off in (-4000, 4000):
            cases.append(("flipper", 0.0, mode, "dist_ack", 1500, off, 0))
        for ppm in (-10000, 10000):
            cases.append(("flipper", 0.0, mode, "dist_ack", 1500, 0, ppm))
        for tw in (-6.0, 0.0, 4.2, 8.0):
            cases.append(("sine", tw, mode, "urgency", 1500, 0, 0))

    print("%-7s %5s %-4s %-10s %6s %6s %7s | %s" %
          ("source", "twist", "mode", "call", "noise", "off", "ppm", "ok"))
    bad = 0
    for src, tw, mode, name, noise, off, ppm in cases:
        ok = 0
        for seed in (1, 2, 3):
            got = run(channel(wave(name, src, tw), mode, noise, off, ppm, seed))
            want = [] if tb[name][1] is None else [tb[name][1]]
            ok += got == want
            bad += bool(got) and got != want
        print("%-7s %5.1f %-4s %-10s %6d %6d %7d | %d/3" % (src, tw, mode, name, noise, off, ppm, ok))
    bad += real_captures()
    rnd = random.Random(7)
    for mode in ("std",):
        x = [rnd.gauss(0, 6000) for _ in range(60 * FS_SIM)]
        x = rc_hp(biquad_lp(x, 5000), 30) if mode == "raw" else biquad_lp(deemph(rc_hp(x, 300)), 3000)
        adc = [min(4095, max(0, int(round(BIAS + LSB_PER_HZ * x[i * 10])))) for i in range(len(x) // 10)]
        got = run(adc)
        bad += len(got)
        print("noise   60 s  %-4s -> %s" % (mode, got or "nothing"))
    print("wrong calls: %d" % bad)
    return bad


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--one":
        print(run(channel(flipper_wave(test_bits()["distress"][0]))))
    else:
        sys.exit(1 if main() else 0)
