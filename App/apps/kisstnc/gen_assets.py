#!/usr/bin/env python3
"""KISS TNC read-only texts and Bell 202 correlator tables."""
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


a = Assets("KISSTNC")
a.text("T_TITLE", "KISS TNC")
a.text("T_IDLE", "ON")
a.text("T_HOLD", "WAIT")
a.text("T_TX", "TX")
a.text("T_DENIED", "NO TX")
a.text("T_NOUART", "NO UART")
a.text("T_SQL", "SQL")
a.text("T_DCD", "DCD")
a.text("T_FDX", "FDX")
a.text("T_LISTEN", "Listening")
a.const("T_TITLE_CHARS", len("KISS TNC"))

# dem_t prefix, then the cosine tables, so one asset read fills the front end.
a.u32("DEMOD_INIT", [2048 << 4] + [0] * 8 + [64, 64] + [0] * 18)
a.i8("COS1200", cos_table(1200))
a.i8("COS2200", cos_table(2200))

if __name__ == "__main__":
    a.main()
