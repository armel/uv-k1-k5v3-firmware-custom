#!/usr/bin/env python3
"""Pure-Python model of the RS41 receive chain and of the integer demodulator
in ../decrs41.c (keep both in step).

Chain: the Flipper .sub bursts (or the generator's bits) -> discriminator
output in Hz (+/- 2380) + white noise, full-scale noise around the burst ->
RAW audio path: an unknown high-pass corner (hp, the swept parameter; POCSAG RX
on the radio points near 1 kHz) and the measured 3.3 kHz wall -> 12-bit ADC on
PA4 around 2048 at 19.2 kHz (optional clock error) -> Demod.

Demod (per ADC sample), as decrs41.c:
  DC removal (Q8, 1/256), peak of |y| (attack 1/4, decay 1/256)
  two hysteresis slicers: the level flips when y passes +/- num/8 of the peak
  (num = 2, 4) and holds in between, whatever the droop of the high-pass
  each slicer: DPLL pulled by its level changes (target 3/4, gain 1/4) that
  also learns the bit rate (step gain 1/256, within +/-5 %: the MCU clock is
  off by a few percent on some radios), a bit per wrap, the last 64 bits
  compared with the sync word or its complement
  (<= 6 wrong bits), then 2496 data bits.

The previous demodulator (sign of the integrated level, sync accepted on a
soft correlation of 0.6) is modelled too: it decodes none of the Flipper
files, even through a 30 Hz corner, because the start of the preamble after
the steady carrier passes for a sync and holds it busy for the real one.

  python3 -B model_rx.py        sweep table: one character per burst
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flipper_rs41 as F

FS_SIM = 96000
FS = 19200
BIAS = 2048
LSB_PER_HZ = 0.065        # measured on the K1 (EPIRB 406 lab)
DEV = 2380.0              # Flipper preset 2FSKDev238Async

SYNC0, SYNC1 = 0x11CAB610, 0xF8129622
STEP, PLL_CTR, PLL_SHIFT, SYNC_ERR = 16384, 49152, 2, 6
FREQ_SHIFT, STEP_MAX = 8, 820
NUMS = (2, 4)
M32 = 0xFFFFFFFF


# ---------------------------------------------------------------- channel ----

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


def sub_wave(path):
    """Discriminator output (Hz) of a Flipper RAW file: + run = one tone."""
    runs = []
    with open(path) as f:
        for line in f:
            if line.startswith("RAW_Data:"):
                runs += [int(v) for v in line.split()[1:]]
    sig, t = [], 0.0
    for r in runs:
        n = int(round((t + abs(r)) * FS_SIM / 1e6)) - int(round(t * FS_SIM / 1e6))
        t += abs(r)
        sig += [DEV if r > 0 else -DEV] * n
    return sig


def channel(sig, hp=30.0, noise=0.0, ppm=0.0, seed=1, gap_ms=300):
    rnd = random.Random(seed)
    gap = int(gap_ms * 1e-3 * FS_SIM)
    x = [rnd.gauss(0, 6000) for _ in range(gap)]
    x += [v + rnd.gauss(0, noise) for v in sig]
    x += [rnd.gauss(0, 6000) for _ in range(gap)]
    x = rc_hp(biquad_lp(biquad_lp(x, 3300), 3300), hp)
    step = FS_SIM / FS * (1 + ppm * 1e-6)
    out, p = [], 0.0
    while p < len(x) - 1:
        i = int(p); f = p - i
        v = x[i] * (1 - f) + x[i + 1] * f
        out.append(min(4095, max(0, int(round(BIAS + LSB_PER_HZ * v + rnd.gauss(0, 1))))))
        p += step
    return out


# ------------------------------------------------------ integer demodulator --

def near64(a, b):
    n = 0
    for v in (a, b):
        while v:
            v &= v - 1
            n += 1
            if n > SYNC_ERR:
                return False
    return True


class Slicer:
    def __init__(self, num):
        self.num, self.lev, self.phase, self.step = num, 0, 0, STEP
        self.sr0 = self.sr1 = 0
        self.rearm()

    def rearm(self):
        self.state, self.nbit = 0, 0
        self.inv = 0
        self.frm = bytearray(F.FRAME_LEN)

    def bit(self, b):
        if self.state == 0:
            self.sr0 = ((self.sr0 >> 1) | (self.sr1 << 31)) & M32
            self.sr1 = (self.sr1 >> 1) | (b << 31)
            if self.nbit < 64:
                self.nbit += 1
                return False
            x0, x1 = self.sr0 ^ SYNC0, self.sr1 ^ SYNC1
            if near64(x0, x1):
                self.inv = 0
            elif near64(x0 ^ M32, x1 ^ M32):
                self.inv = 1
            else:
                return False
            self.state = 1
            return False
        b ^= self.inv
        if b:
            self.frm[self.nbit >> 3] |= 1 << (self.nbit & 7)
        self.nbit += 1
        if self.nbit < F.FRAME_LEN * 8:
            return False
        self.state = 2
        return True


class Demod:
    def __init__(self, nums=NUMS):
        self.dc, self.amp, self.yp = -1, 0, 0
        self.sl = [Slicer(n) for n in nums]

    def rearm(self):
        for s in self.sl:
            s.rearm()

    def push(self, adc):
        xq = adc << 8
        if self.dc < 0:
            self.dc = xq
        self.dc += (xq - self.dc) >> 8
        y0 = xq - self.dc
        y = y0 + ((y0 - self.yp) >> 1)     # edge lift, as decrs41.c
        self.yp = y0
        a = -y if y < 0 else y
        if a > self.amp:
            self.amp += (a - self.amp) >> 2
        else:
            self.amp -= self.amp >> 8
        done = False
        for s in self.sl:
            if s.state == 2:
                done = True
                continue
            t = (self.amp >> 3) * s.num
            lev = s.lev
            if y > t:
                lev = 1
            elif y < -t:
                lev = 0
            s.phase += s.step
            if lev != s.lev:
                err = PLL_CTR - s.phase
                if err > 32768:
                    err -= 65536
                s.phase += err >> PLL_SHIFT
                s.step = min(max(s.step + (err >> FREQ_SHIFT), STEP - STEP_MAX), STEP + STEP_MAX)
                s.lev = lev
            if s.phase < 65536:
                continue
            s.phase -= 65536
            if s.bit(lev):
                done = True
        return done


class OldDemod:
    """The first decrs41.c: integrate-and-dump, sign, soft sync at 0.6."""
    SPB = (FS << 8) // 4800
    SYNC_BITS = [((SYNC0 | SYNC1 << 32) >> i) & 1 for i in range(64)]

    def __init__(self):
        self.init(-1)

    def init(self, mean):
        self.meanQ8, self.ph, self.integ, self.pe = mean, self.SPB // 2, 0, 0
        self.nbit = self.guard = self.state = self.inv = self.sign = self.cpos = self.nfill = 0
        self.soft = [0] * 64
        self.frm = bytearray(F.FRAME_LEN)

    def rearm(self):
        self.init(self.meanQ8)

    def emit(self, v):
        if self.state == 0:
            self.soft[self.cpos] = v
            self.cpos = (self.cpos + 1) & 63
            self.nfill = min(64, self.nfill + 1)
            if self.nfill < 64:
                return
            score = en = 0
            for i in range(64):
                s = self.soft[(self.cpos + i) & 63]
                en += abs(s)
                score += s if self.SYNC_BITS[i] else -s
            if not (en > 240 and abs(score) * 5 > en * 3):
                return
            self.inv, self.nbit, self.guard, self.state = score < 0, 64, 0, 1
            return
        self.guard += 1
        if self.guard > F.FRAME_LEN * 8 + 128:
            self.state = 2
            return
        if (v >= 0) ^ self.inv:
            self.frm[self.nbit >> 3] |= 1 << (self.nbit & 7)
        self.nbit += 1
        if self.nbit >= F.FRAME_LEN * 8:
            self.state = 2

    def push(self, x):
        if self.state == 2:
            return True
        xq = x << 8
        if self.meanQ8 < 0:
            self.meanQ8 = xq
        e = xq - self.meanQ8
        if self.state != 1:
            self.meanQ8 += (e + 32) >> 6
        sg = 1 if e > 0 else 0
        if sg != self.sign:
            a, b = abs(self.pe), abs(e)
            err = ((a << 8) // (a + b + 1)) - 128 - self.ph
            while err < -(self.SPB // 2):
                err += self.SPB
            while err > self.SPB // 2:
                err -= self.SPB
            self.ph += err >> 3
            self.sign = sg
        self.pe = e
        if self.ph < 256:
            part = (e * max(self.ph, 0)) >> 8
            self.emit(max(-32767, min(32767, (self.integ + part) >> 8)))
            self.integ = e - part
            self.ph += self.SPB - 256
            return self.state == 2
        self.integ += e
        self.ph -= 256
        return False


# ------------------------------------------------------------------ sweep ----

def wire_of(frame):
    return bytes(v ^ F.MASK[i & 63] for i, v in enumerate(frame))


STATUS = range(0x39, 0x39 + 44)


def frames(adc, ref, dm):
    """Bursts whose STATUS block came out exact (what the app counts)."""
    good = 0
    for x in adc:
        if not dm.push(x):
            continue
        if isinstance(dm, OldDemod):
            good += all(dm.frm[i] == ref[i] for i in STATUS)
            dm.rearm()
            continue
        for s in dm.sl:
            if s.state != 2:
                continue
            if all(s.frm[i] == ref[i] for i in STATUS):
                good += 1
                dm.rearm()
                break
            s.rearm()
    return good


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ref = wire_of(F.test_frame())
    ref_gnss = wire_of(F.gnss_frame())
    hps = (30, 300, 700, 1000, 1500)
    print("Flipper files, STATUS frames read (badcrc must read 0)")
    print("%-13s %-5s %s" % ("file", "demod", "  ".join("hp%-5d" % h for h in hps)))
    for name in ("valid", "inverted", "badcrc", "badpos",
                 "gnss_valid", "gnss_inverted", "gnss_badpos"):
        w = sub_wave(os.path.join(here, "flipper", "rs41_%s.sub" % name))
        r = ref_gnss if name.startswith("gnss") else ref
        for label, mk in (("new", Demod), ("old", OldDemod)):
            row = [frames(channel(w, hp=hp, seed=hp), r, mk()) for hp in hps]
            print("%-13s %-5s %s" % (name, label, "  ".join("%-7d" % v for v in row)))
    print()
    print("noise and clock sweep, new demod: per corner, noise 0/1000/1500,"
          " each clock error -3%,-1%,0,+1%,+3% x polarity (S = read)")
    bits = F.wire_bits(F.test_frame())
    lead = [DEV] * int(0.1 * FS_SIM)
    spb = FS_SIM / 4800
    for hp in hps:
        cols = []
        for noise in (0, 1000, 1500):
            c = ""
            for ppm in (-30000, -10000, 0, 10000, 30000):
                for inv in (0, 1):
                    seq = [i & 1 for i in range(320)] + [b ^ inv for b in bits]
                    sig = lead + [(-DEV if seq[int(i / spb)] else DEV)
                                  for i in range(int(len(seq) * spb))] + [DEV] * 960
                    adc = channel(sig, hp=hp, noise=noise, ppm=ppm, seed=hp * 7 + noise + ppm // 100 + inv)
                    c += "S" if frames(adc, ref, Demod()) else "."
            cols.append(c)
        print("hp%-5d %s" % (hp, " ".join(cols)))
    noise = channel([], hp=1000, gap_ms=10000, seed=9)
    dm, syncs = Demod(), 0
    for x in noise:
        busy = any(s.state == 1 for s in dm.sl)
        dm.push(x)
        syncs += not busy and any(s.state == 1 for s in dm.sl)
        for s in dm.sl:
            if s.state == 2:
                s.rearm()
    print("\nreceiver noise alone, 20 s: %d false syncs" % syncs)


if __name__ == "__main__":
    main()
