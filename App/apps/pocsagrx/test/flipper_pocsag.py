#!/usr/bin/env python3
"""Flipper Zero test transmitter for the POCSAG RX app: write Sub-GHz RAW files
(.sub) that play a POCSAG transmission on 433.650 MHz, the bench
channel of the APRS RX and ATIS RX files. Tune the radio to 433.650 MHz FM,
launch POCSAG RX, press Send.

POCSAG keys the carrier frequency directly with the bits (NRZ, a 1 on the
lower frequency), which is what the Flipper's CC1101 does (preset
2FSKDev238Async, +/-2.38 kHz against +/-4.5 kHz for a pager transmitter): no
tone to imitate, the RAW durations are the runs of equal bits. Each run is
rounded from exact bit-edge times so the timing error never accumulates.

  flipper_pocsag.py <out_dir> [--freq 433650000] [--baud 1200] [--names alpha,two]
    -> pocsag_alpha.sub, _numeric, _two, _long, _same, _inverted, _biterr, _gap
       (1200 bauds); pocsag512_alpha.sub, ... at 512 bauds, pocsag2400_... at
       2400 (not decoded: the app shows the preamble's rate only)
One transmission per file (about 1.5 s at 1200 bauds, 3.5 s at 512).
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pocsag

PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
LEAD_US = 100000            # unmodulated carrier before the preamble
TAIL_US = 10000
PER_LINE = 512              # values per RAW_Data line, as the Flipper writes them


def raw_durations(bits, baud=1200, lead_us=LEAD_US, tail_us=TAIL_US):
    """Signed RAW durations: positive = the higher frequency (a 0), negative =
    the lower one (a 1). The lead and the tail are sent as zeros."""
    tb = 1e6 / baud
    lv = [0] + list(bits) + [0]
    t = [0.0, float(lead_us)]
    for i in range(len(bits)):
        t.append(lead_us + (i + 1) * tb)
    t.append(t[-1] + tail_us)
    out, start, cur = [], 0, lv[0]
    for i in range(1, len(lv) + 1):
        if i == len(lv) or lv[i] != cur:
            d = round(t[i]) - round(t[start])
            out.append(-d if cur else d)
            if i < len(lv):
                start, cur = i, lv[i]
    return out


def write_sub(path, bits, freq, baud):
    d = raw_durations(bits, baud)
    lines = ["Filetype: Flipper SubGhz RAW File", "Version: 1",
             "Frequency: %d" % freq, "Preset: %s" % PRESET, "Protocol: RAW"]
    for i in range(0, len(d), PER_LINE):
        lines.append("RAW_Data: " + " ".join(str(x) for x in d[i:i + PER_LINE]))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return d


def test_bits():
    """name -> (bits, pages the app must show)."""
    out = {n: (pocsag.bits(p), p) for n, p in pocsag.TEST_PAGES.items()}
    b, p = out["alpha"]
    out["inverted"] = ([1 - x for x in b], p)       # the other discriminator polarity
    e = list(b)                                     # one wrong bit in every other codeword
    for k in range(pocsag.PREAMBLE_BITS + 32 + 7, len(e), 64):
        e[k] ^= 1
    out["biterr"] = (e, p)
    # three wrong bits in the third message codeword of the long page: it
    # cannot be corrected, by any slicer, and the characters it held (bits 40
    # to 59 of the text: characters 5 to 8) are shown as '?'
    b, p = out["long"]
    e = list(b)
    words = pocsag.batches(p)
    k = [i for i, cw in enumerate(words) if cw != pocsag.SYNC and cw != pocsag.IDLE and cw >> 31][2]
    for d in (3, 11, 20):
        e[pocsag.PREAMBLE_BITS + 32 * k + d] ^= 1
    r, f, t = p[0]
    out["gap"] = (e, [(r, f, t[:5] + "????" + t[9:])])
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default=".")
    ap.add_argument("--freq", type=int, default=433650000)
    ap.add_argument("--baud", type=int, default=1200, choices=(512, 1200, 2400))
    ap.add_argument("--names", default="", help="comma-separated test names (default: all)")
    a = ap.parse_args()
    if not 433050000 <= a.freq <= 434790000:
        sys.exit("refusing %d Hz: keep the bench files in the 433 MHz SRD band" % a.freq)
    os.makedirs(a.out, exist_ok=True)
    names = [n for n in a.names.split(",") if n]
    prefix = "pocsag" if a.baud == 1200 else "pocsag%d" % a.baud
    for name, (b, pages) in test_bits().items():
        if names and name not in names:
            continue
        p = os.path.join(a.out, "%s_%s.sub" % (prefix, name))
        d = write_sub(p, b, a.freq, a.baud)
        print("%-28s %5.0f ms %4d runs  %s" % (p, sum(abs(x) for x in d) / 1000, len(d),
                                              " | ".join("%d: %s" % (r, t) for r, f, t in pages)))
