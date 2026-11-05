#!/usr/bin/env python3
"""Host model of the AIS RX receive chain and of the integer demodulator in
../aisrx_app.c (keep both in step).

Chain: transmitter -> discriminator output in Hz -> radio audio path ->
12-bit ADC on PA4 around 2048 at 38.4 kHz (optional clock error) -> Demod.

Transmitters:
  gmsk     AIS: 9600 bit/s, Gaussian BT 0.4, +/-2.4 kHz
  flipper  the custom CC1101 preset of flipper_ais.py: GFSK BT 0.5, 9596 bd,
           +/-2.38 kHz, input sampled by the CC1101 every 13 us (async mode)
  stock    the Flipper stock 2FSKDev238Async preset: square 2-FSK, +/-2.38
           kHz, input sampled every 26 us

Radio audio path (AF bypass, REG_47 = 0x6942), as the other RAW decoders'
models: an unknown high-pass corner (POCSAG RX points near 1 kHz) and a
low-pass modelled as a 4th-order Butterworth at `lp`. Without the bypass the
path stops near 3.3 kHz (Sonde Lab); with it Sonde M20 decodes 9600 chip/s,
so the bypass reaches further: AIS RX receives real AIS through it
(2026-10-20). In the model 9600 bit/s needs that low-pass at 4 kHz or above.
Discriminator gain 0.065 LSB/Hz (measured on a K1, EPIRB 406 lab); full-scale
noise around the burst.

Demod (per ADC sample), as aisrx_app.c:
  DC removal (Q8, 1/256)
  equaliser y = 7 x[n-2] - 3 (x[n] + x[n-4]): x1 DC, x2.8 at 2.4 kHz, x7 at
  4.8 kHz, restores the single-bit runs the low-pass flattens
  peak of |y| (attack 1/4, decay 1/256)
  two lanes: hysteresis slicer (level flips beyond +/- num/8 of the peak,
  num = 1, 3, holds in between whatever the high-pass droop), DPLL pulled by
  level changes (target 3/4, gain 1/4), a bit per wrap, NRZI, HDLC flag after
  12 training bits, de-stuffing, CRC residue 0xF0B8; the first valid frame
  stops both lanes.

  python3 -B model_rx.py        sweep table, bad CRC, noise
"""

from __future__ import annotations

import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ais

BAUD = 9600
FS_ADC = 38400
FS_SIM = 192000
BIAS = 2048
LSB_PER_HZ = 0.065

PHASE_STEP, PHASE_CENTER, PHASE_GAIN = 16384, 49152, 2
NUMS = (1, 3)
RAW_BITS = 544
M32 = 0xFFFFFFFF


# ------------------------------------------------------------ transmitter ----

def gaussian(src: list[float], bt: float, baud: float) -> list[float]:
    sps = FS_SIM / baud
    sigma = math.sqrt(math.log(2.0)) / (2.0 * math.pi * bt) * sps
    radius = int(math.ceil(3.5 * sigma))
    taps = [math.exp(-0.5 * (k / sigma) ** 2) for k in range(-radius, radius + 1)]
    total = sum(taps)
    taps = [t / total for t in taps]
    pad = [src[0]] * radius + src + [src[-1]] * radius
    return [sum(t * pad[i + k] for k, t in enumerate(taps)) for i in range(len(src))]


def async_input(levels: list[int], grid_us: float, offset: float) -> list[float]:
    """CC1101 asynchronous TX: the data pin is sampled every grid_us."""
    count = int(len(levels) * FS_SIM / BAUD)
    out = []
    for i in range(count):
        t_us = i * 1e6 / FS_SIM
        tq = math.floor((t_us - offset) / grid_us) * grid_us + offset
        k = min(max(int(tq * BAUD / 1e6), 0), len(levels) - 1)
        out.append(1.0 if levels[k] else -1.0)
    return out


