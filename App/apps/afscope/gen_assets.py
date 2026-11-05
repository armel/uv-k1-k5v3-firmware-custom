#!/usr/bin/env python3
# AF Scope read-only assets: the screen texts, the timebase and gain names and
# the known tones first (one word-aligned block that draw() reads into the
# stack), then the main screen's big digits (taken from App/font.c, so they
# match the firmware), the speaker icon, and the fixed parts of the screens
# drawn here rather than in code: the status-bar capsules, the SCOPE grid and
# the TUNE needle scale per span, read straight into the status line or the
# framebuffer (128-byte lines, LSB = top row).
#
#   ./gen_assets.py afscope_assets.bin afscope_assets.h
import os, re, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

UI = [
    ("T_HZ",    "Hz"),
    ("T_DBM",   "dBm "),                    # TUNE: "  RSSI  -87dBm   PP  812"
    ("T_RSSI",  "  RSSI "),
    ("T_STAB",  "STAB"),
    ("T_PP",    "  PP "),                   # SCOPE: Hz, 2 blanks, PP, 2 blanks,
    ("T_DC",    "  DC "),                   # DC right on the screen's edge
    ("T_NONE",  "----"),
    ("T_PERS",  " PERS"),
]
# SCOPE timebases (key UP / DOWN): the time per 32-pixel division
TB_TEXT = ["0.8ms/d", "1.7ms/d", "3.3ms/d", "6.7ms/d", " 13ms/d"]   # one width:
                                            # the bottom line's fields stay put
TB_STRIDE = 8
GAIN_TEXT = ["AUTO", "x1  ", "x2  ", "x4  ", "x8  ", "x16 "]   # each step doubles;
                                            # padded to one width
GAIN_STRIDE = 5
EDGE_TEXT = [" /", " \\"]                   # trigger on the rising / falling edge
EDGE_STRIDE = 3

# Tones the TUNE needle measures against: the nearest one names the reading.
# Names are labels, not detections (the app decodes nothing).
TONES = [
    (600, "CW TONE"), (697, "DTMF ROW 1"), (700, "CW TONE"), (770, "DTMF ROW 2"),
    (800, "CW TONE"), (852, "DTMF ROW 3"), (941, "DTMF ROW 4"), (1000, "TEST TONE"),
    (1200, "MARK / SYNC"), (1209, "DTMF COL 1"), (1336, "DTMF COL 2"),
    (1477, "DTMF COL 3"), (1500, "SSTV BLACK"), (1633, "DTMF COL 4"),
    (1750, "CALL TONE"), (1900, "SSTV LEADER"), (2200, "SPACE"), (2300, "SSTV WHITE"),
]
NAME_STRIDE = 17                           # "1750 CALL TONE": the label

# ---- screen geometry, shared with afscope_app.c and test/render.py ----
# status bar: the title capsule (print_inverse; text x, end x), then RAW
# (text x) while the filters are off, then the icons, right-aligned on the
# battery text (UI_DrawStatusBattery writes from x 84 with 4 characters):
# the lock while held, else the clip mark while samples reach the ADC limits
# (9 px), then the speaker (10 px), up to x 82
CAPS_TITLE, RAW_X = (2, 34), 38
LOCK_X, SPK_X = 61, 73
SPANS = [50, 20, 10]                       # TUNE needle: +-Hz full scale
TUNE_MID, SCALE_Y = 63, 19                 # needle scale: centre, line (fb y)
SCOPE_MID = 19                             # SCOPE trace centre (fb y)

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


# The firmware's big frequency digits (the active "Terminus font" variant):
# 10 columns over two pages, '0'..'9' then '-'.
BIG = font_rows("gFontBigDigits", 20, SRC.index("// Terminus font"))
TINY = font_rows("gFont3x5", 3)            # from ' '
if len(BIG) != 11:
    sys.exit("font.c: unexpected gFontBigDigits layout")


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

    def bytes(self):
        return b"".join(self.b)


