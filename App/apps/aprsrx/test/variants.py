#!/usr/bin/env python3
"""Compare demodulator variants on the hard cases of the sweep (dev tool).
Run from a file (multiprocessing re-imports __main__)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from multiprocessing import Pool
from model_rx import *

FR = test_frames()
CASES = []
for mode in ("raw", "std"):
    for noise in (3000, 4000):
        CASES.append(("flipper", 0.0, mode, "pos", noise, 0, 0))
    CASES.append(("flipper", 0.0, mode, "long", 2000, 0, 10000))
    CASES.append(("flipper", 0.0, mode, "long", 2000, 0, -10000))
    for tw in (-9.0, -6.0, -3.0, 0.0, 3.0, 6.0, 9.0):
        CASES.append(("sine", tw, mode, "pos", 2500, 0, 0))
    CASES.append(("flipper", 0.0, mode, "badfcs", 1000, 0, 0))

VARIANTS = {
    "s1":       dict(slicers=((1, 1),)),
    "s3x2":     dict(slicers=((1, 2), (1, 1), (2, 1))),
    "s3x1.5":   dict(slicers=((2, 3), (1, 1), (3, 2))),
    "s5":       dict(slicers=((1, 2), (2, 3), (1, 1), (3, 2), (2, 1))),
    "s3x3":     dict(slicers=((1, 3), (1, 1), (3, 1))),
}
SEEDS = (1, 2, 3, 4)


def job(c):
    src, tw, mode, name, noise, off, ppm = c
    w = flipper_wave(FR[name]) if src == "flipper" else sine_wave(FR[name], twist_db=tw)
    row, mx = [], 0
    for seed in SEEDS:
        adc = channel(w, mode, noise, off, ppm, seed)
        want = [] if name == "badfcs" else [FR[name]]
        r = []
        for kw in VARIANTS.values():
            d = Demod(**kw)
            for s in adc:
                d.sample(s)
            r.append(d.frames == want)
            mx = max(mx, d.maxab)
        row.append(r)
    return c, [sum(r[i] for r in row) for i in range(len(VARIANTS))], mx


if __name__ == "__main__":
    with Pool() as p:
        res = p.map(job, CASES)
    names = list(VARIANTS)
    print("%-40s " % "case" + " ".join("%6s" % n for n in names))
    tot = [0] * len(names)
    mx = 0
    for c, r, m in res:
        print("%-40s " % (" ".join(str(x) for x in c)) + " ".join("%6d" % x for x in r))
        tot = [a + b for a, b in zip(tot, r)]
        mx = max(mx, m)
    print("%-40s " % ("TOTAL /%d" % (len(CASES) * len(SEEDS))) + " ".join("%6d" % x for x in tot))
    print("max(Mm*Ps, Ms*Pm) = %d  (x3 = %d, int32 max 2147483647)" % (mx, 3 * mx))
