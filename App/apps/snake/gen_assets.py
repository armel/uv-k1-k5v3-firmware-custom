#!/usr/bin/env python3
# Snake read-only assets: texts, decimal places, direction steps, the 4x4
# sprites (16-bit masks, bit i = pixel (i & 3, i >> 2)) and the 128x64 title
# screen. The four sprite sets are contiguous: the app loads them onto its
# stack in one read per frame.
#
#   ./gen_assets.py snake_assets.bin snake_assets.h [preview.pgm]
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets
from app_art import Canvas

HEAD      = [0x6566, 0x0FD2, 0x66A6, 0x0FB4]            # by direction: up, right, down, left
HEAD_OPEN = [0x65A9, 0xC35A, 0x95A6, 0x3CA5]            # facing the food
BODY = [0, 0, 0, 0x0CA6, 0, 0x6426, 0x6AC0, 0,          # by neighbour mask:
        0, 0x0356, 0x0DB0, 0, 0x6530, 0, 0, 0]          # up 1, right 2, down 4, left 8
TAIL = [0, 0x0466, 0x0CE0, 0, 0x6620, 0, 0, 0,
        0x0370, 0, 0, 0, 0, 0, 0, 0]
FOOD = 0x6996
STEP = {"U": (0, -1), "R": (1, 0), "D": (0, 1), "L": (-1, 0)}
NEIGHBOUR = {(0, -1): 1, (1, 0): 2, (0, 1): 4, (-1, 0): 8}
HEADING = {(0, -1): 0, (1, 0): 1, (0, 1): 2, (-1, 0): 3}

def cell(cv, mask, gx, gy):
    """One 4x4 sprite on the game's board grid (as draw_sprite())."""
    for i in range(16):
        if mask >> i & 1:
            cv.set(2 + gx * 4 + (i & 3), 10 + gy * 4 + (i >> 2))

def title_screen():
    """The logo inside the board frame; a snake at game scale heads for food."""
    cv = Canvas()
    for x in range(1, 127):                     # board frame, as render()
        cv.set(x, 1); cv.set(x, 54)
    for y in range(1, 55):
        cv.set(1, y); cv.set(126, y)
    cv.word("SNAKE", 6, kx=1.3, ky=1.1)
    x, y = 2, 8                                 # tail cell, then the moves
    cells = [(x, y)]
    for move, count in (("R", 8), ("U", 2), ("R", 6), ("D", 2), ("R", 8)):
        for _ in range(count):
            x, y = x + STEP[move][0], y + STEP[move][1]
            cells.append((x, y))
    for k, (cx, cy) in enumerate(cells):
        if k == len(cells) - 1:                 # head, mouth open towards the food
            px, py = cells[k - 1]
            mask = HEAD_OPEN[HEADING[(cx - px, cy - py)]]
        elif k == 0:
            nx, ny = cells[1]
            mask = TAIL[NEIGHBOUR[(nx - cx, ny - cy)]]
        else:
            (px, py), (nx, ny) = cells[k - 1], cells[k + 1]
            mask = BODY[NEIGHBOUR[(nx - cx, ny - cy)] | NEIGHBOUR[(px - cx, py - cy)]]
        cell(cv, mask, cx, cy)
    cell(cv, FOOD, 28, 8)
    return cv

preview_path = sys.argv.pop(3) if len(sys.argv) == 4 else None
a = Assets("SNAKE")
a.text("T_SCORE", "SCORE:")
a.text("T_BEST", "BEST:")
a.text("T_NEW_SCORE", "NEW SCORE")
a.text("T_GAME_OVER", "GAME OVER")
a.text("T_PAUSE", "PAUSE")
a.text("T_PRESS", "PRESS MENU")
a.u16("PLACE", [10000, 1000, 100, 10])
a.i8("DXY", [0, -1,  1, 0,  0, 1,  -1, 0])   # (dx, dy) for up, right, down, left
# Direction bit from a neighbour's packed (dx + 2 * dy) & 7 offset.
a.u8("DIR_MASK", [0, 2, 4, 0, 0, 0, 1, 8])
a.u16("SPR_HEAD", HEAD)
a.u16("SPR_HEAD_OPEN", HEAD_OPEN)
a.u16("SPR_BODY", BODY)
a.u16("SPR_TAIL", TAIL)
title = title_screen()
a.raw("ART_TITLE", title.pages())
a.main()
if preview_path:
    title.preview(preview_path)
