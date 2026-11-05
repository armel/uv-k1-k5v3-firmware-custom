#!/usr/bin/env python3
# Resistor read-only assets: texts, the IEC 60062 colour-code tables, the
# band layouts, the ohm and plus-minus signs and the resistor body art.
#
# Body art: BODY_PAGES frame-buffer pages (from page BODY_LINE) of 128 column
# bytes, LSB = top row. ART_BODY is the drawing (leads, outline, shading),
# ART_FILL the area a band covers: a band is drawn by clearing (or, when
# selected, setting) the ART_FILL bits of its columns.
#
#   ./gen_assets.py resistor_assets.bin resistor_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

W = 128
BODY_LINE, BODY_PAGES = 1, 4          # frame-buffer pages 1..4
BAND_W = 8

# Colour index = IEC 60062 colour code (black 0 .. white 9), then gold, silver.
# Names are the usual three-letter abbreviations.
COLOURS = ["BLK", "BRN", "RED", "ORG", "YEL", "GRN", "BLU", "VIO", "GRY", "WHT", "GLD", "SLV"]
GOLD, SILVER = 10, 11
NO_TOL = len(COLOURS)                 # virtual colour: no tolerance band (3 bands)

# Multiplier per colour, as printed under the band.
MULT = ["x1", "x10", "x100", "x1K", "x10K", "x100K", "x1M", "x10M", "x100M",
        "x1G", "x0.1", "x0.01"]
# Tolerance per colour (IEC 60062:2016), "" = not a tolerance colour.
TOL = ["", "1%", "2%", "0.05%", "0.02%", "0.5%", "0.25%", "0.1%", "0.01%", "",
       "5%", "10%", "20%"]
# Temperature coefficient per colour, ppm/K (IEC 60062:2016).
TC = ["250", "100", "50", "15", "25", "20", "10", "5", "1"]

# Band roles and the colours each one accepts (bit = colour index).
ROLE_D0, ROLE_D1, ROLE_D2, ROLE_MULT, ROLE_TOL, ROLE_TC = range(6)
def mask(colours): return sum(1 << c for c in colours)
MASKS = [mask(range(10))] * 3 + [mask(range(12)),
         mask(c for c in range(12) if TOL[c]),
         mask(range(len(TC)))]

# Band slots on the body (left x), as the Flipper app places them; the last
# two are set apart as on real parts.
SLOTS = [18, 34, 50, 66, 86, 102]
# Per band count (3..6): the role and the slot of each band, left to right.
LAYOUT = {3: [ROLE_D0, ROLE_D1, ROLE_MULT],
          4: [ROLE_D0, ROLE_D1, ROLE_MULT, ROLE_TOL],
          5: [ROLE_D0, ROLE_D1, ROLE_D2, ROLE_MULT, ROLE_TOL],
          6: [ROLE_D0, ROLE_D1, ROLE_D2, ROLE_MULT, ROLE_TOL, ROLE_TC]}
LAYOUT_SLOTS = {3: [0, 1, 2], 4: [0, 1, 2, 5], 5: [0, 1, 2, 3, 5],
                6: [0, 1, 2, 3, 4, 5]}

# First launch: 4 bands, 4.7 kohm 5 % (yellow violet red gold); the third
# digit and the temperature coefficient show up with 5 and 6 bands.
DEFAULT_TYPE = 4
DEFAULT_ROLES = [4, 7, 0, 2, GOLD, 1]

def cols(rows):
    """Picture rows ('#' = pixel) -> column bytes, LSB = top row."""
    return [sum(1 << r for r, row in enumerate(rows) if row[c] == "#")
            for c in range(len(rows[0]))]

# Ohm sign for the small bold font: 7 rows like its capitals.
OHM = cols(["..###..",
            ".##.##.",
            "##...##",
            "##...##",
            ".##.##.",
            "..#.#..",
            "###.###"])
# Plus-minus sign for the 3x5 font (which has none).
PLUS_MINUS = cols([".#.",
                   "###",
                   ".#.",
                   "...",
                   "###"])

