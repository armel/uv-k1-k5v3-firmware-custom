#!/usr/bin/env python3
# EPIRB 406 read-only assets: the screen texts and the protocol names.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, so every text costs a
# 2-byte sp-relative add instead of a literal-pool load and a pool word.
# The protocol names are parsed from dec406.c (NAMES in dec406_proto_name), so
# the host test and the app share one table.
#
#   ./gen_assets.py epirb406_assets.bin epirb406_assets.h
import os, re, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

def proto_names():
    src = open(os.path.join(HERE, "dec406.c")).read()
    m = re.search(r"static const char NAMES\[\]\s*=(.*?);", src, re.S)
    if not m:
        sys.exit("dec406.c: NAMES table not found")
    packed = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
    names = packed.split("\\0")
    if len(names) != 24:
        sys.exit(f"dec406.c: {len(names)} protocol names, 24 expected")
    return names

TITLE = "EPIRB 406"

UI = [
    ("T_TITLE",    TITLE),
    ("T_WAIT",     "WAITING..."),
    ("T_NOPOS",    "no position"),
    ("T_SELFTEST", "SELF-TEST "),
    ("T_LONG",     "LONG"),
    ("T_SHORT",    "SHORT"),
    ("T_BCH",      "BCH "),
    ("T_OK",       "OK"),
    ("T_ERR",      "ER"),
    ("T_NA",       "--"),
    ("T_SEP",      " / "),
    ("T_FRAME",    "FR "),
    ("T_DBM",      "dBm"),
    ("T_INT",      " INT"),
    ("T_EXT",      " EXT"),
    ("T_HOMING",   " 121"),
    ("T_COARSE",   " COARSE"),
    ("T_RAWID",    " RAW ID"),
    ("T_CANCEL",   " CANCEL"),
    ("T_KHZ",      " kHz"),
    ("T_RX",       "RX "),
    ("T_ERROR",    " ER "),
    ("T_NOSYNC",   " NS"),
    ("T_CUT",      " CUT"),
]

a = Assets("EPIRB406")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)                 # the UI block read by draw()
a.const("T_TITLE_CHARS", len(TITLE))
a.table("T_PROTO", proto_names())           # 16 location, then 8 user protocols
UP, DN = [0x04, 0x06, 0x07, 0x06, 0x04], [0x10, 0x30, 0x70, 0x30, 0x10]
a.u8("BMP_SCROLL", [(u if i & 1 else 0) | (d if i & 2 else 0)
                     for i in range(4) for u, d in zip(UP, DN)])
a.const("SCROLL_W", len(UP))
SPK = [0x1c, 0x1c, 0x3e, 0x7f, 0x00, 0x22, 0x1c, 0x41, 0x22, 0x1c]
a.u8("BMP_SPK", SPK)
a.const("SPK_W", len(SPK))
a.main()
