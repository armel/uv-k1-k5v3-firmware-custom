#!/usr/bin/env python3
# Plasma read-only assets: the sine wave, the metaball falloff, the ordered-dither
# matrix, the scene presets and the HUD texts. The app copies the tables onto its
# stack at launch, so the render loop reads them from RAM at full speed while
# they stay out of the overlay.
#
#   ./gen_assets.py plasma_assets.bin plasma_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

# 32 * sin(2*pi*i/256): four of these sum to -128..128.
SIN = [
      0,   1,   2,   2,   3,   4,   5,   5,   6,   7,   8,   9,   9,  10,  11,  12,
     12,  13,  14,  14,  15,  16,  16,  17,  18,  18,  19,  20,  20,  21,  21,  22,
     23,  23,  24,  24,  25,  25,  26,  26,  27,  27,  27,  28,  28,  29,  29,  29,
     30,  30,  30,  30,  31,  31,  31,  31,  31,  32,  32,  32,  32,  32,  32,  32,
     32,  32,  32,  32,  32,  32,  32,  32,  31,  31,  31,  31,  31,  30,  30,  30,
     30,  29,  29,  29,  28,  28,  27,  27,  27,  26,  26,  25,  25,  24,  24,  23,
     23,  22,  21,  21,  20,  20,  19,  18,  18,  17,  16,  16,  15,  14,  14,  13,
     12,  12,  11,  10,   9,   9,   8,   7,   6,   5,   5,   4,   3,   2,   2,   1,
      0,  -1,  -2,  -2,  -3,  -4,  -5,  -5,  -6,  -7,  -8,  -9,  -9, -10, -11, -12,
    -12, -13, -14, -14, -15, -16, -16, -17, -18, -18, -19, -20, -20, -21, -21, -22,
    -23, -23, -24, -24, -25, -25, -26, -26, -27, -27, -27, -28, -28, -29, -29, -29,
    -30, -30, -30, -30, -31, -31, -31, -31, -31, -32, -32, -32, -32, -32, -32, -32,
    -32, -32, -32, -32, -32, -32, -32, -32, -31, -31, -31, -31, -31, -30, -30, -30,
    -30, -29, -29, -29, -28, -28, -27, -27, -27, -26, -26, -25, -25, -24, -24, -23,
    -23, -22, -21, -21, -20, -20, -19, -18, -18, -17, -16, -16, -15, -14, -14, -13,
    -12, -12, -11, -10,  -9,  -9,  -8,  -7,  -6,  -5,  -5,  -4,  -3,  -2,  -2,  -1,
]

# Metaball falloff ~ 1/d^2, indexed by (squared distance >> shift), clamped to
# 255 by the app: +106 on a ball centre, -21 far away, so three balls span the
# field range once doubled (scene gain 1).
FALL_K = 24
FALL = [round(139 * FALL_K / (i + FALL_K)) - 33 for i in range(256)]

# Scenes, keys 1-9: {kind, radial centres, x scale, y scale, diagonal scale,
# radial shift, gain}.  kind 0 = sine ripple of the squared distance, kind 1 =
# metaball falloff.  A zero scale drops that linear sine term.
SCENES = [
    ("CLASSIC", (0, 1, 4, 4, 3, 5, 0)),
    ("RIPPLE",  (0, 1, 6, 3, 5, 4, 0)),
    ("SILK",    (0, 1, 3, 7, 2, 6, 0)),
    ("DUNES",   (0, 1, 5, 5, 4, 5, 0)),
    ("WARP",    (0, 1, 2, 8, 6, 4, 0)),
    ("MOIRE",   (0, 2, 0, 0, 0, 2, 1)),   # two interfering zone plates
    ("ZONE",    (0, 1, 0, 0, 0, 1, 2)),   # one Fresnel zone plate
    ("BLOBS",   (1, 3, 0, 0, 0, 2, 1)),   # three metaballs
    ("LAVA",    (1, 3, 2, 3, 0, 2, 1)),   # metaballs over a slow plasma
]

a = Assets("PLASMA")
a.table("T_SCENE", [name for name, _ in SCENES])
a.table("T_MODE", ["BANDS", "DITHER", "CONTOUR"])
# Title labels, indexed by the app's HUD_* kind.
a.table("T_LABEL", ["SCENE ", "RENDER: ", "SPEED ", "INVERT: ", "AUTO CYCLE: ", "PAUSE"])
a.table("T_ONOFF", ["OFF", "ON"])
a.i8("SIN", SIN)
a.i8("FALL", FALL)
# 4x4 ordered-dither matrix, flattened: idx = (y&3)*4 + (x&3), values 0..15.
a.u8("BAYER", [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5])
a.u8("SCENE", [v for _, rec in SCENES for v in rec])
a.const("NSCENE", len(SCENES))
a.const("SCENE_REC", 7)
a.const("NMODE", 3)
a.main()
