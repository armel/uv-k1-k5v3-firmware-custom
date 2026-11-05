#!/usr/bin/env python3
# Minesweeper read-only assets: texts, the tile sheet, the status-line
# smileys, the cursor steps of keys 1..9, the board background and the
# 128x64 title screen.
#
# Tile sheet: one record of TILE_W column bytes per kind (LSB = top row),
# already placed inside the tile: column i is x + 1 + i and bit b is row
# y + b of the tile's frame-buffer page. render() ORs a record in with no
# shift, and loads the whole sheet onto its stack once per frame.
#
#   ./gen_assets.py minesweeper_assets.bin minesweeper_assets.h [preview.pgm]
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets
from app_art import Canvas, W

COLS, ROWS, TILE = 15, 6, 8   # the board, as in minesweeper_app.c
FB_PAGES = 7                  # frame-buffer pages under the status line
TILE_W = 7

def cols(rows):
    """Picture rows ('#' = pixel) -> column bytes, LSB = top row."""
    return [sum(1 << r for r, row in enumerate(rows) if row[c] == "#")
            for c in range(len(rows[0]))]

DIGITS = [[0x02, 0x1F, 0x00], [0x19, 0x15, 0x12],   # gFont3x5 '1' .. '8', the
          [0x11, 0x15, 0x0A], [0x07, 0x04, 0x1F],   # glyphs print_tiny draws
          [0x17, 0x15, 0x09], [0x1E, 0x15, 0x1D],
          [0x19, 0x05, 0x03], [0x1F, 0x15, 0x1F]]
FLAG = cols([".##..",                               # pennant on a pole: unlike
             ".###.",                               # the digit 1 next to it
             ".####",
             ".#...",
             "###.."])
MINE = cols(["...#...",                             # round, four horns and a
             "..###..",                             # highlight, as the title's
             ".#.###.",                             # big mine
             "#######",
             ".#####.",
             "..###..",
             "...#..."])
BOOM = [cols(["...#...",                            # the mine that went off: a
              ".#.#.#.",                            # white star on its black
              "..###..",                            # cell, big then small (two
              "#######",                            # frames)
              "..###..",
              ".#.#.#.",
              "...#..."]),
        cols([".......",
              "...#...",
              "..###..",
              ".#####.",
              "..###..",
              "...#...",
              "......."])]
CURSOR = cols(["##...##",                           # corner brackets
               "#.....#",
               ".......",
               ".......",
               ".......",
               "#.....#",
               "##...##"])
FACES = [[0x00, 0x10, 0x22, 0x20, 0x20, 0x22, 0x10, 0x00],   # playing
         [0x00, 0x00, 0x32, 0x48, 0x48, 0x32, 0x00, 0x00],   # STAR held
         [0x02, 0x16, 0x26, 0x22, 0x22, 0x26, 0x16, 0x02],   # won
         [0x05, 0x22, 0x15, 0x10, 0x10, 0x15, 0x22, 0x05]]   # lost

def interior(sprite, dx, dy):
    """A sprite drawn at (x + 1 + dx, y + dy) of its tile, as a sheet record."""
    out = [0] * TILE_W
    for i, c in enumerate(sprite):
        if c << dy > 0xFF:
            raise SystemExit("tile sprite crosses the tile's page")
        out[dx + i] = c << dy
    return out

TILES = [[0] * TILE_W]                                  # 0: revealed, no mine around
TILES += [interior(d, 2, 2) for d in DIGITS]            # 1..8: as print_tiny at (x + 3, y + 2)
K_FLAG, K_MINE, K_CURSOR, K_BOOM = range(len(TILES), len(TILES) + 4)
TILES += [interior(FLAG, 1, 2),                         # 5x5 at (x + 2, y + 2)
          interior(MINE, 0, 1), interior(CURSOR, 0, 1)] # 7x7 at (x + 1, y + 1)
TILES += [interior(b, 0, 1) for b in BOOM]              # K_BOOM, K_BOOM + 1

# The board in frame-buffer coordinates: it stays on page boundaries (tile row
# = page) right under the status line, BOARD_X px from the left. Its soft
# shadow is a 50 % dither SHADOW_R px wide right of the board and SHADOW_B px
# under it, SHADOW_GAP px away from it all along, as cast by a raised board:
# each band starts with a 45° cut in line with a corner of the board (top
# right, bottom left).
RIGHT, BOTTOM = COLS * TILE, ROWS * TILE
BOARD_X, SHADOW_GAP, SHADOW_R, SHADOW_B = 1, 1, 4, 3
assert BOARD_X + RIGHT + SHADOW_GAP + SHADOW_R < W
assert BOTTOM + SHADOW_GAP + SHADOW_B < FB_PAGES * 8

