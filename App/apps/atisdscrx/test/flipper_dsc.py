#!/usr/bin/env python3
"""Flipper Zero test transmitter for the ATIS DSC RX app: write Sub-GHz RAW
files (.sub) that play a DSC call (1200 bauds, 1300 / 2100 Hz) on 433.650 MHz,
the bench channel of the ATIS RX and APRS RX files. The Flipper cannot reach
marine channel 70 (156.525 MHz), and must not transmit a DSC call there: a
false distress alert is an offence. Tune the radio to 433.650 MHz FM, launch
ATIS DSC RX, press Send.

The square-wave FSK and the .sub format are ATIS RX's
(atisrx/test/flipper_atis.py). One call per file (0.4 to 0.6 s).

  flipper_dsc.py <out_dir> [--freq 433650000]
    -> dsc_distress.sub, _dist_nopos, _dist_ack, _urgency, _safety, _routine,
       _test, _badecc (see dsc.TEST_CALLS)
"""
import argparse, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "atisrx", "test"))
sys.dont_write_bytecode = True
import dsc
from flipper_atis import write_sub          # ATIS RX's .sub writer


def test_bits():
    """name -> bits, as model_dsc.test_bits()."""
    out = {n: dsc.bits(c) for n, c in dsc.TEST_CALLS.items()}
    out["badecc"] = dsc.bits(dsc.TEST_CALLS["routine"], bad_ecc=True)
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default=".")
    ap.add_argument("--freq", type=int, default=433650000)
    a = ap.parse_args()
    if not 433050000 <= a.freq <= 434790000:
        sys.exit("refusing %d Hz: keep the bench files in the 433 MHz SRD band "
                 "(a DSC call on the marine band is a real alert)" % a.freq)
    os.makedirs(a.out, exist_ok=True)
    for name, b in test_bits().items():
        p = os.path.join(a.out, "dsc_%s.sub" % name)
        d = write_sub(p, b, a.freq)
        print("%-26s %4.0f ms %5d edges" % (p, sum(abs(x) for x in d) / 1000, len(d)))