def capsule(c, s, x, end):                  # GUI_DisplaySmallestInverse, status line
    c.tiny(s, x, 1)
    b = c.b[0]
    b[x - 2] ^= 0x3E
    for i in range(x - 1, end):
        b[i] ^= 0x7F
    b[end] ^= 0x3E


# "AF SCOPE", then "RAW" after it with the filters off: one segment per
# path (raw)
STATUS_W = RAW_X + 4 * len("RAW") + 1


def status(raw):
    c = Lines(1)
    capsule(c, "AF SCOPE", *CAPS_TITLE)
    if raw:
        capsule(c, "RAW", RAW_X, RAW_X + 4 * len("RAW"))
    return bytes(c.b[0][:STATUS_W])


def tune_scale(label):                      # fb line 2, y 16..23
    c = Lines(3)
    for x in range(TUNE_MID - 50, TUNE_MID + 51):
        c.set(x, SCALE_Y)
    for x in range(TUNE_MID - 50, TUNE_MID + 51, 10):
        for y in range(SCALE_Y - (3 if x == TUNE_MID else 2), SCALE_Y):
            c.set(x, y)
    c.tiny("-" + label, 0, 16)
    c.tiny("+" + label, 128 - 4 * (len(label) + 1), 16)
    return bytes(c.b[2])


def scope_grid():                           # fb lines 0..4
    c = Lines(5)
    for x in range(0, 128, 3):
        c.set(x, SCOPE_MID)
    for x in range(32, 128, 32):
        for y in range(1, 39, 3):
            c.set(x, y)
    return c.bytes()


SPK = [0x1c, 0x1c, 0x3e, 0x7f, 0x00, 0x22, 0x1c, 0x41, 0x22, 0x1c]   # FoxHunt's
LOCK = [0x7c, 0x46, 0x45, 0x45, 0x45, 0x45, 0x45, 0x46, 0x7c]        # FoxHunt's
CLIP = [0x0e, 0x01, 0x01, 0x01, 0x3e, 0x40, 0x40, 0x40, 0x38]        # a clipped wave
if len(CLIP) != len(LOCK):
    sys.exit("CLIP and LOCK share one slot: same width")

a = Assets("AFSCOPE")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
labels = ["%d %s" % t for t in TONES]
if any(len(n) + 1 > NAME_STRIDE for n in labels) or \
        [f for f, _ in TONES] != sorted(f for f, _ in TONES):
    sys.exit("TONES: labels fit the stride, frequencies ascending")
for name, table, stride in (("T_TB", TB_TEXT, TB_STRIDE), ("T_GAIN", GAIN_TEXT, GAIN_STRIDE),
                            ("T_EDGE", EDGE_TEXT, EDGE_STRIDE), ("T_TONES", labels, NAME_STRIDE)):
    a.table(name, table, stride=stride)
    ui_size += len(table) * stride
if ui_size % 4:                             # TONE_HZ word-aligned in the block
    pad = -ui_size % 4
    a.table("T_PAD", ["x" * (pad - 1)], stride=pad)
    ui_size += pad
# The first data item, right after the texts: draw() reads it in the same block.
a.u16("TONE_HZ", [f for f, _ in TONES])
ui_size += 2 * len(TONES)
a.const("UI_SIZE", ui_size)                 # the block read by draw()
a.const("TONE_COUNT", len(TONES))
a.const("TB_COUNT", len(TB_TEXT))
a.const("GAIN_COUNT", len(GAIN_TEXT))
a.u8("BIG", [b for g in BIG for b in g])
a.u8("BMP_SPK", SPK)
a.u8("BMP_LOCK", LOCK)
a.u8("BMP_CLIP", CLIP)
a.raw("STATUS", status(0) + status(1))
a.const("STATUS_W", STATUS_W)
a.const("LOCK_X", LOCK_X)
a.const("SPK_X", SPK_X)
a.raw("TUNE_SCALE", b"".join(tune_scale("%d" % s) for s in SPANS))
a.raw("SCOPE_GRID", scope_grid())

if __name__ == "__main__":
    a.main()
