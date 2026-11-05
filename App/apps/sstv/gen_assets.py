#!/usr/bin/env python3
# SSTV read-only assets: the screen texts, the dither thresholds and the
# speaker icon.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, so every text costs a
# 2-byte sp-relative add instead of a literal-pool load and a pool word.
# THR holds the per-row thresholds of the two renderings (key 3), 4 rows of 4
# columns each: the 4x4 Bayer matrix (dither) then a flat mid-grey (1-bit), as
# in test/model.py (THR, and the 128 of the threshold sweep).
# MODES holds one fixed-size record per SSTV mode (test/modes.py record(),
# sstv_app.c mode_t), T_NAMES their names.
#
#   ./gen_assets.py sstv_assets.bin sstv_assets.h
import os, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "test"))
from app_assets import Assets, cdefine
from model import BAYER
import modes

TITLE = "SSTV"                              # the version is in the .app header

UI = [
    ("T_TITLE",  TITLE),
    ("T_WAIT",   "WAIT"),                   # status-bar capsule while nothing came
    ("T_RX",     " "),                      # after the mode: "PD120 123/248"
    ("T_OK",     " OK"),
    ("T_LOST",   " lost"),
    ("T_SENT",   "Logo sent"),
    ("T_DENIED", "TX denied"),
    ("T_ABORT",  "TX aborted"),
    ("T_NOPIC",  "No picture yet"),
    ("T_RXABORT","RX aborted"),
    ("T_DITHER", "dither"),
    ("T_1BIT",   "1-bit"),
    ("T_HELP",   "MENU tx 1/F1mode 2bw 3spk 4view"),    # 31 tiny characters
]

# The status line of the info screen, by ST_* (sstv_app.c): its text's offset
# in the UI block. RX, OK and LOST follow the mode of the picture, RX is
# followed by "period/periods". WAIT is shown as a capsule in the status bar.
STATUS = ["T_WAIT", "T_RX", "T_OK", "T_LOST", "T_SENT", "T_DENIED", "T_ABORT", "T_NOPIC",
          "T_RXABORT"]

assert all(len(s) <= 32 for n, s in UI if n == "T_HELP")   # print_tiny does not clip

a = Assets("SSTV")
ui_size = 0
offset = {}
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    offset[name] = ui_size
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)                 # the UI block read by draw()
a.u8("ST_TEXT", [offset[n] for n in STATUS])
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_WAIT_CHARS", len("WAIT"))
a.u8("THR", [b * 16 + 8 for b in BAYER] + [128] * 16)
a.table("T_NAMES", [m[0] for m in modes.MODES])
a.raw("MODES", b"".join(modes.record(i)[1] for i in range(len(modes.MODES))))
a.const("MODE_COUNT", len(modes.MODES))
a.const("MODE_SIZE", modes.SIZE)
a.const("MODE_RX", modes.RX_SIZE)
a.const("SEG_MAX", modes.SEG_MAX)
a.u8("BMP_F", [0x3e,0x7f,0x41,0x75,0x75,0x75,0x7d,0x7f,0x3e])   # F armed, as APRS TX / FoxHunt
a.u8("BMP_SPK", [0x1c, 0x1c, 0x3e, 0x7f, 0x00, 0x22, 0x1c, 0x41, 0x22, 0x1c])  # FoxHunt's
# The last picture, LCD layout (status line then the 7 frame lines), blank as
# packaged. It must stay the LAST item: the loader rewrites the last 1024 bytes
# of the assets with the screen left at exit (APP_FLAG_SAVE_SCREEN,
# APP_SCREEN_SAVE_LEN in app_overlay.h; pack_app.py --save-screen).
a.raw("PIC", bytes(cdefine("app_overlay.h", "APP_SCREEN_SAVE_LEN")))
a.main()
