#!/usr/bin/env python3
# Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
# Licensed under the Apache License, Version 2.0 (see the C sources).
# Scan 406 read-only assets: the screen texts.
#
# Each text is padded to a multiple of 4 bytes: draw() reads them all in one
# asset_read into a word-aligned stack block, so a text costs a 2-byte
# sp-relative add instead of a literal-pool load and a pool word.
#
#   ./gen_assets.py scan406_assets.bin scan406_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

TITLE = "SCAN 406"

UI = [
    ("T_TITLE", TITLE),
    ("T_AUTO",  "AUTO"),         # T_AUTO + mode * MODE_STRIDE
    ("T_WATCH", "WATCH"),
    ("T_TEST",  "TEST"),          # ends at x = 82: the battery percentage starts near 94
    ("T_NONE",  "NO BEACON YET"),
    ("T_MHZ",   "406.0"),        # T_MHZ + test * MHZ_STRIDE, then the kHz
    ("T_MHZT",  "433.6"),        # (TEST: + 19)
    ("T_AGO",   "S AGO"),
    ("T_BURSTS", "BURSTS GEN1 "),  # the counts row; "1G" would read "16" in the
    ("T_2G",    "GEN2 "),          # tiny font, whose G looks like a 6
    ("T_TST",   "TST "),         # after the kind: self-test frame sync
    ("T_KEYS",  "MENU HELP"),
]

a = Assets("SCAN406")
ui_size = 0
offs = {}
for name, s in UI:
    pad = -(len(s) + 1) % 4
    a.text(name, s + "\0" * pad)
    offs[name] = ui_size
    ui_size += len(s) + 1 + pad
a.const("MODE_STRIDE", offs["T_WATCH"] - offs["T_AUTO"])
# g.type - 1 (K_1G, K_CUT, K_ERR, K_OK, K_2G), fixed stride, part of the UI block
KIND = ["GEN1 ? ", "GEN1 CUT ", "GEN1 ERR ", "GEN1 OK ", "GEN2 "]
stride = -(-max(len(k) + 1 for k in KIND) // 4) * 4
a.table("T_KIND", KIND, stride)
ui_size += stride * len(KIND)
a.const("MHZ_STRIDE", offs["T_MHZT"] - offs["T_MHZ"])
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_KEYS_CHARS", len(dict(UI)["T_KEYS"]))
# MENU: the help page, NUL-separated lines in the tiny font (32 per row, 8 rows at most)
HELP = [                             # the keys only: the rest is in README.md
    "*     AUTO (EPIRB 406) / WATCH",
    "0     406 MHZ / TEST 433 MHZ",
    "MENU  THIS HELP",
    "EXIT  QUIT",
    "ANY   ACKNOWLEDGE ALARM",
    "ANY   BACK TO THE SCAN (HERE)",
]
assert all(len(l) <= 32 for l in HELP)
blob = "\0".join(HELP) + "\0"
blob += "\0" * (-len(blob) % 4)
a.text("T_HELP", blob[:-1])          # text() adds the last NUL
a.const("HELP_SIZE", len(blob))
a.const("HELP_LINES", len(HELP))
a.main()
