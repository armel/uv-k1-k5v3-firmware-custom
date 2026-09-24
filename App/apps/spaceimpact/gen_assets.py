#!/usr/bin/env python3
# Space Impact read-only assets: texts, tables, the sprite sheet (column-major,
# LSB = top row, read by blit() at draw time) and the 128x64 title screen as 8
# LCD pages of 128 bytes (page 0 = status line, pages 1..7 = frame buffer;
# page 7 stays blank for the blinking prompt).
#
#   ./gen_assets.py spaceimpact_assets.bin spaceimpact_assets.h [preview.pgm]
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets
from app_art import Canvas

SPRITES = [
    ("SPR_SHIP",  [0x41,0x6B,0x7F,0x3E,0x3E,0x1C,0x1C,0x1C,0x08,0x08]),
    ("SPR_ENEMY", [0x4C,0x32,0x15,0x21,0x21,0x15,0x32,0x4C,     # invader  (+ type * 8)
                   0x08,0x14,0x14,0x14,0x22,0x41,0x49,0x77,     # jet
                   0x0C,0x56,0x3D,0x15,0x15,0x3D,0x56,0x0C,     # saucer
                   0x0E,0x11,0x6D,0x41,0x41,0x6D,0x11,0x0E]),   # skull
    ("SPR_BOOM",  [0x00,0x00,0x08,0x1C,0x08,0x00,0x00,0x00,     # + frame * 8
                   0x41,0x04,0x10,0x40,0x02,0x20,0x08,0x41]),
    ("SPR_BOSS",  [0x00,0x78,0x84,0x02,0x32,0x31,0x01,0x01,0x01,0x01,0x31,0x32,0x02,0x84,0x78,0x00,   # octopus
                   0x30,0x0C,0x82,0x71,0x0D,0x03,0xE0,0x1E,0x02,0x00,0x03,0x0D,0x31,0xC2,0x0C,0x30,
                   0xC0,0x20,0x10,0xDC,0xD2,0x12,0x1D,0xD1,0xD1,0x1D,0x12,0xD2,0xDC,0x10,0x20,0xC0,   # mothership (+32)
                   0x20,0x11,0x0A,0x06,0x02,0x02,0x02,0x3E,0x3E,0x02,0x02,0x02,0x06,0x0A,0x11,0x20]),
    ("SPR_HEART", [0x06,0x0F,0x1E,0x0F,0x06]),
    ("SPR_MISS",  [0x1B,0x0E,0x04,0x0E,0x0E,0x0E,0x0E,0x04]),   # 8x5, also the HUD stock icon
]

def title_screen(sprites):
    cv = Canvas()
    cv.two_words("SPACE", "IMPACT")
    # Scene under the logo: the ship firing at an invader wave.
    ship, enemy = sprites["SPR_SHIP"], sprites["SPR_ENEMY"]
    cv.sprite(6, 46, ship)
    for x in (18, 26, 34):                         # gun burst
        for k in range(4):
            cv.set(x + k, 49)
    for i, ex in enumerate((52, 68, 84, 100)):
        cv.sprite(ex, 45 + (i & 1) * 3, enemy[8 * (i & 3):8 * (i & 3) + 8])
    return cv

def main():
    preview_path = sys.argv.pop(3) if len(sys.argv) == 4 else None
    a = Assets("SPACEIMPACT")
    a.text("T_GAME_OVER", "GAME OVER")
    a.text("T_PAUSE", "PAUSE")
    a.text("T_PRESS", "PRESS MENU")
    a.text("T_LEVEL", "LEVEL 1")         # digit patched at run time
    a.u16("PLACE", [10000, 1000, 100, 10, 1])
    a.u8("POINTS", [10, 15, 40, 25])     # score per enemy type
    a.u8("SPEED", [1, 2, 1, 1])          # pixels per tick per enemy type
    for name, cols in SPRITES:
        a.u8(name, cols)
    cv = title_screen(dict(SPRITES))
    a.raw("ART_TITLE", cv.pages())
    a.main()
    if preview_path:
        cv.preview(preview_path)

if __name__ == "__main__":
    main()
