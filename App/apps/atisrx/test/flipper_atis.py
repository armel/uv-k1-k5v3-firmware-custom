#!/usr/bin/env python3
"""Flipper Zero test transmitter for the ATIS RX app: write Sub-GHz RAW files
(.sub) that play an ATIS signal sequence (1200 bauds, 1300 / 2100 Hz) on
433.650 MHz, the bench channel of the APRS RX and EPIRB 406 files. The Flipper
cannot reach the marine VHF band (156 MHz), and must not: tune the radio to
433.650 MHz FM, launch ATIS RX, press Send.

As in aprsrx/test/flipper_aprs.py: the CC1101 only switches between two
frequencies (preset 2FSKDev238Async, +/-2.38 kHz), so each tone is sent as a
square wave, phase-continuous across bit boundaries; the receiver's audio
low-pass leaves mostly the fundamental. The RAW durations are the square wave's
half-periods, rounded from exact edge times.

  flipper_atis.py <out_dir> [--freq 433650000]
    -> atis_pc8075.sub, _fm1234, _dc5513, _mmsi, _badecc (see atis.TEST_IDS)
One sequence per file (0.4 s): press Send once per reception.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atis

PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
BAUD = 1200
F_Y, F_B = 1300.0, 2100.0   # Y (1) is the lower tone, B (0) the higher one
LEAD_US = 100000            # unmodulated carrier first, as the end of a voice transmission
TAIL_US = 10000
PER_LINE = 512              # values per RAW_Data line, as the Flipper writes them


def tones(bits):
    return [F_Y if b else F_B for b in bits]


def edges(tn, lead_us=LEAD_US, tail_us=TAIL_US):
    """Exact edge times (us) of the square wave; level is high at t=0.
    Returns (edge_times, end_time)."""
    t0 = float(lead_us)
    ph = 0.0                             # tone phase in cycles (high while frac < 0.5)
    tb = 1e6 / BAUD
    ed = []
    for i, f in enumerate(tn):
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
    return ed, t0 + len(tn) * tb + tail_us


def raw_durations(ed, end):
    """Signed RAW durations: positive = high tone, negative = low tone."""
    pts = [0] + [round(t) for t in ed] + [round(end)]
    out, lv = [], 1
    for a, b in zip(pts, pts[1:]):
        out.append((b - a) if lv else -(b - a))
        lv ^= 1
    return out


def write_sub(path, bits, freq):
    d = raw_durations(*edges(tones(bits)))
    lines = ["Filetype: Flipper SubGhz RAW File", "Version: 1",
             "Frequency: %d" % freq, "Preset: %s" % PRESET, "Protocol: RAW"]
    for i in range(0, len(d), PER_LINE):
        lines.append("RAW_Data: " + " ".join(str(x) for x in d[i:i + PER_LINE]))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return d


def test_bits():
    """name -> (bits, identity the app must show, or None)."""
    out = {n: (atis.bits(i), i) for n, i in atis.TEST_IDS.items()}
    out["badecc"] = (atis.bits(atis.TEST_IDS["fm1234"], bad_ecc=True), None)
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default=".")
    ap.add_argument("--freq", type=int, default=433650000)
    a = ap.parse_args()
    if not 433050000 <= a.freq <= 434790000:
        sys.exit("refusing %d Hz: keep the bench files in the 433 MHz SRD band "
                 "(the marine band is live, and out of the Flipper's reach)" % a.freq)
    os.makedirs(a.out, exist_ok=True)
    for name, (b, ident) in test_bits().items():
        p = os.path.join(a.out, "atis_%s.sub" % name)
        d = write_sub(p, b, a.freq)
        shown = "(bad error check: nothing shown)" if ident is None else \
            "%s %s" % (ident, atis.to_call(ident) or "")
        print("%-24s %4.0f ms %5d edges  %s" % (p, sum(abs(x) for x in d) / 1000, len(d), shown))
