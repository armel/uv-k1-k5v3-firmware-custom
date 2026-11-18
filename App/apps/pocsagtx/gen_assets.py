#!/usr/bin/env python3
"""POCSAG TX read-only assets: the screen texts and the multi-tap keymap.

The UI texts come first, each padded to a multiple of 4 bytes so draw() can read
them all in one asset_read into a word-aligned stack block (the same trick as
POCSAG RX / APRS TX). The multi-tap keymap is raw data read on demand.

   ./gen_assets.py pocsagtx_assets.bin pocsagtx_assets.h
"""
import os, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "POCSAG TX"

UI = [
    ("T_TITLE",   TITLE),
    ("T_RIC",     "RIC"),
    ("T_FUNC",    "Func"),
    ("T_BAUD",    "Baud"),
    ("T_MSG",     "Msg"),
    ("T_TX",      "TX"),
    ("T_DENIED",  "TX denied"),
    ("T_HINT",    "PTT TX  MENU edit  EXIT quit"),
    ("T_BACK",    "EXIT back"),
    ("T_MODE_UP", "ABC"),    # contiguous, stride 4: mode * 4
    ("T_MODE_LO", "abc"),
    ("T_MODE_NUM","123"),
]

a = Assets("POCSAGTX")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_TX_CHARS", 2)

# Multi-tap keymap, key 0..9, each entry padded to 5 bytes with NUL.
KEYS = [" ", ".,?!", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"]
a.raw("KEYMAP", b"".join(v.encode("ascii").ljust(5, b"\0") for v in KEYS))
a.const("KEYMAP_STRIDE", 5)

if __name__ == "__main__":
    a.main()
