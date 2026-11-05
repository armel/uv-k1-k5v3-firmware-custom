#!/usr/bin/env python3
# ACARS RX read-only assets: UI text, status icons and MSK filter tables.
import math
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "ACARS RX"
WAIT = "WAIT"

UI = [
    ("T_TITLE", TITLE),
    ("T_WAIT", WAIT),
    ("T_ADC", "ADC "),
    ("T_SYNC", " SYN "),
    ("T_CRC", " CRC "),
    ("T_PAR", " PAR "),
    ("T_DBM", "dBm"),
    ("T_RX", " RX "),
    ("T_FIX", " FIX "),
    ("T_DEC", "DEC"),
    ("T_RAW", "RAW"),
    ("T_DOWN", "DOWN"),
    ("T_UP", "UP"),
    ("T_SUB", "SUB "),
    ("T_MFI", " MFI "),
    ("T_DATA", "DATA "),
    ("T_TERMINAL", "TERMINAL DATA"),
    ("T_ARINC622", "ARINC 622"),
    ("T_LINK", "LINK TEST"),
    ("T_MEDIA", "MEDIA ADVISORY"),
    ("T_MIAM", "MIAM"),
]

a = Assets("ACARSRX")
ui_size = 0
for name, text in UI:
    pad = -(len(text) + 1) % 4
    a.text(name, text + "\0" * pad)
    ui_size += len(text) + 1 + pad
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_WAIT_CHARS", len(WAIT))
a.const("UI_SIZE", ui_size)

# Speaker plus scroll marks, indexed by speaker/up/down bits as in APRS RX.
speaker = [0x1C, 0x1C, 0x3E, 0x7F, 0x00, 0x22, 0x1C, 0x41, 0x22, 0x1C]
up = [0x04, 0x06, 0x07, 0x06, 0x04]
down = [0x10, 0x30, 0x70, 0x30, 0x10]
tail = []
for index in range(8):
    tail += speaker if index & 1 else [0] * len(speaker)
    tail += [0] * 3
    tail += [(u if index & 2 else 0) | (d if index & 4 else 0)
             for u, d in zip(up, down)]
a.u8("BMP_TAIL", tail)
a.const("TAIL_W", len(tail) // 8)

# 64-step 1800 Hz mixer table and the 16 samples of sin(pi*n/16) used by
# ACARSDEC's one-1200-Hz-period half-sine matched filter. Keep them contiguous:
# app_main loads both into dem_t with one asset read.
a.i8("COSINE", [round(127 * math.cos(2 * math.pi * n / 64)) for n in range(64)])
a.u8("TAPS", [round(127 * math.sin(math.pi * n / 16)) for n in range(16)])

if __name__ == "__main__":
    a.main()
