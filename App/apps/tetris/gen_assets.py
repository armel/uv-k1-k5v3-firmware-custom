#!/usr/bin/env python3
# Tetris read-only assets: texts, piece masks, scoring, decimal places, the
# rotation wall kicks and the 128x64 title screen.
#
#   ./gen_assets.py tetris_assets.bin tetris_assets.h [preview.pgm]
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets
from app_art import Canvas

MASKS = [0x00F0, 0x4444, 0x0F00, 0x2222,
         0x0066, 0x0066, 0x0066, 0x0066,
         0x0072, 0x0262, 0x0270, 0x0232,
         0x0036, 0x0462, 0x0360, 0x0231,
         0x0063, 0x0264, 0x0630, 0x0132,
         0x0071, 0x0226, 0x0470, 0x0322,
         0x0074, 0x0622, 0x0170, 0x0223]

def title_cell(cv, x, y):
    """Bevelled 4x4 block on a 5 px grid: larger than in game, to read at a glance."""
    for k in range(4):
        cv.set(x + k, y); cv.set(x + k, y + 3); cv.set(x, y + k); cv.set(x + 3, y + k)
    cv.rect(x + 1, y + 1, x + 2, y + 2)

def title_screen():
    """Wide logo over a stack with a gap, and a T piece coming down into it."""
    cv = Canvas()
    cv.word("TETRIS", 2, kx=1.45, ky=1.2)
    floor, x0 = 55, 4
    stack = ["111111111111101111111111",       # bottom row first, 5 px cells
             "111110111111100111111101",
             "001100011110000011100100"]
    for r, row in enumerate(stack):
        for c, cell in enumerate(row):
            if cell == "1":
                title_cell(cv, x0 + c * 5, floor - 4 - 5 * r)
    t_down = 0x0027                             # T pointing down, over the gap
    for i in range(16):
        if t_down >> i & 1:
            title_cell(cv, x0 + (12 + (i & 3)) * 5, floor - 4 - 25 + (i >> 2) * 5)
    return cv

preview_path = sys.argv.pop(3) if len(sys.argv) == 4 else None
a = Assets("TETRIS")
a.text("T_TETRIS", "TETRIS")
a.text("T_NEXT", "NEXT")
a.text("T_NEW_BEST", "NEW BEST!")
a.text("T_GAME_OVER", "GAME OVER")
a.text("T_PAUSE", "PAUSE")
a.text("T_SCORE", "SCORE")
a.text("T_LINES", "LINES")
a.text("T_LEVEL", "LEVEL")
a.text("T_BEST", "BEST")
a.text("T_PRESS", "PRESS MENU")
# Four 4x4 masks (one per rotation) for I, O, T, S, Z, J and L.
a.u16("MASK", MASKS)
a.u16("LINE_POINTS", [0, 100, 300, 500, 800])                  # by lines cleared
a.u32("DECIMAL_PLACE", [100000, 10000, 1000, 100, 10, 1])
a.i8("KICK", [0, -1, 1, -2, 2])                                # rotation x offsets
title = title_screen()
a.raw("ART_TITLE", title.pages())
a.main()
if preview_path:
    title.preview(preview_path)
