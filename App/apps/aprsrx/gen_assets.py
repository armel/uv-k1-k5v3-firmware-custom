#!/usr/bin/env python3
# APRS RX read-only assets: the screen texts and the correlator tables.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, so every text costs a
# 2-byte sp-relative add instead of a literal-pool load and a pool word.
# The Mic-E message names are a fixed-stride table read straight into the row.
# The tables are 127 * cos(2 pi f n / 9600): one period of 1200 Hz (8 samples)
# and of 2200 Hz (48 samples = 11 cycles). The sine is the same table read 3/4
# of a period ahead (+6 and +12), as in test/model_rx.py.
#
#   ./gen_assets.py aprsrx_assets.bin aprsrx_assets.h
import math, os, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets
from aprs_symbols_20 import BITMAPS, BYTES_PER_ICON, CODES, WIDTH

FS = 9600
TITLE = "APRS RX"                           # the version is in the .app header
WAIT_CAPS = "WAIT"                          # status-bar capsule until a frame

UI = [
    ("T_TITLE",  TITLE),
    ("T_WAIT",   WAIT_CAPS),
    ("T_DBM",    "dBm"),
    ("T_KMH",    "km/h "),
    ("T_CUSTOM", "Custom-"),
]

# Mic-E message types, indexed by the destination's message bits A B C
MIC_MSG = ["Emergency", "Priority", "Special", "Committed",
           "Returning", "In Service", "En Route", "Off Duty"]

def cos_table(f):
    per = FS // math.gcd(FS, f)
    return [int(round(127 * math.cos(2 * math.pi * f * n / FS))) for n in range(per)]

def mic_code(c):
    """Digit in bits 0-3, message/position bit in bit 4, custom flag in bit 5."""
    digit = 0
    for base in (ord('0'), ord('A'), ord('P')):
        if base <= c < base + 10:
            digit = c - base
    custom = ord('A') <= c <= ord('K')
    bit = c >= ord('P') or custom
    return digit | (int(bit) << 4) | (int(custom) << 5)

a = Assets("APRSRX")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.u8("T_MIC", [mic_code(c) for c in range(128)])
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_WAIT_CHARS", len(WAIT_CAPS))
a.table("T_MSG", MIC_MSG)                    # Mic-E standard messages
# Assets.build() places every text/table before binary data. Include the
# messages between the UI labels and T_MIC in the single display read.
ui_size += len(MIC_MSG) * (max(map(len, MIC_MSG)) + 1) + 128
a.const("UI_SIZE", ui_size)
# The status bar from x = 59 (SPK_X), in one asset_read: the speaker icon
# (bit 0 of the index), 3 blank columns, then the scroll marks at x = 72, up
# (bit 1, rows hidden above) in bits 0-2, down (bit 2, rows hidden below) in
# bits 4-6. 8 variants of TAIL_W bytes.
SPK = [0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c]   # FoxHunt's
UP, DN = [0x04, 0x06, 0x07, 0x06, 0x04], [0x10, 0x30, 0x70, 0x30, 0x10]
tail = []
for i in range(8):
    tail += (SPK if i & 1 else [0] * len(SPK)) + [0] * 3
    tail += [(u if i & 2 else 0) | (d if i & 4 else 0) for u, d in zip(UP, DN)]
a.u8("BMP_TAIL", tail)
a.const("TAIL_W", len(tail) // 8)
# Perfect hash of (table, code) over 128 slots, as drawSymbol() computes it: the
# multiplier is the smallest one without a collision for the current CODES.
symbol_map = bytearray(128 * 3)
for index, code in enumerate(CODES):
    slot = (ord(code[0]) + 59 * ord(code[1])) & 127
    if symbol_map[slot * 3]:
        raise ValueError("APRS symbol hash collision")
    symbol_map[slot * 3:slot * 3 + 3] = bytes((index + 1, ord(code[0]), ord(code[1])))
a.raw("SYM_MAP", symbol_map)
a.raw("SYM_BITMAPS", BITMAPS)
a.const("SYM_COUNT", len(CODES))
a.const("SYM_W", WIDTH)
a.const("SYM_BYTES", BYTES_PER_ICON)
# dem_t prefix: dc, eight filter/correlator words, pm/ps, r/ks, ring.
# Append the existing cosine tables so one read initializes the entire front end.
a.u32("DEMOD_INIT", [2048 << 4] + [0] * 8 + [64, 64] + [0] * 18)
a.i8("COS1200", cos_table(1200))            # 8 entries
a.i8("COS2200", cos_table(2200))            # 48 entries
if __name__ == "__main__":
    a.main()
