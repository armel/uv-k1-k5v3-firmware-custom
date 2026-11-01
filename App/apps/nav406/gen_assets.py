#!/usr/bin/env python3
# Copyright 2026 Johan Denoyer F4WAT, Licensed under the Apache License,
# Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0). The dial is drawn
# with GeoNav's helpers (Copyright 2026 Armel F4HWN, Apache-2.0).
"""Nav 406 assets: UI texts, the help page, the baud rates, the Q1000 cosine
table and the two dial bitmaps (north up with the cardinal letters, course up without).
The dial is GeoNav's (Armel F4HWN, Apache-2.0), drawn with its helpers."""
import importlib.util
import math
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

spec = importlib.util.spec_from_file_location("geonav_assets",
                                              os.path.join(HERE, "..", "geonav", "gen_assets.py"))
gn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gn)

VER = os.environ.get("APP_VER", "dev")
UI = [
    ("T_TITLE", "NAV406 " + VER),
    ("T_NO_TARGET", "NO TARGET"),
    ("T_GET_TARGET", "EPIRB 406: KEY 2"),
    ("T_SAVED", "TARGET SAVED"),
    ("T_AHEAD", "AHEAD"),
    ("T_LEFT", "L "),
    ("T_RIGHT", "R "),
    ("T_NUP", "N UP"),
    ("T_AZ", "AZ "),
    ("T_PM", "+-"),
    ("T_GPS", "GPS "),
    ("T_NO_DATA", "NO DATA"),
    ("T_NO_FIX", "NO FIX"),
    ("T_KMH", "km/h"),
    ("T_DP", " D+"),
    ("T_DM", " D-"),
    ("T_M", "m"),
    ("T_KM", "km"),
    ("T_8N1", " 8N1"),
    ("T_ID", "ID "),
]


def dial(letters):
    """GeoNav's dial: ring, ticks every 30 degrees (longer at the cardinals),
    the top index; the N/E/S/W letters if asked."""
    R, c = gn.DIAL_R, gn.DIAL_R
    px = set()
    ry = round(R * gn.Y_NUM / gn.Y_DEN)
    gn.circle(px, c, c, R)
    ring = set(px)

    def along(k, inset):
        a, f = math.radians(k), (R - inset) / R
        return c + R * f * math.sin(a), c - ry * f * math.cos(a)

    for k in range(0, 360, 30):
        ex, ey = along(k, 0)
        start = min(ring, key=lambda q: (q[0] - ex) ** 2 + (q[1] - ey) ** 2)
        ix, iy = along(k, 5 if k % 90 == 0 else 3)
        gn.segment(px, *start, round(ix), round(iy))
    if letters:
        for k, ch in ((90, "E"), (180, "S"), (270, "W")):
            x, y = along(k, 10)
            gn.tiny(px, ch, round(x) - 1, round(y) - 2)
    for y in range(c - ry):                 # top index: north, or ahead
        px.update((x, y) for x in range(c - y * 3 // 4, c + y * 3 // 4 + 1))
    return gn.pages(px, gn.DIAL_W, gn.DIAL_PAGES)


a = Assets("NAV406")
ui_size = 0
for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
    ui_size += len(value) + 1 + pad
a.const("UI_SIZE", ui_size)
# MENU: the help page, NUL-separated lines in the tiny font (32 characters),
# under a bold line with the current GPS settings.
HELP = [
    "1      BAUD RATE (SAVED)",
    "2      PIN D+ / D- (SAVED)",
    "8 / 9  SIG FINDER 121.5 / 243",
    "0      FORGET THE TARGET",
    "MENU   CLOSE THIS PAGE",
    "EXIT   EPIRB 406, OR QUIT",
]
assert all(len(l) <= 32 for l in HELP)
help_blob = "\0".join(HELP) + "\0"
help_blob += "\0" * (-len(help_blob) % 4 or 4)   # ends with an empty line
a.text("T_HELP", help_blob[:-1])
a.const("HELP_SIZE", len(help_blob))
a.table("T_BAUD", ["4800", "9600", "19200", "38400", "57600", "115200"], stride=8)
a.u16("COS_Q1000", [round(1000.0 * math.cos(math.radians(d))) for d in range(91)])
a.raw("DIAL_NUP", dial(True))
a.raw("DIAL_CUP", dial(False))
a.const("DIAL_R", gn.DIAL_R)
a.const("DIAL_W", gn.DIAL_W)
a.const("DIAL_PAGES", gn.DIAL_PAGES)
a.const("DIAL_Y_NUM", gn.Y_NUM)
a.const("DIAL_Y_DEN", gn.Y_DEN)

if __name__ == "__main__":
    a.main()
