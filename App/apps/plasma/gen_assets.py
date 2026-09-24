#!/usr/bin/env python3
# Plasma read-only assets: the sine wave, the ordered-dither matrix and the
# pattern presets. The app copies them onto its stack at launch, so the render
# loop reads them from RAM at full speed while they stay out of the overlay.
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

a = Assets("PLASMA")
a.i8("SIN", SIN)
# 4x4 ordered-dither matrix, flattened: idx = (y&3)*4 + (x&3), values 0..15.
a.u8("BAYER", [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5])
# Pattern presets: {x scale, y scale, diagonal scale, radial ring shift}.
a.u8("VAR", [4, 4, 3, 5, 6, 3, 5, 4, 3, 7, 2, 6, 5, 5, 4, 5, 2, 8, 6, 4])
a.const("NVAR", 5)
a.main()
