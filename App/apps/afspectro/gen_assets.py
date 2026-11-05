#!/usr/bin/env python3
# AF Spectro read-only assets: the screen texts first (one word-aligned block
# that draw() reads into the stack), then the speaker icon, the quarter sine
# table of the FFT and its window with the dB mantissa table after it (one
# read at launch), and the fixed parts of the screens drawn here rather than
# in code: the status-bar capsules and the frequency axis per sample rate,
# read straight into the status line or the framebuffer (128-byte lines,
# LSB = top row).
#
#   ./gen_assets.py afspectro_assets.bin afspectro_assets.h
import math, os, re, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

UI = [
    # the header, in fixed columns (32 characters):
    #   "1200 / 2200Hz         TW  -5.1dB"
    #   "PK 1180Hz   MIN 0410    MAX 1590"
    ("T_TW",    "         TW "),
    ("T_DB",    "dB"),
    ("T_HZ",    "Hz"),
    ("T_PK",    "PK "),
    ("T_MIN",   "   MIN "),
    ("T_MAX",   "    MAX "),
]

# ---- screen geometry, shared with afspectro_app.c and test/render.py ----
# status bar: the title capsule (print_inverse; text x, end x), then RAW
# (text x) while the filters are off, then the icons, right-aligned on the
# battery text (UI_DrawStatusBattery writes from x 84 with 4 characters):
# the lock while held, else the clip mark while samples reach the ADC limits
# (9 px), then the speaker (10 px), up to x 82
CAPS_TITLE, RAW_X = (2, 42), 46
LOCK_X, SPK_X = 61, 73
TICK_X = [27, 53, 80, 107]                     # 1..4 kHz at 9.6 kHz, x 2 per rate step

FONT_C = os.path.join(HERE, "..", "..", "font.c")
SRC = open(FONT_C).read()


def font_rows(name, width, start_at=0):
    start = SRC.index("{", SRC.index(name, start_at)) + 1
    rows = []
    for line in SRC[start:SRC.index("};", start)].splitlines():
        line = re.sub(r"/\*.*?\*/", "", line).split("//")[0]
        vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", line)]
        if len(vals) == width:
            rows.append(vals)
    return rows


TINY = font_rows("gFont3x5", 3)            # from ' '


class Lines:
    """n framebuffer lines of 128 columns, drawn pixel by pixel"""
    def __init__(self, n):
        self.b = [bytearray(128) for _ in range(n)]

    def set(self, x, y):
        self.b[y >> 3][x] |= 1 << (y & 7)

    def tiny(self, s, x, y):                # GUI_DisplaySmallest
        for ch in s:
            for i, col in enumerate(TINY[ord(ch) - 32]):
                for j in range(6):
                    if col >> j & 1:
                        self.set(x + i, y + j)
            x += 4


def capsule(c, s, x, end):                  # GUI_DisplaySmallestInverse, status line
    c.tiny(s, x, 1)
    b = c.b[0]
    b[x - 2] ^= 0x3E
    for i in range(x - 1, end):
        b[i] ^= 0x7F
    b[end] ^= 0x3E


# "AF SPECTRO", then "RAW" after it with the filters off: one segment per
# path (raw)
STATUS_W = RAW_X + 4 * len("RAW") + 1


def status(raw):
    c = Lines(1)
    capsule(c, "AF SPECTRO", *CAPS_TITLE)
    if raw:
        capsule(c, "RAW", RAW_X, RAW_X + 4 * len("RAW"))
    return bytes(c.b[0][:STATUS_W])



def axis(fsi):                              # one line: ticks on rows 0-1, kHz labels
    c = Lines(1)
    for i, x in enumerate(TICK_X):
        c.set(x, 0); c.set(x, 1)
        s = "%dk" % ((i + 1) << fsi)
        c.tiny(s, x - len(s) * 2 + 1, 2)
    return bytes(c.b[0])


SPK = [0x1c, 0x1c, 0x3e, 0x7f, 0x00, 0x22, 0x1c, 0x41, 0x22, 0x1c]   # FoxHunt's
LOCK = [0x7c, 0x46, 0x45, 0x45, 0x45, 0x45, 0x45, 0x46, 0x7c]        # FoxHunt's
CLIP = [0x0e, 0x01, 0x01, 0x01, 0x3e, 0x40, 0x40, 0x40, 0x38]        # a clipped wave
if len(CLIP) != len(LOCK):
    sys.exit("CLIP and LOCK share one slot: same width")
# sin(2 pi i / 256), i = 0..64, Q14: the FFT twiddles, the Hann window
SINQ = [int(round(16384 * math.sin(2 * math.pi * i / 256))) for i in range(65)]

a = Assets("AFSPECTRO")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)                 # the block read by draw()
a.u8("BMP_SPK", SPK)
a.u8("BMP_LOCK", LOCK)
a.u8("BMP_CLIP", CLIP)
a.i16("SINQ", SINQ)
# 10 log10(1 + i / 32) in tenths of dB, the mantissa of lg(), read with SINQ
a.u8("LOGT", [int(round(100 * math.log10(1 + i / 32))) for i in range(32)])
a.raw("STATUS", status(0) + status(1))
a.const("STATUS_W", STATUS_W)
a.const("LOCK_X", LOCK_X)
a.const("SPK_X", SPK_X)
a.raw("AXIS", b"".join(axis(i) for i in range(3)))


if __name__ == "__main__":
    a.main()
