#!/usr/bin/env python3
# Cube3D read-only assets: the solids, the Q14 sine quadrant and the speeds.
#
# SHAPES layout (offsets relative to SHAPES):
#   0          count N
#   1 + 3*k    solid k directory entry: u16 record offset, u8 record length
#   record     nv, nf, name + NUL, nv x (x, y, z) int8, then nf packed faces
#              (n, v0 .. v{n-1}), each wound CCW as seen from outside
# The faces were generated offline by a convex-hull extractor, so every solid
# shares the same outward winding and the signed-area cull sign. SHAPE_MAXV and
# SHAPE_REC_MAX size the app's buffers, so adding a solid needs no C change.
#
#   ./gen_assets.py cube3d_assets.bin cube3d_assets.h
import math, os, struct, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

MAXN = 6   # largest face polygon

SHAPES = [
    ("CUBE",
     [(-26,-26,-26),(26,-26,-26),(26,26,-26),(-26,26,-26),(-26,-26,26),(26,-26,26),(26,26,26),(-26,26,26)],
     [(2,1,0,3),(4,0,1,5),(7,3,0,4),(5,1,2,6),(6,2,3,7),(7,4,5,6)]),
    ("OCTAHEDRON",
     [(38,0,0),(-38,0,0),(0,38,0),(0,-38,0),(0,0,38),(0,0,-38)],
     [(4,0,2),(2,0,5),(3,0,4),(5,0,3),(2,1,4),(5,1,2),(4,1,3),(3,1,5)]),
    ("TETRAHEDRON",
     [(28,28,28),(28,-28,-28),(-28,28,-28),(-28,-28,28)],
     [(2,0,1),(1,0,3),(3,0,2),(2,1,3)]),
    ("DIAMOND",
     [(30,0,0),(14,26,0),(-14,26,0),(-30,0,0),(-14,-26,0),(14,-26,0),(0,0,40),(0,0,-40)],
     [(6,0,1),(1,0,7),(5,0,6),(7,0,5),(6,1,2),(2,1,7),(6,2,3),(3,2,7),(6,3,4),(4,3,7),(6,4,5),(5,4,7)]),
    ("ICOSAHEDRON",
     [(0,18,29),(0,18,-29),(0,-18,29),(0,-18,-29),(18,29,0),(18,-29,0),(-18,29,0),(-18,-29,0),(29,0,18),(29,0,-18),(-29,0,18),(-29,0,-18)],
     [(8,0,2),(2,0,10),(6,0,4),(4,0,8),(10,0,6),(3,1,9),(11,1,3),(4,1,6),(9,1,4),(6,1,11),
      (5,2,7),(8,2,5),(7,2,10),(7,3,5),(5,3,9),(11,3,7),(9,4,8),(8,5,9),(10,6,11),(11,7,10)]),
    ("CUBOCTA",
     [(-24,-24,0),(-24,24,0),(24,-24,0),(24,24,0),(-24,0,-24),(-24,0,24),(24,0,-24),(24,0,24),(0,-24,-24),(0,-24,24),(0,24,-24),(0,24,24)],
     [(4,0,5,1),(2,9,0,8),(8,0,4),(5,0,9),(10,1,11,3),(4,1,10),(11,1,5),(3,7,2,6),(6,2,8),(9,2,7),(10,3,6),(7,3,11),(8,4,10,6),(7,11,5,9)]),
    ("HEXPRISM",
     [(26,0,24),(12,22,24),(-12,22,24),(-26,0,24),(-12,-22,24),(12,-22,24),(26,0,-24),(12,22,-24),(-12,22,-24),(-26,0,-24),(-12,-22,-24),(12,-22,-24)],
     [(4,5,0,1,2,3),(7,1,0,6),(6,0,5,11),(8,2,1,7),(9,3,2,8),(10,4,3,9),(11,5,4,10),(9,8,7,6,11,10)]),
    ("PENTAGEM",
     [(28,0,0),(8,26,0),(-22,16,0),(-22,-16,0),(8,-26,0),(0,0,42),(0,0,-42)],
     [(5,0,1),(1,0,6),(4,0,5),(6,0,4),(5,1,2),(2,1,6),(5,2,3),(3,2,6),(5,3,4),(4,3,6)]),
]

# One Q14 sine quadrant: symmetry recovers the full 256-step wave.
SIN_Q = [
       0,  402,  804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981,
    4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723, 8076,
    8423, 8765, 9102, 9434, 9760,10080,10394,10702,11003,11297,11585,
   11866,12140,12406,12665,12916,13160,13395,13623,13842,14053,14256,
   14449,14635,14811,14978,15137,15286,15426,15557,15679,15791,15893,
   15986,16069,16143,16207,16261,16305,16340,16364,16379,16384,
]

# Quarter-half-units per frame for speed levels 1..16. The low end has
# fractional angular steps; level 16 reaches 16 half-units/frame once divided
# by four.
ROT_RATE = [1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 30, 36, 44, 52, 64]

# Perspective projection without a division: for n = x * FOCAL and
# zc = z + DIST, the C expression n / zc (truncating toward zero) equals
# sign(n) * ((|n| * RECIP[zc - RECIP_ZMIN]) >> RECIP_SHIFT) exactly, with
# RECIP[d] = 2^RECIP_SHIFT // d + 1, as long as |n| < 2^RECIP_SHIFT / zc (the
# error term then stays below 1 / zc). The zc range covers every solid's
# largest radius, plus a margin for the Q14 rotation rounding.
DIST, FOCAL = 150, 80
RECIP_SHIFT = 20
R = int(max(math.sqrt(x * x + y * y + z * z) for _, v, _ in SHAPES for x, y, z in v)) + 4
RECIP_ZMIN, RECIP_ZMAX = DIST - R, DIST + R
RECIP = [(1 << RECIP_SHIFT) // d + 1 for d in range(RECIP_ZMIN, RECIP_ZMAX + 1)]
if FOCAL * R >= (1 << RECIP_SHIFT) // RECIP_ZMAX or FOCAL * R * RECIP[0] >= 1 << 31 or RECIP[0] > 0xFFFF:
    sys.exit("RECIP: a solid is too large for an exact division-free projection")

def record(name, verts, faces):
    out = bytes([len(verts), len(faces)]) + name.encode("ascii") + b"\x00"
    for v in verts:
        out += struct.pack("<3b", *v)
    for f in faces:
        if not 3 <= len(f) <= MAXN or any(i >= len(verts) for i in f):
            sys.exit(f"{name}: bad face {f}")
        out += bytes([len(f), *f])
    if len(out) > 255:
        sys.exit(f"{name}: record {len(out)} B exceeds 255 B")
    return out

records = [record(*s) for s in SHAPES]
base = 1 + 3 * len(records)
shapes = bytes([len(records)])
body = b""
for r in records:
    shapes += struct.pack("<HB", base + len(body), len(r))
    body += r

a = Assets("CUBE3D")
a.raw("SHAPES", shapes + body)
a.i16("SIN_Q", SIN_Q)
a.u8("ROT_RATE", ROT_RATE)
a.u16("RECIP", RECIP)
a.const("RECIP_ZMIN", RECIP_ZMIN)
a.const("RECIP_ZMAX", RECIP_ZMAX)
a.const("RECIP_SHIFT", RECIP_SHIFT)
a.const("DIST", DIST)
a.const("FOCAL", FOCAL)
a.const("SHAPE_MAXV", max(len(v) for _, v, _ in SHAPES))
a.const("SHAPE_REC_MAX", max(len(r) for r in records))
a.main()
