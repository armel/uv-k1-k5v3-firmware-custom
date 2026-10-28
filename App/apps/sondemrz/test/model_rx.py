#!/usr/bin/env python3
"""Model the MRZ Flipper -> BK4829 audio -> PA4 -> demodulator path.

The channel matches the RS41 model: 2.38 kHz FSK deviation, the measured
3.3 kHz audio low-pass, a swept high-pass corner, and 19.2 kHz ADC sampling.
OldDemod mirrors the original integrate-and-dump MRZ receiver. Demod mirrors
the RS41-style adaptive hysteresis slicers and second-order DPLLs.
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flipper_mrz as F

FS_SIM = 96000
FS = 19200
BIAS = 2048
LSB_PER_HZ = 0.065
DEV = 2380.0

HEADER = int("10011001100110011001100110011001100110101010", 2)
HEADER_MASK = (1 << 44) - 1
STEP = 65536 * 2400 // FS
PLL_CTR = 40960
PLL_SHIFT = 2
FREQ_SHIFT = 8
STEP_MAX = 410
SYNC_ERR = 2
NUMS = (2, 4)


def popcount(value):
    count = 0
    while value:
        value &= value - 1
        count += 1
    return count


def biquad_lp(x, fc):
    w = math.tan(math.pi * fc / FS_SIM)
    q = 1 / math.sqrt(2)
    n = 1 / (1 + w / q + w * w)
    b0 = w * w * n
    b1 = 2 * b0
    a1 = 2 * (w * w - 1) * n
    a2 = (1 - w / q + w * w) * n
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
    runs = []
    with open(path, encoding="ascii") as source:
        for line in source:
            if line.startswith("RAW_Data:"):
                runs += [int(value) for value in line.split()[1:]]
    signal, elapsed = [], 0.0
    for run in runs:
        count = int(round((elapsed + abs(run)) * FS_SIM / 1e6)) - int(round(elapsed * FS_SIM / 1e6))
        elapsed += abs(run)
        signal += [DEV if run > 0 else -DEV] * count
    return signal


def channel(signal, hp=30.0, noise=0.0, ppm=0.0, seed=1, gap_ms=200):
    rnd = random.Random(seed)
    gap = int(gap_ms * 1e-3 * FS_SIM)
    x = [rnd.gauss(0, 6000) for _ in range(gap)]
    x += [value + rnd.gauss(0, noise) for value in signal]
    x += [rnd.gauss(0, 6000) for _ in range(gap)]
    x = rc_hp(biquad_lp(biquad_lp(x, 3300), 3300), hp)
    stride = FS_SIM / FS * (1 + ppm * 1e-6)
    out, pos = [], 0.0
    while pos < len(x) - 1:
        index = int(pos)
        frac = pos - index
        value = x[index] * (1 - frac) + x[index + 1] * frac
        value = BIAS + LSB_PER_HZ * value + rnd.gauss(0, 1)
        out.append(min(4095, max(0, int(round(value)))))
        pos += stride
    return out


def crc16(payload):
    crc = 0xFFFF
    for byte in payload:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def frame_ok(frame):
    latlon = frame[30:32] == b"\xFF\xFF"
    length = 47 if latlon else 50
    crc_len = 42 if latlon else 45
    # Byte 0 belongs to the sync already consumed by the compact decoder.
    if frame[1:3] != b"\xBF\x35":
        return False
    return crc16(frame[3:3 + crc_len]) == int.from_bytes(frame[3 + crc_len:5 + crc_len], "little")


class Slicer:
    def __init__(self, num, center=PLL_CTR):
        self.num = num
        self.center = center
        self.lev = 0
        self.phase = 0
        self.step = STEP
        self.shift = 0
        self.rearm()

    def rearm(self):
        self.state = 0
        self.inv = 0
        self.half = 0
        self.nbit = 0
        self.need = 400
        self.frame = bytearray(50)

    def chip(self, chip):
        if self.state == 0:
            self.shift = ((self.shift << 1) | chip) & HEADER_MASK
            if self.nbit < 44:
                self.nbit += 1
                return False
            error = popcount(self.shift ^ HEADER)
            inverse_error = popcount((self.shift ^ HEADER) ^ HEADER_MASK)
            if error <= SYNC_ERR:
                self.inv = 0
            elif inverse_error <= SYNC_ERR:
                self.inv = 1
            else:
                return False
            self.byte = 0x2F
            self.nbit = 14
            self.half = 0
            self.state = 1
            return False
        chip ^= self.inv
        if not self.half:
            self.half = chip + 1
            return False
        first = self.half - 1
        self.half = 0
        if first == chip:
            self.rearm()
            return False
        self.byte = ((self.byte << 1) | first) & 0xFFFF
        self.nbit += 1
        if not self.nbit & 7:
            self.frame[(self.nbit - 1) >> 3] = self.byte & 0xFF
        if self.nbit == 256 and self.frame[30:32] == b"\xFF\xFF":
            self.need = 376
        if self.nbit < self.need:
            return False
        self.state = 2
        return True


class Demod:
    def __init__(self, center=PLL_CTR):
        self.dc = -1
        self.amp = 0
        self.previous = 0
        self.slicers = [Slicer(num, center) for num in NUMS]

    def rearm(self):
        for slicer in self.slicers:
            slicer.rearm()

    def push(self, adc):
        xq = adc << 8
        if self.dc < 0:
            self.dc = xq
        self.dc += (xq - self.dc) >> 8
        y0 = xq - self.dc
        y = y0 + ((y0 - self.previous) >> 1)
        self.previous = y0
        amplitude = abs(y)
        if amplitude > self.amp:
            self.amp += (amplitude - self.amp) >> 2
        else:
            self.amp -= self.amp >> 8
        done = False
        for slicer in self.slicers:
            if slicer.state == 2:
                done = True
                continue
            threshold = self.amp >> (2 if slicer.num == 2 else 1)
            level = slicer.lev
            if y > threshold:
                level = 1
            elif y < -threshold:
                level = 0
            slicer.phase += slicer.step
            if level != slicer.lev:
                error = slicer.center - slicer.phase
                if error > 32768:
                    error -= 65536
                elif error < -32768:
                    error += 65536
                slicer.phase += error >> PLL_SHIFT
                slicer.step = min(max(slicer.step + (error >> FREQ_SHIFT), STEP - STEP_MAX), STEP + STEP_MAX)
                slicer.lev = level
            if slicer.phase >= 65536:
                slicer.phase -= 65536
                done |= slicer.chip(level)
        return done


class OldDemod:
    SPB = (FS << 8) // 2400

    def __init__(self, sync10=6):
        self.sync10 = sync10
        self.mean = -1
        self.phase = self.SPB // 2
        self.integral = 0
        self.previous = 0
        self.sign = 0
        self.chips = [0] * 64
        self.rearm()

    def rearm(self):
        self.state = 0
        self.inv = 0
        self.half = 0
        self.pos = 0
        self.fill = 0
        self.nbit = 0
        self.need = 400
        self.frame = bytearray(50)

    def emit(self, soft):
        if self.state == 0:
            self.chips[self.pos] = soft
            self.pos = (self.pos + 1) & 63
            self.fill = min(44, self.fill + 1)
            if self.fill < 44:
                return False
            score = energy = 0
            base = (self.pos - 44) & 63
            for index in range(44):
                value = self.chips[(base + index) & 63]
                expected = (HEADER >> (43 - index)) & 1
                energy += abs(value)
                score += value if expected else -value
            if energy <= 160 or abs(score) * 10 <= energy * self.sync10:
                return False
            self.inv = score < 0
            self.frame[0] = 0xAA
            self.frame[1] = 0xBC
            self.nbit = 14
            self.half = 0
            self.state = 1
            return False
        value = -soft if self.inv else soft
        if not self.half:
            self.first = value
            self.half = 1
            return False
        self.half = 0
        bit = self.first > value
        if not self.nbit & 7:
            self.frame[self.nbit >> 3] = 0
        if bit:
            self.frame[self.nbit >> 3] |= 1 << (7 - (self.nbit & 7))
        self.nbit += 1
        if self.nbit == 256 and self.frame[30:32] == b"\xFF\xFF":
            self.need = 376
        if self.nbit < self.need:
            return False
        self.state = 2
        return True

    def push(self, sample):
        if self.state == 2:
            return True
        xq = sample << 8
        if self.mean < 0:
            self.mean = xq
        error = xq - self.mean
        if self.state != 1:
            self.mean += (error + 32) >> 6
        sign = error > 0
        if sign != self.sign:
            a, b = abs(self.previous), abs(error)
            phase_error = ((a << 8) // (a + b + 1)) - 128 - self.phase
            if phase_error < -(self.SPB // 2):
                phase_error += self.SPB
            self.phase += phase_error >> 3
            self.sign = sign
        self.previous = error
        if self.phase < 256:
            part = (error * max(self.phase, 0)) >> 8
            done = self.emit(max(-32767, min(32767, (self.integral + part) >> 8)))
            self.integral = error - part
            self.phase += self.SPB - 256
            return done
        self.integral += error
        self.phase -= 256
        return False


def decoded(adc, demod):
    count = 0
    stamps = set()
    for sample in adc:
        if not demod.push(sample):
            continue
        slicers = demod.slicers if isinstance(demod, Demod) else (demod,)
        accepted = False
        for slicer in slicers:
            if slicer.state != 2:
                continue
            if frame_ok(slicer.frame):
                count += 1
                stamps.add(bytes(slicer.frame[4:7]))
                demod.rearm()
                accepted = True
                break
            slicer.rearm()
        if accepted:
            continue
    return count, len(stamps)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    hps = (30, 300, 700, 1000, 1500)
    print("Flipper files, CRC-valid frames / distinct UTC updates")
    print("%-15s %-5s %s" % ("file", "demod", "  ".join("hp%-5d" % hp for hp in hps)))
    for name in ("ecef_valid", "ecef_inverted", "ecef_badcrc",
                 "latlon_valid", "latlon_inverted", "latlon_badcrc"):
        wave = sub_wave(os.path.join(here, "flipper", "mrz_%s.sub" % name))
        for label, make in (("old", OldDemod), ("new", Demod)):
            row = [decoded(channel(wave, hp=hp, seed=hp), make()) for hp in hps]
            print("%-15s %-5s %s" % (name, label, "  ".join("%d/%d" % value for value in row)))
    print("\nDPLL center sweep at 1 kHz high-pass")
    wave = sub_wave(os.path.join(here, "flipper", "mrz_ecef_valid.sub"))
    for center in (32768, 40960, 49152):
        row = [decoded(channel(wave, hp=1000, ppm=ppm, seed=center + ppm), Demod(center))
               for ppm in (-30000, -10000, 0, 10000, 30000)]
        print("center %-5d %s" % (center, "  ".join("%d/%d" % value for value in row)))
    strict = [decoded(channel(wave, hp=1000, ppm=ppm, seed=90000 + ppm), OldDemod(9))
              for ppm in (-30000, -10000, 0, 10000, 30000)]
    print("old 90%% sync %s" % "  ".join("%d/%d" % value for value in strict))
    print("\nNoise/clock sweep at 1 kHz high-pass, normal and inverted")
    inverted = sub_wave(os.path.join(here, "flipper", "mrz_ecef_inverted.sub"))
    for noise in (0, 1000, 1500):
        values = []
        for ppm in (-30000, -10000, 0, 10000, 30000):
            for polarity, signal in enumerate((wave, inverted)):
                values.append(decoded(channel(signal, hp=1000, noise=noise, ppm=ppm,
                                              seed=noise + ppm + polarity), Demod()))
        print("noise %-4d %s" % (noise, "  ".join("%d/%d" % value for value in values)))
    samples = channel([], hp=1000, gap_ms=10000, seed=9)
    demod, syncs = Demod(), 0
    for sample in samples:
        before = [slicer.state for slicer in demod.slicers]
        done = demod.push(sample)
        syncs += sum(old != 1 and slicer.state == 1
                     for old, slicer in zip(before, demod.slicers))
        if done:
            for slicer in demod.slicers:
                if slicer.state == 2:
                    slicer.rearm()
    print("receiver noise alone, 20 s: %d false syncs" % syncs)


if __name__ == "__main__":
    main()