def transmitter(payload: bytes, source: str = "gmsk", invert: bool = False,
                bad_crc: bool = False, repeats: int = 1, seed: int = 1) -> list[float]:
    bits = []
    for _ in range(repeats):
        bits += ais.wire_bits(payload, bad_crc)
    levels = ais.nrzi_levels(bits)
    if invert:
        levels = [1 - level for level in levels]
    offset = random.Random(seed).random()
    if source == "gmsk":
        sps = FS_SIM // BAUD
        src = [1.0 if level else -1.0 for level in levels for _ in range(sps)]
        return [2400.0 * v for v in gaussian(src, 0.4, BAUD)]
    if source == "flipper":
        grid = 1e6 / (9596 * 8)
        return [2380.0 * v for v in gaussian(async_input(levels, grid, offset * grid),
                                             0.5, 9596)]
    if source == "stock":
        grid = 1e6 / (4798 * 8)
        return [2380.0 * v for v in async_input(levels, grid, offset * grid)]
    raise ValueError(source)


# ---------------------------------------------------------------- channel ----

def biquad_lp(values: list[float], cutoff: float) -> list[float]:
    w = math.tan(math.pi * cutoff / FS_SIM)
    q = 1.0 / math.sqrt(2.0)
    norm = 1.0 / (1.0 + w / q + w * w)
    b0 = w * w * norm
    b1 = 2.0 * b0
    a1 = 2.0 * (w * w - 1.0) * norm
    a2 = (1.0 - w / q + w * w) * norm
    x1 = x2 = y1 = y2 = 0.0
    out = []
    for value in values:
        result = b0 * value + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, value, y1, result
        out.append(result)
    return out


def rc_hp(values: list[float], cutoff: float) -> list[float]:
    alpha = math.exp(-2.0 * math.pi * cutoff / FS_SIM)
    out = []
    state = previous = 0.0
    for value in values:
        state = alpha * (state + value - previous)
        previous = value
        out.append(state)
    return out


def channel(signal: list[float], lp: float = 5000.0, hp: float = 1000.0,
            noise: float = 0.0, offset: float = 0.0, ppm: float = 0.0,
            phase: float = 0.0, seed: int = 1, gap_ms: float = 30.0) -> list[int]:
    rnd = random.Random(seed)
    gap = int(gap_ms * 1e-3 * FS_SIM)
    values = [rnd.gauss(0.0, 6000.0) for _ in range(gap)]
    values += [v + offset + rnd.gauss(0.0, noise) for v in signal]
    values += [rnd.gauss(0.0, 6000.0) for _ in range(gap)]
    values = rc_hp(biquad_lp(biquad_lp(values, lp), lp), hp)
    step = FS_SIM / FS_ADC * (1.0 + ppm * 1e-6)
    position = phase * step
    samples = []
    while position < len(values) - 1:
        index = int(position)
        fraction = position - index
        value = values[index] * (1.0 - fraction) + values[index + 1] * fraction
        adc = round(BIAS + LSB_PER_HZ * value + rnd.gauss(0.0, 1.0))
        samples.append(min(4095, max(0, adc)))
        position += step
    return samples


# ------------------------------------------------------ integer demodulator --

class Lane:
    def __init__(self, num: int):
        self.num = num
        self.phase = 0
        self.history = 0
        self.level = 0
        self.sampled = 0
        self.collecting = False
        self.raw: list[int] = []