def board_background():
    """The board outline and its soft shadow, as the frame-buffer pages
    render() copies in place of a clear before drawing the tiles."""
    px = set()
    left, right = BOARD_X, BOARD_X + RIGHT
    for x in range(left, right + 1):                     # the outline, as draw_rect
        px.add((x, 0)); px.add((x, BOTTOM))
    for y in range(BOTTOM + 1):
        px.add((left, y)); px.add((right, y))
    edge_x, edge_y = right + SHADOW_GAP, BOTTOM + SHADOW_GAP   # last blank column / row
    for y in range(edge_y + SHADOW_B + 1):               # the shadow, off the board
        for x in range(left, edge_x + SHADOW_R + 1):
            side = x > edge_x and y >= x - right         # right band, cut in line with (right, 0)
            under = y > edge_y and x >= left + y - BOTTOM  # bottom band, in line with (left, BOTTOM)
            if (side or under) and (x + y) % 2 == 0:
                px.add((x, y))
    return bytes(sum(1 << b for b in range(8) if (x, page * 8 + b) in px)
                 for page in range(FB_PAGES) for x in range(W))

# Cursor step (dx, dy) of keys 1..9: the keypad as a compass, 5 stays put.
DIRS = [-1, -1,   0, -1,   1, -1,
        -1,  0,   0,  0,   1,  0,
        -1,  1,   0,  1,   1,  1]

def tile(cv, kind, x, y, hidden):
    """A tile as render() draws it, its top-left corner at screen (x, y)."""
    if hidden:
        for k in range(TILE + 1):
            cv.set(x + k, y); cv.set(x + k, y + TILE); cv.set(x, y + k); cv.set(x + TILE, y + k)
    for i, c in enumerate(TILES[kind]):
        for bit in range(8):
            if c >> bit & 1:
                cv.set(x + 1 + i, y + bit)

def big_mine(cv, cx, cy):
    """Round sea mine: an 11 px disc, eight horns and a highlight."""
    for y in range(-5, 6):
        for x in range(-5, 6):
            if x * x + y * y <= 30:          # r * r + r: a round pixel disc
                cv.set(cx + x, cy + y)
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
        for k in (6, 7):
            cv.set(cx + dx * k, cy + dy * k)
    for dx, dy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):   # short, off the corners
        cv.set(cx + dx * 4, cy + dy * 4)
    for x, y in ((-2, -3), (-3, -2), (-2, -2)):
        cv.set(cx + x, cy + y, 0)

def big_flag(cv, x, y):
    """A 14 px flag: pole, pennant pointing right, stepped base."""
    for k in range(13):
        cv.set(x + 3, y + k)
    for r, w in enumerate([3, 5, 7, 9, 7, 5, 3]):
        for k in range(1, w + 1):
            cv.set(x + 3 + k, y + r)
    for k in range(1, 6):
        cv.set(x + k, y + 12)
    for k in range(7):
        cv.set(x + k, y + 13)

def title_screen():
    """MINE / SWEEPER between a big mine and a big flag, over a row of
    tiles as the game draws them (screen y = frame-buffer y + 8)."""
    cv = Canvas()
    cv.two_words("MINE", "SWEEPER")
    big_mine(cv, 16, 10)
    big_flag(cv, 101, 3)
    for i, t in enumerate("HHHF1..12FHHHHH"):   # Hidden, Flag, '.' empty, digits
        kind = K_FLAG if t == "F" else int(t) if t.isdigit() else 0
        tile(cv, kind, 4 + i * 8, 46, t in "HF")
    return cv

preview_path = sys.argv.pop(3) if len(sys.argv) == 4 else None
a = Assets("MINESWEEPER")
a.text("T_FLAGS", "FLAGS LEFT:")
a.text("T_WIN", "YOU WIN!")
a.text("T_LOST", "YOU LOST!")
a.text("T_PRESS", "PRESS MENU")
a.u8("TILES", sum(TILES, []))
a.const("TILE_W", TILE_W)
a.const("K_FLAG", K_FLAG)
a.const("K_MINE", K_MINE)
a.const("K_CURSOR", K_CURSOR)
a.const("K_BOOM", K_BOOM)
a.u8("FACE", sum(FACES, []))
a.const("FACE_W", len(FACES[0]))
for i, name in enumerate(("FACE_PLAY", "FACE_SCARED", "FACE_WON", "FACE_LOST")):
    a.const(name, i)
a.i8("DIRS", DIRS)
a.raw("ART_BOARD", board_background())
a.const("BOARD_X", BOARD_X)
title = title_screen()
a.raw("ART_TITLE", title.pages())
a.main()
if preview_path:
    title.preview(preview_path)
