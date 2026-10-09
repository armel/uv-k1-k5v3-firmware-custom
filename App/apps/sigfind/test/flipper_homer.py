#!/usr/bin/env python3
"""Flipper Zero test transmitter for the Sig Finder ELT detector: write
Sub-GHz RAW files (.sub) that play a homing signal on 433.650 MHz.

The Flipper (CC1101) cannot amplitude-modulate, but it can key the carrier on
and off: keying it at the audio rate is AM at 100 % with a rectangular tone,
the square-wave form RTCA DO-183 allows for a homer (duty 33..55 %). An AM
receiver demodulates it as a square wave at the tone frequency, which is what
the app's zero-crossing tracker counts. Preset OOK, RAW durations in us:
positive = carrier on, negative = off. Edges are placed on an absolute time
grid, so the sweep rate does not drift over the file.

Set the radio to AM on 433.650 MHz, then Send. Each file lasts SECONDS: the
app should show "ELT" within about 2 s for the homer files, never for the
negative ones (upward sweep, steady tone).

NEVER use these files on 121.5 / 243 MHz or 406.0-406.1 MHz (distress
frequencies); the Flipper cannot reach 121.5 / 243 MHz, but 406 MHz is inside
its 387..464 MHz band.

  flipper_homer.py [out_dir]     (default: ./flipper next to this script)

Check a file against the detector on the host before sending it:
  ./sweep_test flipper/homer_default.sub
"""
import os, sys

FREQ = 433650000
PRESET = "FuriHalSubGhzPresetOok650Async"
SECONDS = 20.0
DUTY = 0.5
PER_LINE = 512          # values per RAW_Data line, as the Flipper writes them

# name: (f_start Hz, f_end Hz, sweeps/s, law, expected to be detected)
FILES = {
    "homer_default":  (1570, 820, 2.42, "period", True),   # like homer121.py / a real beacon
    "homer_textbook": (1600, 300, 3.0,  "linear", True),   # full DO-183 range
    "homer_fast_min": (1000, 300, 4.0,  "linear", True),   # fastest rate, minimum 700 Hz span
    "homer_slow_min": (1600, 900, 2.0,  "linear", True),   # slowest rate, minimum 700 Hz span
    "neg_upward":     (300, 1600, 3.0,  "linear", False),  # sweeps up: not a homer
    "neg_tone_1k":    (1000, 1000, 3.0, "linear", False),  # steady tone: not a homer
}

FORBIDDEN = [(121.3e6, 121.7e6), (242.8e6, 243.2e6), (405.9e6, 406.2e6)]


def edges(f0, f1, rate, law, seconds=SECONDS, duty=DUTY):
    """Carrier on/off edge times (s): each tone cycle is on for duty x period,
    off for the rest; every sweep restarts at a cycle start (as homer121.py)."""
    T = 1.0 / rate
    out, t0 = [], 0.0
    while t0 < seconds:
        t = 0.0
        while t < T:
            x = t / T
            if law == "period":            # tone period grows linearly (RC ramp)
                p = 1.0 / f0 + (1.0 / f1 - 1.0 / f0) * x
            else:                          # frequency linear in time
                p = 1.0 / (f0 + (f1 - f0) * x)
            out.append(t0 + t)             # on
            out.append(t0 + min(t + duty * p, T))   # off
            t += p
        t0 += T
    out.append(seconds if out[-1] < seconds else out[-1])
    return out


def raw_durations(e):
    """Edge times -> signed us durations, rounded on the absolute grid."""
    us = [round(t * 1e6) for t in e]
    d = []
    for i in range(len(us) - 1):
        n = us[i + 1] - us[i]
        if n <= 0:
            continue
        on = i % 2 == 0
        if d and (d[-1] > 0) == on:        # merge equal levels (zero-length gaps)
            d[-1] += n if on else -n
        else:
            d.append(n if on else -n)
    return d


def write_sub(path, d, freq=FREQ):
    if any(lo <= freq <= hi for lo, hi in FORBIDDEN):
        sys.exit("refused: %g MHz is a distress frequency" % (freq / 1e6))
    lines = ["Filetype: Flipper SubGhz RAW File", "Version: 1",
             "Frequency: %d" % freq, "Preset: %s" % PRESET, "Protocol: RAW"]
    for i in range(0, len(d), PER_LINE):
        lines.append("RAW_Data: " + " ".join(str(x) for x in d[i:i + PER_LINE]))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "flipper")
    os.makedirs(out, exist_ok=True)
    for name, (f0, f1, rate, law, det) in FILES.items():
        d = raw_durations(edges(f0, f1, rate, law))
        p = os.path.join(out, name + ".sub")
        write_sub(p, d)
        print("%-40s %5d > %4d Hz, %.2f sweeps/s, %-6s %6d values, %s"
              % (p, f0, f1, rate, law, len(d), "ELT expected" if det else "must NOT show ELT"))
