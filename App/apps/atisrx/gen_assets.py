#!/usr/bin/env python3
# ATIS RX read-only assets: the screen texts, the country table and the
# correlator tables.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, so every text costs a
# 2-byte sp-relative add instead of a literal-pool load and a pool word.
# MIDS / MID_INFO: the maritime identification digits known to test/atis.py,
# sorted, and for each the first letter of its call signs then its country.
# The tables are 127 * cos(2 pi f n / 9600): one period of 1300 Hz (96 samples
# = 13 cycles) and of 2100 Hz (32 samples = 7 cycles). The sine is the same
# table read 3/4 of a period ahead (Q_Y, Q_B), as in test/model_rx.py.
#
#   ./gen_assets.py atisrx_assets.bin atisrx_assets.h
import os, struct, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "test"))
from app_assets import Assets
import atis
from model_rx import cos_table, quarter, F_Y, F_B

TITLE = "ATIS RX"                           # the version is in the .app header
WAIT_CAPS = "WAIT"                          # status-bar capsule until a station

UI = [
    ("T_TITLE",  TITLE),
    ("T_WAIT",   WAIT_CAPS),
    ("T_DBM",    "dBm  heard "),            # "-89dBm  heard 3  last 02m15"
    ("T_LAST",   "  last "),                # 32 characters at most, what a tiny row holds
    ("T_CH",     "CH"),
]

a = Assets("ATISRX")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_WAIT_CHARS", len(WAIT_CAPS))

# Countries: the MIDs in one array (one read, then a linear search), and one
# fixed-size record per MID: the first letter of the call sign, the name.
MID_STRIDE = 16
mids = sorted(atis.MIDS)
info = b""
for m in mids:
    letter, name = atis.MIDS[m]
    if len(name) > MID_STRIDE - 2 or len(name) > 18:
        raise ValueError("country name too long: " + name)
    info += letter.encode("ascii") + name.encode("ascii").ljust(MID_STRIDE - 1, b"\0")
a.u16("MIDS", mids)
a.raw("MID_INFO", info)
a.const("MID_COUNT", len(mids))
a.const("MID_STRIDE", MID_STRIDE)

# The status bar from x = 59 (SPK_X), in one asset_read, as APRS RX: the speaker
# icon (bit 0 of the index), 3 blank columns, then the marks at x = 72, up
# (bit 1, a newer station) in bits 0-2, down (bit 2, an older one) in bits 4-6.
SPK = [0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c]   # FoxHunt's
UP, DN = [0x04, 0x06, 0x07, 0x06, 0x04], [0x10, 0x30, 0x70, 0x30, 0x10]
tail = []
for i in range(8):
    tail += (SPK if i & 1 else [0] * len(SPK)) + [0] * 3
    tail += [(u if i & 2 else 0) | (d if i & 4 else 0) for u, d in zip(UP, DN)]
a.u8("BMP_TAIL", tail)
a.const("TAIL_W", len(tail) // 8)

# dem_t prefix: dc, eight filter/correlator words, py/pb, r/ky/kb, ring.
# The cosine tables follow, so one read initializes the entire front end.
a.u32("DEMOD_INIT", [2048 << 4] + [0] * 8 + [64, 64] + [0] * 3 + [0] * 16)
a.i8("COS1300", cos_table(F_Y))             # 96 entries
a.i8("COS2100", cos_table(F_B))             # 32 entries
a.const("Q_Y", quarter(F_Y))                # 72
a.const("Q_B", quarter(F_B))                # 8
if __name__ == "__main__":
    a.main()
