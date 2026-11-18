#!/usr/bin/env python3
"""GeoNav UI, APRS TX profile proxy, latitude cosine table and the bitmaps of
the azimuth dial and the message icon."""
import math
import os
import struct
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets, cdefine
from af_lcd import BOLD, TINY

PROXY_MAGIC = cdefine("app_config_proxy.h", "APP_CONFIG_PROXY_MAGIC")
PROFILE_MAGIC = cdefine("aprs_profile.h", "APRS_PROFILE_MAGIC")
PROFILE_HEAD = cdefine("aprs_profile.h", "APRS_PROFILE_COMMENT")
TARGET = b"APRS TX"

# GeoNav only reads the profile header through the configuration proxy. The
# comment and callsign stay private to APRS TX/APRS Edit.
proxy = struct.pack("<I16sHBBI", PROXY_MAGIC, TARGET.ljust(16, b"\0"),
                    0, PROFILE_HEAD, 0, PROFILE_MAGIC)
if len(proxy) != 28:
    raise ValueError("configuration proxy descriptor layout changed")

TITLE = "GEONAV"
UI = [
    ("T_TITLE", TITLE),
    ("T_NO_TARGET", "NO TARGET"),
    ("T_OPEN_SONDE", "From a sonde app"),
    ("T_QTH_MISSING", "QTH NOT SET"),
    ("T_USE_APRS_EDIT", "Set in APRS Edit"),
    ("T_NEED_APRS_TX", "APRS TX required"),
    ("T_BACK", "EXIT BACK"),
    ("T_AZ", "AZ"),
    ("T_ALT", "ALT"),
    ("T_EL", "EL"),
    ("T_SCALE", "SCALE"),
    ("T_M", " m"),
    ("T_KM", " km"),
]

# Azimuth dial, north up: outer ring, 10/30/90 degree graduations, cardinal
# letters, north index and the dotted range ring. The app copies it into the
# left of the frame buffer, then draws the azimuth and the target over it.
# LCD pixels are taller than wide (5:6), so every vertical radius is scaled
# by Y_NUM / Y_DEN to keep the dial round on the glass.
DIAL_R = 27                    # horizontal radius; the centre is (DIAL_R, DIAL_R)
DIAL_RING = 12                 # range ring radius: the 1-2-5 scale step
DIAL_W = 2 * DIAL_R + 1
DIAL_PAGES = 7
Y_NUM, Y_DEN = 5, 6


def polar(cx, cy, r, deg):
    a = math.radians(deg)
    return round(cx + r * math.sin(a)), round(cy - r * Y_NUM / Y_DEN * math.cos(a))


def segment(px, x0, y0, x1, y1):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        px.add((x0, y0))
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def circle(px, cx, cy, r, dotted=False):
    """Midpoint ellipse of horizontal radius r, vertical r * Y_NUM / Y_DEN."""
    rx, ry = r, round(r * Y_NUM / Y_DEN)
    pts = set()
    def quad(x, y):
        pts.update(((cx + x, cy + y), (cx - x, cy + y), (cx + x, cy - y), (cx - x, cy - y)))
    x, y = 0, ry
    d1 = ry * ry - rx * rx * ry + rx * rx / 4
    while ry * ry * x < rx * rx * y:
        quad(x, y)
        if d1 < 0:
            d1 += ry * ry * (2 * x + 3)
        else:
            d1 += ry * ry * (2 * x + 3) + rx * rx * (2 - 2 * y)
            y -= 1
        x += 1
    d2 = ry * ry * (x + 0.5) ** 2 + rx * rx * (y - 1) ** 2 - rx * rx * ry * ry
    while y >= 0:
        quad(x, y)
        if d2 > 0:
            d2 += rx * rx * (3 - 2 * y)
        else:
            d2 += ry * ry * (2 * x + 2) + rx * rx * (3 - 2 * y)
            x += 1
        y -= 1
    pts = sorted(pts, key=lambda q: math.atan2(q[1] - cy, q[0] - cx))
    px.update(p for i, p in enumerate(pts) if not dotted or i % 2 == 0)


def tiny(px, s, x, y):
    for ch in s:
        g = TINY[ord(ch) - 32]
        for i in range(3):
            for j in range(6):
                if g[i] >> j & 1:
                    px.add((x + i, y + j))
        x += 4


def pages(px, width, count):
    """Column bytes, LSB = top row, one block of `width` bytes per page."""
    out = bytearray()
    for page in range(count):
        for x in range(width):
            out.append(sum(1 << bit for bit in range(8) if (x, page * 8 + bit) in px))
    return bytes(out)


def dial():
    px, c = set(), DIAL_R
    ry = round(DIAL_R * Y_NUM / Y_DEN)
    circle(px, c, c, DIAL_R)
    ring = set(px)

    def along(k, inset):
        """Point on the ray of azimuth k, inset pixels inside the ring."""
        a, f = math.radians(k), (DIAL_R - inset) / DIAL_R
        return c + DIAL_R * f * math.sin(a), c - ry * f * math.cos(a)

    # Ticks every 30 degrees, longer at the cardinals. Each one starts on the
    # ring pixel nearest its ray, so it stays attached whatever the rounding.
    for k in range(0, 360, 30):
        ex, ey = along(k, 0)
        start = min(ring, key=lambda q: (q[0] - ex) ** 2 + (q[1] - ey) ** 2)
        ix, iy = along(k, 5 if k % 90 == 0 else 3)
        segment(px, *start, round(ix), round(iy))
    for k, ch in ((0, "N"), (90, "E"), (180, "S"), (270, "W")):
        x, y = along(k, 10)
        tiny(px, ch, round(x) - 1, round(y) - 2)
    for y in range(c - ry):                # north index, a filled triangle
        px.update((x, y) for x in range(c - y * 3 // 4, c + y * 3 // 4 + 1))
    circle(px, c, c, DIAL_RING, dotted=True)
    return pages(px, DIAL_W, DIAL_PAGES)


# Message icon: a small dial holding a bold question mark.
ICON_W = 23
ICON_PAGES = 4


def icon():
    px, cx, cy = set(), ICON_W // 2, 16
    circle(px, cx, cy, 11)
    for k in range(0, 360, 90):
        segment(px, *polar(cx, cy, 10, k), *polar(cx, cy, 8, k))
    glyph = BOLD[ord("?") - 33]
    for i in range(6):
        for j in range(7):
            if glyph[i] >> j & 1:
                px.add((cx - 3 + i, cy - 4 + j))
    return pages(px, ICON_W, ICON_PAGES)


a = Assets("GEONAV")
a.prefix("CFG_PROXY", proxy)
ui_size = 0
for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
    ui_size += len(value) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.table("T_DIR", ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                  "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"],
        stride=4)
a.u16("COS_Q1000", [round(1000.0 * math.cos(math.radians(deg)))
                      for deg in range(91)])
a.raw("DIAL", dial())
a.const("DIAL_R", DIAL_R)
a.const("DIAL_RING", DIAL_RING)
a.const("DIAL_W", DIAL_W)
a.const("DIAL_PAGES", DIAL_PAGES)
a.const("DIAL_Y_NUM", Y_NUM)
a.const("DIAL_Y_DEN", Y_DEN)
a.raw("ICON", icon())
a.const("ICON_W", ICON_W)
a.const("ICON_PAGES", ICON_PAGES)

if __name__ == "__main__":
    a.main()
