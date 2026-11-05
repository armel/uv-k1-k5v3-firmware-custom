#!/usr/bin/env python3
"""APRS DIGI read-only texts and Bell 202 correlator tables."""
import math
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

FS = 9600


def cos_table(freq):
    period = FS // math.gcd(FS, freq)
    return [int(round(127 * math.cos(2 * math.pi * freq * n / FS)))
            for n in range(period)]


a = Assets("APRSDIGI")
a.text("T_TITLE", "APRS DIGI")
a.text("T_WAIT", "WAIT")
a.text("T_HOLD", "HOLD")
a.text("T_TX", "TX")
a.text("T_DENIED", "DENIED")
a.text("T_NOCALL", "NO CALL")
a.text("T_RX", "RX  ")
a.text("T_DIGI", "DIGI ")
a.text("T_DUPE", "DUP ")
a.text("T_DROP", "DROP ")
a.text("T_DBM", "dBm")
a.const("T_TITLE_CHARS", len("APRS DIGI"))

# dem_t prefix: DC, filter/correlator words, peaks, phases and product ring.
# The cosine tables follow so one asset read initializes the whole front end.
a.u32("DEMOD_INIT", [2048 << 4] + [0] * 8 + [64, 64] + [0] * 18)
a.i8("COS1200", cos_table(1200))
a.i8("COS2200", cos_table(2200))

if __name__ == "__main__":
    a.main()
