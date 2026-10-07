#!/usr/bin/env python3
"""Flipper Zero test transmitter for EPIRB 406: write Sub-GHz RAW files (.sub)
that play a first-generation 406 frame on 433.650 MHz (433 MHz SRD band).

The Flipper cannot phase-modulate, so each biphase-L half-bit is sent as one
2-FSK tone (preset 2FSKDev238Async, +/-2.38 kHz, RAW durations of 1250 us).
The receiver's discriminator then outputs the biphase waveform itself instead
of the pulses of a real beacon; the app's leaky integrator still tracks its
sign (checked with a pure-Python model of the receive chain and dec406, down to
~2.5 kHz rms of discriminator noise and with +/-4 kHz carrier offset).

NEVER transmit these files on 406.0-406.1 MHz (distress band).

  flipper406.py <out_dir>    -> epirb406_long.sub, _selftest, _short, _bch_err,
                                epirb406_eltdt.sub, _eltdt_selftest
One burst per file: press Send once, wait at least 2 s before the next one
(the app waits for the carrier to drop before re-arming).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from frame406 import build

FREQ = 433650000
PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
HALF_US = 1250          # 400 bps biphase: 800 half-bits per second
LEAD_US = 160000        # unmodulated carrier before the bit sync, as a beacon
TAIL_US = 20000         # carrier kept after the last bit
PER_LINE = 512          # values per RAW_Data line, as the Flipper writes them

FRAMES = {
    "long":     build(),
    "selftest": build(selftest=True),
    "short":    build(short=True),
    "bch_err":  build(flip=50),       # BCH-1 must show ERR
    "eltdt":    build(eltdt=True),    # ELT(DT) location protocol
    "eltdt_selftest": build(eltdt=True, selftest=True),
}

def raw_durations(frame_hex):
    """Signed durations (us): positive = high tone, negative = low tone."""
    n = len(frame_hex) * 4
    v = int(frame_hex, 16)
    levels = []
    for i in range(n):
        levels += [1, 0] if (v >> (n - 1 - i)) & 1 else [0, 1]
    segs = [[1, LEAD_US]]                 # the lead tone merges with the first "1" half
    for lv in levels:
        if segs[-1][0] == lv: segs[-1][1] += HALF_US
        else: segs.append([lv, HALF_US])
    segs[-1][1] += TAIL_US
    return [d if lv else -d for lv, d in segs]

def write_sub(path, frame_hex):
    d = raw_durations(frame_hex)
    lines = ["Filetype: Flipper SubGhz RAW File", "Version: 1",
             "Frequency: %d" % FREQ, "Preset: %s" % PRESET, "Protocol: RAW"]
    for i in range(0, len(d), PER_LINE):
        lines.append("RAW_Data: " + " ".join(str(x) for x in d[i:i + PER_LINE]))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, fr in FRAMES.items():
        p = os.path.join(out, "epirb406_%s.sub" % name)
        write_sub(p, fr)
        print(p, fr)