class Demodulator:
    """Integer receiver kept line-for-line compatible with the radio app."""

    def __init__(self):
        self.dc = BIAS << 8
        self.peak = 0
        self.x = [0] * 5
        self.lanes = [Lane(num) for num in NUMS]
        self.frames: list[bytes] = []
        self.sync = 0
        self.ok = 0

    def sample(self, adc: int) -> None:
        x = self.x
        self.dc += ((adc << 8) - self.dc) >> 8
        x[4], x[3], x[2], x[1] = x[3], x[2], x[1], x[0]
        x[0] = adc - (self.dc >> 8)
        y = 7 * x[2] - 3 * (x[0] + x[4])
        a = abs(y)
        if a > self.peak:
            self.peak += (a - self.peak) >> 2
        else:
            self.peak -= self.peak >> 8
        for lane in self.lanes:
            th = (self.peak * lane.num) >> 3
            level = 1 if y > th else 0 if y < -th else lane.level
            lane.phase += PHASE_STEP
            if level != lane.level:
                lane.phase += (PHASE_CENTER - lane.phase) >> PHASE_GAIN
            lane.level = level
            if lane.phase >= 65536:
                lane.phase -= 65536
                self.bit(lane, 1 if level == lane.sampled else 0)
                lane.sampled = level

    def busy(self) -> bool:
        return any(lane.collecting for lane in self.lanes)

    def bit(self, lane: Lane, bit: int) -> None:
        lane.history = ((lane.history >> 1) | (bit << 31)) & M32
        if lane.collecting:
            if len(lane.raw) >= RAW_BITS:
                lane.collecting = False
            else:
                lane.raw.append(bit)
        if lane.history >> 24 != ais.FLAG:
            return
        if lane.collecting:
            lane.collecting = False
            if len(lane.raw) > 8 and self.finish(lane.raw[:-8]):
                for other in self.lanes:
                    other.collecting = False
        elif lane.history & 0xFFFFF000 in (0x7EAAA000, 0x7E555000):
            if not self.busy():
                self.sync += 1
            lane.raw = []
            lane.collecting = True

    def finish(self, raw: list[int]) -> bool:
        bits = []
        ones = 0
        for bit in raw:
            if bit:
                ones += 1
                if ones > 5:
                    return False
            else:
                if ones == 5:
                    ones = 0
                    continue
                ones = 0
            bits.append(bit)
        if len(bits) & 7 or len(bits) < 24 or len(bits) > 57 * 8:
            return False
        frame = bytearray(len(bits) // 8)
        for index, bit in enumerate(bits):
            frame[index // 8] |= bit << (index & 7)
        if not ais.valid_fcs(frame):
            return False
        self.ok += 1
        self.frames.append(bytes(frame[:-2]))
        return True

    def run(self, samples: list[int]) -> list[bytes]:
        for sample in samples:
            self.sample(sample)
        return self.frames


def decode(payload: bytes, source: str = "gmsk", **kwargs) -> list[bytes]:
    return Demodulator().run(channel(transmitter(payload, source=source), **kwargs))


def main() -> None:
    payloads = ais.test_payloads()
    names = ("classa", "static5", "classb19", "static24")
    print("source  lowpass | decoded per high-pass corner 300 / 1000 / 1500 Hz"
          " (noise 150 Hz, clock -1..+1 %, 4 phases, both polarities)")
    failures = 0
    for source in ("gmsk", "flipper", "stock"):
        for lp in (3300, 4000, 5000, 8000):
            row = []
            for hp in (300, 1000, 1500):
                ok = 0
                for k, name in enumerate(names):
                    wave = transmitter(payloads[name], source, invert=k & 1, seed=k)
                    got = Demodulator().run(channel(
                        wave, lp=lp, hp=hp, noise=150, phase=k * 0.25,
                        ppm=(-10000, 0, 10000, 5000)[k], seed=k + 1))
                    ok += got == [payloads[name]]
                row.append(ok)
                if lp >= 5000:
                    failures += len(names) - ok
            print("%-7s %5d   | %s" % (source, lp,
                  "   ".join("%d/%d" % (ok, len(names)) for ok in row)))

    bad = Demodulator()
    bad.run(channel(transmitter(payloads["classa"], bad_crc=True)))
    if bad.frames or bad.sync != 1:
        failures += 1
        print("bad CRC: unexpected result", bad.frames, bad.sync)
    else:
        print("bad CRC: sync 1, crc 0, rejected")

    noise = Demodulator()
    noise.run(channel([], lp=8000, hp=300, gap_ms=10000, seed=9))
    if noise.frames:
        failures += 1
        print("noise: false frame")
    else:
        print("noise: 20 s, no frame (sync %d)" % noise.sync)

    if failures:
        raise SystemExit("AIS RX model: %d failed cases at 5 kHz or above" % failures)
    print("AIS RX model: every case decodes with a low-pass at 5 kHz or above")


if __name__ == "__main__":
    main()
