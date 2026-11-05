#!/usr/bin/env python3
"""Flipper Zero test transmitter for the APRS RX app: write Sub-GHz RAW files
(.sub) that play an APRS frame (AX.25, Bell 202 AFSK 1200 bauds) on 433.650 MHz
(433 MHz SRD band, the same bench channel as EPIRB 406).

The Flipper's CC1101 cannot frequency-modulate with an audio tone, only switch
between two frequencies (preset 2FSKDev238Async, +/-2.38 kHz). So each AFSK
tone is sent as a square wave: the carrier toggles at twice the tone frequency
(1200 Hz mark, 2200 Hz space), phase-continuous across bit boundaries, and the
receiver's discriminator outputs that square wave; its audio low-pass leaves
mostly the fundamental. The RAW durations are the square wave's half-periods,
rounded from exact edge times so the timing error never accumulates.

  flipper_aprs.py <out_dir> [--call F4HWN] [--freq 433650000]
    -> aprs_pos.sub, _digi, _msg, _status, _long, _badfcs (see ax25.test_frames)
One frame per file: press Send once, wait ~1 s before the next one.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ax25 import test_frames, hdlc_bits, decode

PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
BAUD = 1200
MARK, SPACE = 1200.0, 2200.0
LEAD_US = 100000        # unmodulated carrier before the flags (squelch / RSSI trigger)
TAIL_US = 10000         # carrier kept after the last flag
PER_LINE = 512          # values per RAW_Data line, as the Flipper writes them


def nrzi_tones(bits):
    """Bell 202 NRZI: a 0 changes the tone, a 1 keeps it. Starts on mark."""
    f, out = MARK, []
    for b in bits:
        if not b:
            f = SPACE if f == MARK else MARK
        out.append(f)
    return out


def edges(tones, lead_us=LEAD_US, tail_us=TAIL_US):
    """Exact edge times (us) of the square wave; level is high at t=0.
    Returns (edge_times, end_time)."""
    t0 = float(lead_us)                  # lead carrier holds the high level
    ph = 0.0                             # tone phase in cycles (high while frac < 0.5)
    tb = 1e6 / BAUD
    ed = []
    for i, f in enumerate(tones):
        start, end = t0 + i * tb, t0 + (i + 1) * tb
        t = start
        while True:
            nxt = (int(ph * 2) + 1) / 2.0             # next half-cycle boundary
            dt = (nxt - ph) / f * 1e6
            if t + dt > end:
                ph += (end - t) * f / 1e6
                break
            t += dt
            ph = nxt
            ed.append(t)
    return ed, t0 + len(tones) * tb + tail_us


def raw_durations(ed, end):
    """Signed RAW durations: positive = high tone, negative = low tone."""
    pts = [0] + [round(t) for t in ed] + [round(end)]
    out, lv = [], 1
    for a, b in zip(pts, pts[1:]):
        out.append((b - a) if lv else -(b - a))
        lv ^= 1
    return out


def sub_durations(frame):
    ed, end = edges(nrzi_tones(hdlc_bits(frame)))
    return raw_durations(ed, end)


def write_sub(path, frame, freq):
    d = sub_durations(frame)
    lines = ["Filetype: Flipper SubGhz RAW File", "Version: 1",
             "Frequency: %d" % freq, "Preset: %s" % PRESET, "Protocol: RAW"]
    for i in range(0, len(d), PER_LINE):
        lines.append("RAW_Data: " + " ".join(str(x) for x in d[i:i + PER_LINE]))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return d


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default=".")
    ap.add_argument("--call", default="F4HWN")
    ap.add_argument("--freq", type=int, default=433650000)
    a = ap.parse_args()
    if 144000000 <= a.freq <= 146000000:
        sys.exit("refusing 2 m: the Flipper cannot reach it anyway, and 144.800 is live APRS")
    os.makedirs(a.out, exist_ok=True)
    for name, fr in test_frames(a.call).items():
        p = os.path.join(a.out, "aprs_%s.sub" % name)
        d = write_sub(p, fr, a.freq)
        print("%-28s %5.0f ms %5d edges  %s" % (p, sum(abs(x) for x in d) / 1000,
                                               len(d), decode(fr) or "(bad FCS)"))
