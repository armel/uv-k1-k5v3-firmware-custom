#!/usr/bin/env python3
# POCSAG RX read-only assets: the screen texts and the status-bar marks.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, so every text costs a
# 2-byte sp-relative add instead of a literal-pool load and a pool word.
# The demodulator needs no table (test/model_rx.py): no tone to correlate.
#
#   ./gen_assets.py pocsagrx_assets.bin pocsagrx_assets.h
import os, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "POCSAG RX"                         # the version is in the .app header
# key 4: a page without text, in one centred capsule (NOTEXT_X): same length
NOTEXT = ("Tone Only", "Text Lost")
assert len(NOTEXT[0]) == len(NOTEXT[1])

UI = [
    ("T_TITLE",  TITLE),
    ("T_DBM",    "dBm  func "),             # "-89dBm  func 3  last 02m15"
    ("T_LAST",   "  last "),                # 30 characters at most (a tiny row holds 32)
    ("T_PRE",    "pre "),                   # until a first page, or key 3:
                                            # "pre 512 1200:0/0 512:3/96"
    ("T_TONE",   NOTEXT[0]),                # key 4: an address alone
    ("T_LOST",   NOTEXT[1]),                # key 4: characters came, none was read
]

a = Assets("POCSAGRX")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.const("NOTEXT_CHARS", len(NOTEXT[0]))

# The status bar from x = 59 (SPK_X), in one asset_read, as APRS RX: the speaker
# icon (bit 0 of the index), 3 blank columns, then the scroll marks at x = 72,
# up (bit 1) in bits 0-2, down (bit 2) in bits 4-6. 8 variants of TAIL_W bytes.
SPK = [0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c]   # FoxHunt's
UP, DN = [0x04, 0x06, 0x07, 0x06, 0x04], [0x10, 0x30, 0x70, 0x30, 0x10]
tail = []
for i in range(8):
    tail += (SPK if i & 1 else [0] * len(SPK)) + [0] * 3
    tail += [(u if i & 2 else 0) | (d if i & 4 else 0) for u, d in zip(UP, DN)]
a.u8("BMP_TAIL", tail)
a.const("TAIL_W", len(tail) // 8)
# Key 4 on: an outlined bell (a tone-only pager only rings), read to x = 45-52
# (TONE_X): centred between the title capsule (x <= 38) and the speaker icon
# (x >= 59).
a.u8("BMP_TONE", [0x20, 0x3c, 0x22, 0x61, 0x61, 0x22, 0x3c, 0x20])
if __name__ == "__main__":
    a.main()
