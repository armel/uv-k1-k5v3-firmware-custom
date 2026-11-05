#!/usr/bin/env python3
# Hell Maze read-only assets: the sine quadrant, the level map and the start.
#
# SIN_Q   ANG_Q + 1 Q14 sines over one quadrant (0 .. 90 degrees inclusive);
#         symmetry gives the full ANG_FULL-step circle.
# MAP     MAP_W x MAP_H tiles, row by row, two per byte (even tile in the low
#         nibble): 0 = floor, 1..3 = wall type (1 brick, 2 stone, 3 pillar).
#         The border must be walls.
# START   x, y (Q8 tile units, u16) and the heading (0 .. ANG_FULL - 1, u16).
# FOES    x, y (Q8, u16) of each demon, FOE_N of them.
# SPR_IMP 3 frames (walk 0, walk 1, dead) and SPR_GUN 2 frames (idle, firing):
#         per row an ink u16 then an opaque-mask u16, bit 15 = leftmost pixel.
#         '#' black, 'o' white, '.' transparent.
#
# Built with Armel F4HWN's shared asset builder (../app_assets.py). The maze,
# the demon and the gun are original drawings.
#
#   ./gen_assets.py hellmaze_assets.bin hellmaze_assets.h
import math, os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

ANG_FULL = 512          # one turn
ANG_Q = ANG_FULL // 4

LEVEL = [
    "1111111111111111",
    "1..............1",
    "1.22222..3..3..1",
    "1.2...2........1",
    "1.2...2..3..3..1",
    "1.22.22........1",
    "1..............1",
    "1111.111112..111",
    "1..............1",
    "1..3.....222...1",
    "1........2.2...1",
    "1..3.....2.....1",
    "1........2222..1",
    "1.11111........1",
    "1......3.......1",
    "1111111111111111",
]

MAP_W = len(LEVEL[0])
MAP_H = len(LEVEL)
assert MAP_W == 16 and MAP_H == 16 and all(len(r) == MAP_W for r in LEVEL)
for y, row in enumerate(LEVEL):
    for x, c in enumerate(row):
        if x in (0, MAP_W - 1) or y in (0, MAP_H - 1):
            assert c != ".", f"open border at {x},{y}"

FOES = [(4, 3), (12, 1), (8, 6), (13, 9), (5, 10), (11, 13), (2, 14)]
for x, y in FOES:
    assert LEVEL[y][x] == ".", f"foe in a wall at {x},{y}"

IMP_TOP = [
    "...#........#...",
    "...##......##...",
    "....########....",
    "...##########...",
    "...#oo####oo#...",
    "...##########...",
    "....#o#oo#o#....",
    ".....######.....",
    "..############..",
    ".##.########.##.",
    ".#..########..#.",
]
IMP = [
    IMP_TOP + [
        "....###..###....",
        "....##....##....",
        "....##....##....",
        "...###....###...",
        "..####....####..",
    ],
    IMP_TOP + [
        "....###..###....",
        "...##......##...",
        "..##........##..",
        ".###........###.",
        "................",
    ],
    ["................"] * 12 + [
        "......#..#......",
        "...##########...",
        ".##o########o##.",
        "################",
    ],
]
GUN_BODY = [
    "......####......",
    "......####......",
    "......####......",
    ".....######.....",
    ".....##oo##.....",
    "....##oooo##....",
    "...##oooooo##...",
    "..#oooooooooo#..",
    ".#oooooooooooo#.",
    "#oooooooooooooo#",
]
GUN = [
    ["................"] * 4 + GUN_BODY,
    [".....#.##.#.....",
     "......####......",
     "....########....",
     ".....######.....",
    ] + GUN_BODY,
]

def sprite(frames, rows):
    out = []
    for f in frames:
        assert len(f) == rows and all(len(r) == 16 for r in f)
        for r in f:
            ink = sum(1 << (15 - i) for i, c in enumerate(r) if c == "#")
            mask = sum(1 << (15 - i) for i, c in enumerate(r) if c != ".")
            out += [ink, mask]
    return out

tiles = [0 if c == "." else int(c) for row in LEVEL for c in row]
start = (int(1.5 * 256), int(1.5 * 256), 0)
assert LEVEL[start[1] >> 8][start[0] >> 8] == "."

a = Assets("HELLMAZE")
a.text("T_TITLE", "HELL MAZE")
a.text("T_VER", os.environ.get("APP_VER", "dev"))
a.text("T_DIED", "YOU DIED")
a.text("T_CLEAR", "MAZE CLEARED")
a.text("T_AGAIN", "5: PLAY AGAIN")
# Help page (MENU): per entry a key, drawn in a capsule, and at HELP_ACT its
# action next to it; the left column (lines 2 to 6), then the right one.
HELP_ACT = 5
# A leading space centres a one-digit key in its capsule.
HELP = [(" 2", "FORWARD"), (" 8", "BACK"), (" 4", "TURN LEFT"), (" 6", "TURN RIGHT"),
        (" 1", "STRAFE L"), (" 3", "STRAFE R"), ("PTT", "FIRE"), (" 5", "FIRE"),
        ("MENU", "RESUME"), ("EXIT", "QUIT")]
assert all(len(k) < HELP_ACT for k, _ in HELP)
a.table("T_HELP", [k.ljust(HELP_ACT, "\0") + v for k, v in HELP])
a.const("HELP_ACT", HELP_ACT)
a.i16("SIN_Q", [round(16384 * math.sin(math.pi / 2 * i / ANG_Q)) for i in range(ANG_Q + 1)])
a.u8("MAP", [tiles[i] | tiles[i + 1] << 4 for i in range(0, len(tiles), 2)])
# START and FOES are adjacent: the app reads both in one go.
a.u16("START", list(start))
a.u16("FOES", [v * 256 + 128 for xy in FOES for v in xy])
a.u16("SPR_IMP", sprite(IMP, 16))
a.u16("SPR_GUN", sprite(GUN, 14))
a.const("FOE_N", len(FOES))
a.const("IMP_ROWS", 16)
a.const("GUN_ROWS", 14)
a.const("ANG_FULL", ANG_FULL)
a.const("MAP_W", MAP_W)
a.const("MAP_H", MAP_H)
a.main()