# Resistor body, from the Flipper app's r3.png (MIT, Lewis Westbury): chamfered
# end caps with a 2 px outline, cylinder shading (dithered top and bottom,
# light in the middle) and "=" leads; two blank middle rows removed (24 of its
# 26 rows). Slots 0..2 carry its drawn bands, which every band count redraws.
BODY = [
    ".............#################....................................................................#################.............",
    "...........#######################............................................................#######################...........",
    "..........#####.#.#......##.#######################################################################.#.#.#.#.#.#.#.####..........",
    ".........###.#.#.##......#.#.#.#.###############################################################.#.#.#.#.#.#.#.#.#.####.........",
    "........###.#.#.#.#......##.#.#.#.#......##.#.#.#.#......##.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.####........",
    "........##.#.#....#......#...#.#.##......#.#.#.#.##......#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#...............#.###........",
    ".......####.#.....#......#........#......##.#.#.#.#......##.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.....................#.###.......",
    "......###.#.......#......#........#......#........#......#............................................................#.##......",
    "....#####.#.......#......#........#......#........#......#............................................................#.####....",
    "########.#........#......#........#......#........#......#.............................................................#.#######",
    "....###.#.#.......#......#........#......#........#......#............................................................#.####....",
    "########.#........#......#........#......#........#......#.............................................................#.#######",
    "#########.#.......#......#........#......#........#......#............................................................#.########",
    "....####.#.#......#......#........#......#........#......#...........................................................#.#####....",
    "......###.#.#.....#......#..#.#.#.#......##.#.#.#.#......##.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.............#.#.#.##......",
    "......####.#.#.#.##......#.#.#.#.##......#.#.#.#.##......#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.###......",
    ".......##.#.#.#.#.#......##.#.#.#.#......##.#.#.#.#......##.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.###.......",
    ".......###.#.#.#.##......#.#.#.#.##......#.#.#.#.##......#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.####.......",
    "........###.#.#.#.#......##.#.#.#.#......##.#.#.#.#......##.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.##........",
    "........####.#.#.##......#.#.#.#.##......#.#.#.#.##......#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.###........",
    ".........####.#.#.#......##.#.#.###############################################################.#.#.#.#.#.#.#.#.#.#.###.........",
    "..........####.#.##......#.#.#######################################################################.#.#.#.#.#.#.#####..........",
    "...........#######################............................................................#######################...........",
    ".............#################....................................................................#################.............",
]
BODY_TOP = 6                           # first body row in the art (frame-buffer y 14)
assert len(BODY) == 24 and all(len(r) == W for r in BODY)
assert BODY_TOP + len(BODY) <= BODY_PAGES * 8

def body_pixels():
    """(drawing, band area) pixel sets in art coordinates. A band covers, in
    each of its columns, what lies inside the 2 px outline."""
    drawing = {(x, y + BODY_TOP) for y, r in enumerate(BODY) for x, ch in enumerate(r) if ch == "#"}
    area = set()
    for s in SLOTS:
        for x in range(s, s + BAND_W):
            rows = [y for y, r in enumerate(BODY) if r[x] == "#"]
            area |= {(x, y + BODY_TOP) for y in range(rows[0] + 2, rows[-1] - 1)}
    return drawing, area

def pages(px):
    return bytes(sum(1 << b for b in range(8) if (x, p * 8 + b) in px)
                 for p in range(BODY_PAGES) for x in range(W))

def build():
    a = Assets("RESISTOR")
    a.text("T_TITLE", "RESISTOR")
    a.text("T_BANDS", " BANDS")
    a.text("T_PPM", "PPM")
    a.text("T_F", "F")
    a.table("T_COLOUR", COLOURS)
    a.table("T_MULT", MULT)
    a.table("T_TOLV", TOL)
    a.table("T_TCV", TC)
    a.u16("MASKS", MASKS)
    a.u8("LAYOUT", sum((LAYOUT[n] + [0] * (6 - n) for n in range(3, 7)), []))
    a.u8("LAYOUT_X", sum(([SLOTS[s] for s in LAYOUT_SLOTS[n]] + [0] * (6 - n)
                          for n in range(3, 7)), []))
    a.u8("DEFAULTS", [DEFAULT_TYPE] + DEFAULT_ROLES)
    a.u8("OHM", OHM)
    a.u8("PLUS_MINUS", PLUS_MINUS)
    drawing, area = body_pixels()
    a.raw("ART_BODY", pages(drawing))
    a.raw("ART_FILL", pages(area))
    a.const("BODY_LINE", BODY_LINE)
    a.const("BODY_PAGES", BODY_PAGES)
    a.const("BAND_W", BAND_W)
    a.const("NO_TOL", NO_TOL)
    a.const("ROLE_D2", ROLE_D2)
    a.const("ROLE_MULT", ROLE_MULT)
    a.const("ROLE_TOL", ROLE_TOL)
    a.const("ROLE_TC", ROLE_TC)
    a.const("ROLES", 6)
    a.const("COLOURS", len(COLOURS))
    a.const("CODE_CHARS", len(COLOURS[0]))
    return a

if __name__ == "__main__":
    build().main()
