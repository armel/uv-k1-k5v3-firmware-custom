#!/usr/bin/env python3
# Breakout read-only assets: texts, the brick animation patterns and the
# 128x64 title screen.
#
#   ./gen_assets.py breakout_assets.bin breakout_assets.h [preview.pgm]
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets
from app_art import Canvas

BRICK_ANIM = [0b00110001, 0b00101001, 0b00100101, 0b00100011]

def brick(cv, x, y):
    """A 15 px brick as drawn by the game: end caps and the animated fill."""
    for k in range(15):
        col = 0b00011110 if k in (0, 14) else BRICK_ANIM[(k - 1) % 4]
        for bit in range(8):
            if col >> bit & 1:
                cv.set(x + k, y + bit)

def title_screen():
    """BREAK / OUT, a few bricks, the racket and the ball with its trail."""
    cv = Canvas()
    cv.two_words("BREAK", "OUT")
    for x in (2, 22, 86, 106):
        brick(cv, x, 42)
    for k in range(24):                         # racket, as renderRacket()
        cv.set(52 + k, 54)
        if 0 < k < 23:
            cv.set(52 + k, 53); cv.set(52 + k, 55)
    cv.rect(62, 47, 64, 49)                     # ball, as renderBall()
    cv.set(61, 48); cv.set(65, 48)
    for i in range(0, 8, 2):                    # dotted trail
        cv.set(58 - i * 2, 47 - i // 2)
    return cv

preview_path = sys.argv.pop(3) if len(sys.argv) == 4 else None

a = Assets("BREAKOUT")
a.text("T_LEVEL", "Level ")
a.text("T_BALL", "Ball ")
a.text("T_SCORE", "Score ")
a.text("T_GAME_OVER", "GAME OVER")
a.text("T_PAUSE", "PAUSE")
a.text("T_HELLO", "OVL HELLO")   # APP_POC_HELLO bisection build only
a.text("T_PRESS", "PRESS MENU")
a.u8("BRICK_ANIM", BRICK_ANIM)
title = title_screen()
a.raw("ART_TITLE", title.pages())
a.main()
if preview_path:
    title.preview(preview_path)
