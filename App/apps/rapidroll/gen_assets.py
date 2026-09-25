#!/usr/bin/env python3
# Rapid Roll read-only assets: texts, decimal places, the sprite sheet
# (column-major, LSB = top row, read by blit() at draw time) and the 128x64
# title screen.
#
#   ./gen_assets.py rapidroll_assets.bin rapidroll_assets.h [preview.pgm]
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets
from app_art import Canvas

SPRITES = [
    ("SPR_BALL",   [0x3C,0x42,0x8D,0x85,0x81,0x81,0x42,0x3C,    # 4 frames: a mark turning
                    0x3C,0x42,0x81,0x81,0x85,0x8D,0x42,0x3C,    # as the ball rolls
                    0x3C,0x42,0x81,0x81,0xA1,0xB1,0x42,0x3C,
                    0x3C,0x42,0xB1,0xA1,0x81,0x81,0x42,0x3C]),
    ("SPR_HEART",  [0x1E,0x21,0x45,0x82,0x82,0x41,0x21,0x1E]),  # 8x8, the size of the ball
    ("SPR_LIFE",   [0x06,0x0F,0x1E,0x0F,0x06]),
    ("SPR_BOOM",   [0x00,0x52,0x34,0x08,0x34,0x4A,0x08,0x00]),
    ("SPR_TOOTH",  [0x03,0x07,0x0F,0x07,0x03,0x01,0x01]),
    ("SPR_SPIKE",  [0x0C,0x0E,0x0F,0x0E,0x0C,0x08,0x08]),       # 3-row tooth on a 1-row floor, drawn from y - 3
    ("SPR_SQUASH", [0x0C,0x12,0x21,0x21,0x21,0x21,0x12,0x0C]),  # 8x6 ball, just landed
]

def pill(cv, x, y, w):
    """A safe platform as drawn by the game: hollow 3-row pill, rounded ends."""
    for k in range(w):
        v = 0x02 if k in (0, w - 1) else 0x07 if k in (1, w - 2) else 0x05
        for bit in range(3):
            if v >> bit & 1:
                cv.set(x + k, y + bit)

def title_screen(sprites):
    """RAPID / ROLL, the ball on a platform, spikes and a heart to grab."""
    cv = Canvas()
    cv.two_words("RAPID", "ROLL")
    pill(cv, 4, 53, 28)
    cv.sprite(14, 45, sprites["SPR_BALL"][:8])
    spike = sprites["SPR_SPIKE"]
    cv.sprite(48, 50, [spike[k % 7] for k in range(26)])   # spiked floor, from y - 3
    pill(cv, 92, 49, 28)
    cv.sprite(100, 41, sprites["SPR_HEART"])
    return cv

preview_path = sys.argv.pop(3) if len(sys.argv) == 4 else None
a = Assets("RAPIDROLL")
a.text("T_LV", "LV")
a.text("T_GAME_OVER", "GAME OVER")
a.text("T_PAUSE", "PAUSE")
a.text("T_LEVEL1", "LEVEL 1")
a.text("T_PRESS", "PRESS MENU")
a.u16("PLACE", [10000, 1000, 100, 10, 1])
# New-game values of the app's struct globals from `mode` on, in their order
# (the app checks the length with a static assert): mode (ST_PLAY = 0),
# paused, lives, level, riseAcc, respawnCd, lastX, nextX, spikeAllowed,
# alive, on (platform 0), then the tick countdowns boom, banner, wait, invul
# and squash.
a.u8("NEW_GAME", [0, 0, 3, 1, 0, 0, 50, 50, 0, 1, 0, 0, 50, 40, 0, 0])
for name, cols in SPRITES:
    a.u8(name, cols)
title = title_screen(dict(SPRITES))
a.raw("ART_TITLE", title.pages())
a.main()
if preview_path:
    title.preview(preview_path)
