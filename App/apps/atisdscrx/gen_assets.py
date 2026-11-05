#!/usr/bin/env python3
# ATIS DSC RX read-only assets: the screen texts, the name and country tables and
# the correlator tables.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, as in ATIS RX.
# NAMES: one fixed-size record per code the screen names: the code, its kind
# (category, nature of distress, telecommand), then the name (test/dsc.py
# keeps the long forms; these fit a normal-font row with " 14:35" or " CH16").
# MIDS / MID_NAME: the maritime identification digits of test/dsc.py, sorted,
# and the country of each.
# The demodulator tables are ATIS RX's (atisrx/test/model_rx.py).
#
#   ./gen_assets.py atisdscrx_assets.bin atisdscrx_assets.h
import os, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "test"))
sys.path.insert(0, os.path.join(HERE, "..", "atisrx", "test"))
from app_assets import Assets
import dsc
from model_rx import cos_table, quarter, F_Y, F_B   # ATIS RX's demodulator

TITLE = "ATIS DSC"                          # status bar: ends at x = 34, before WAIT (x = 40)
WAIT_CAPS = "WAIT"

UI = [
    ("T_TITLE",  TITLE),
    ("T_WAIT",   WAIT_CAPS),
    ("T_DBM",    "dBm  heard "),            # "-89dBm  heard 3  last 02m15"
    ("T_LAST",   "  last "),
    ("T_CH",     "CH"),
    ("T_TO",     "TO "),
    ("T_ALL",    "TO ALL SHIPS"),
    ("T_AREA",   "TO AREA"),
    ("T_MAYDAY", "MAYDAY "),
    ("T_NOPOS",  "POSITION ?"),
    ("T_NOUTC",  " --:--"),
]

a = Assets("ATISDSCRX")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_WAIT_CHARS", len(WAIT_CAPS))

# kind 0: category (the title of a call), 1: nature of distress, 2: telecommand
K_CAT, K_NAT, K_TC = 0, 1, 2
SHORT = {
    (K_CAT, 100): "ROUTINE", (K_CAT, 108): "SAFETY", (K_CAT, 110): "URGENCY",
    (K_CAT, 112): "DISTRESS",
    (K_NAT, 100): "FIRE", (K_NAT, 101): "FLOODING", (K_NAT, 102): "COLLISION",
    (K_NAT, 103): "GROUNDING", (K_NAT, 104): "CAPSIZING", (K_NAT, 105): "SINKING",
    (K_NAT, 106): "ADRIFT", (K_NAT, 107): "UNSPECIFIED", (K_NAT, 108): "ABANDONING",
    (K_NAT, 109): "PIRACY", (K_NAT, 110): "OVERBOARD", (K_NAT, 112): "EPIRB",
    (K_TC, 100): "F3E SIMPLEX", (K_TC, 101): "F3E DUPLEX", (K_TC, 103): "POLLING",
    (K_TC, 104): "UNABLE", (K_TC, 105): "END OF CALL", (K_TC, 106): "DATA",
    (K_TC, 109): "J3E", (K_TC, 110): "DISTRESS ACK", (K_TC, 112): "DIST RELAY",
    (K_TC, 113): "TTY FEC", (K_TC, 115): "TTY ARQ", (K_TC, 118): "TEST",
    (K_TC, 121): "POS UPDATE", (K_TC, 126): "NO INFO",
}
assert {c for k, c in SHORT if k == K_CAT} == set(dsc.CATEGORIES)
assert {c for k, c in SHORT if k == K_NAT} == set(dsc.NATURES)
assert {c for k, c in SHORT if k == K_TC} == set(dsc.TELECOMMANDS)
NAME_STRIDE = 16
names = b""
for (kind, code), s in sorted(SHORT.items()):
    if len(s) > 12:                          # + " CH16" or " 14:35" in 18 columns
        raise ValueError("name too long: " + s)
    names += bytes([code, kind]) + s.encode("ascii").ljust(NAME_STRIDE - 2, b"\0")
a.raw("NAMES", names)
a.const("NAME_COUNT", len(SHORT))
a.const("NAME_STRIDE", NAME_STRIDE)
a.const("K_CAT", K_CAT)
a.const("K_NAT", K_NAT)
a.const("K_TC", K_TC)

MID_STRIDE = 12
mids = sorted(dsc.MIDS)
info = b""
for m in mids:
    n = dsc.MIDS[m]
    if len(n) > MID_STRIDE - 1:
        raise ValueError("country name too long: " + n)
    info += n.encode("ascii").ljust(MID_STRIDE, b"\0")
a.u16("MIDS", mids)
a.raw("MID_NAME", info)
a.const("MID_COUNT", len(mids))
a.const("MID_STRIDE", MID_STRIDE)

# The status bar from x = 59, as ATIS RX: speaker icon, then the up/down marks.
SPK = [0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c]   # FoxHunt's
UP, DN = [0x04, 0x06, 0x07, 0x06, 0x04], [0x10, 0x30, 0x70, 0x30, 0x10]
tail = []
for i in range(8):
    tail += (SPK if i & 1 else [0] * len(SPK)) + [0] * 3
    tail += [(u if i & 2 else 0) | (d if i & 4 else 0) for u, d in zip(UP, DN)]
a.u8("BMP_TAIL", tail)
a.const("TAIL_W", len(tail) // 8)

# dem_t prefix then the cosine tables, as ATIS RX: one read initializes it.
a.u32("DEMOD_INIT", [2048 << 4] + [0] * 8 + [64, 64] + [0] * 3 + [0] * 16)
a.i8("COS1300", cos_table(F_Y))             # 96 entries
a.i8("COS2100", cos_table(F_B))             # 32 entries
a.const("Q_Y", quarter(F_Y))
a.const("Q_B", quarter(F_B))
if __name__ == "__main__":
    a.main()
