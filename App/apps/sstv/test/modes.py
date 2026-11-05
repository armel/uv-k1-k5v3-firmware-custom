#!/usr/bin/env python3
"""The SSTV modes of the app, after the Dayton paper (JL Barber, "Proposal for
SSTV mode specifications", 2000), as MMSSTV, QSSTV and slowrx use them.

Each mode is one "period" repeated: the segments between two syncs, in the
standard order, as (kind, ms):
  SCAN   a picture scan the app sends the logo in and reads the luminance of
         (Y of Robot and PD, G B R of Martin and Scottie)
  ALT    Robot 36's separator: 1500 Hz on even periods, 2300 Hz on odd ones
  a frequency in Hz: a fixed tone (sync 1200, porch 1500, neutral chroma 1900)
The sync is the 1200 Hz segment. Scottie sends one extra 9 ms sync after the
VIS (pre), its periods start with a separator and carry the sync in the middle.
PD sends two picture lines per period (Y0, R-Y, B-Y, Y1).

weights: the share of each SCAN in the luminance, out of 4 (G 2, R 1, B 1).
periods: periods per picture; rows: 64 screen rows over them (A / B).
"""
SCAN, ALT = "scan", "alt"

def _robot36():
    return [(1200, 9), (1500, 3), (SCAN, 88), (ALT, 4.5), (1900, 1.5), (1900, 44)]

def _robot72():
    return [(1200, 9), (1500, 3), (SCAN, 138), (1500, 4.5), (1900, 1.5), (1900, 69),
            (2300, 4.5), (1900, 1.5), (1900, 69)]

def _martin(scan):
    return [(1200, 4.862), (1500, 0.572), (SCAN, scan), (1500, 0.572), (SCAN, scan),
            (1500, 0.572), (SCAN, scan), (1500, 0.572)]

def _scottie(scan):
    return [(1500, 1.5), (SCAN, scan), (1500, 1.5), (SCAN, scan), (1200, 9),
            (1500, 1.5), (SCAN, scan)]

def _pd(pixel, width):
    scan = pixel * width
    return [(1200, 20), (1500, 2.08), (SCAN, scan), (1900, scan), (1900, scan), (SCAN, scan)]

# name, VIS, segments, scan weights (in period order), periods, pre (ms)
MODES = [
    ("Robot 36",   8,  _robot36(),             [4],       240, 0),
    ("Robot 72",   12, _robot72(),             [4],       240, 0),
    ("Martin M1",  44, _martin(146.432),       [2, 1, 1], 256, 0),
    ("Martin M2",  40, _martin(73.216),        [2, 1, 1], 256, 0),
    ("Scottie S1", 60, _scottie(138.24),       [2, 1, 1], 256, 9),
    ("Scottie S2", 56, _scottie(88.064),       [2, 1, 1], 256, 9),
    ("Scottie DX", 76, _scottie(345.6),        [2, 1, 1], 256, 9),
    ("PD50",       93, _pd(0.286, 320),        [2, 2],    128, 0),
    ("PD90",       99, _pd(0.532, 320),        [2, 2],    128, 0),
    ("PD120",      95, _pd(0.19, 640),         [2, 2],    248, 0),
    ("PD160",      98, _pd(0.382, 512),        [2, 2],    200, 0),
    ("PD180",      96, _pd(0.286, 640),        [2, 2],    248, 0),
    ("PD240",      97, _pd(0.382, 640),        [2, 2],    248, 0),
    ("PD290",      94, _pd(0.286, 800),        [2, 2],    308, 0),
]

def cycles(ms):
    """ms -> 48 MHz cycles, exact for every duration of the table."""
    c = round(ms * 48000)
    assert abs(c - ms * 48000) < 1e-6, ms
    return c

def rows(periods):
    """64 rows over the periods: the row accumulator steps (A, B)."""
    from math import gcd
    g = gcd(64, periods)
    return 64 // g, periods // g

def check():
    for name, vis, segs, w, periods, pre in MODES:
        scans = [d for k, d in segs if k == SCAN]
        assert len(scans) == len(w) and sum(w) == 4, name
        assert all(cycles(d) % 128 == 0 for d in scans), name     # whole cycles per logo column
        assert sum(1 for k, _ in segs if k == 1200) == 1, name
        a, b = rows(periods)
        assert a <= 255 and b <= 255, name

check()

# ---- the asset record of a mode (sstv_app.c mode_t), little-endian ----------
#
#  0 u8  vis       the VIS byte, even parity in bit 7
#  1 u8  rowA, rowB  screen rows: racc += A; a row every time racc >= B
#  3 u8  nscan     picture scans per period (1..3)
#  4 u8  syncMin   shortest sync run taken (samples)
#  5 u8  nseg      TX segments per period
#  6 u8  w[3]      scan weights (sum 4)
#  9 u8  pad
# 10 u16 periods
# 12 i32 per0      period, samples Q4
# 16 i32 stepNum   pixel step Q16 at per0, times per0 (step = stepNum / per)
# 20 i32 first     start bit start -> end of the first sync, samples
# 24 i32 off[3]    sync end -> each scan, samples at per0, less the filter delay
# 36 u32 pre       Scottie's sync after the VIS, cycles (0: none)
# 40 seg[9]        TX: u32 cycles, u16 code (0 SCAN, 1 ALT, else REG_71), u16 pad
import struct

FS = 9600
DELAY = 6.8           # sync end flag -> real edge, samples (tuned on Robot 36: 28.8 -> 22)
SEG_MAX = 9
RX_SIZE = 36
SIZE = 40 + 8 * SEG_MAX
SEG_SCAN, SEG_ALT = 0, 1

def reg71(f):
    return (f * 1353245 + (1 << 16)) >> 17

def vis_byte(vis):
    return vis | (bin(vis).count("1") & 1) << 7

def record(i):
    """The values the app reads for mode i (a dict) and the packed record."""
    name, vis, segs, w, periods, pre = MODES[i]
    s = segs.index(next(x for x in segs if x[0] == 1200))
    # scans in time order from the sync end, around the period
    order = segs[s + 1:] + segs[:s]
    t, offs = 0.0, []
    for k, d in order:
        if k == SCAN:
            offs.append(round(t * FS / 1000 - DELAY))
        t += d
    # weights follow the same order (MODES lists them in period order)
    scan_idx = [j for j, (k, _) in enumerate(segs) if k == SCAN]
    wmap = dict(zip(scan_idx, w))
    w_rx = [wmap[j] for j in [(s + 1 + q) % len(segs) for q in range(len(segs))] if j in wmap]
    per_ms = sum(d for _, d in segs)
    scan_ms = next(d for k, d in segs if k == SCAN)
    sync_ms = segs[s][1]
    r = dict(name=name, vis=vis_byte(vis), periods=periods, nscan=len(offs),
             syncMin=min(48, round(sync_ms * FS / 1000 * 0.4)), nseg=len(segs),
             w=w_rx, per0=round(per_ms * FS / 1000 * 16),
             first=round(2880 + (pre + sum(d for _, d in segs[:s + 1])) * FS / 1000),
             off=offs, pre=cycles(pre) if pre else 0)
    step0 = round(128 * 65536 / (scan_ms * FS / 1000))
    r["stepNum"] = step0 * r["per0"]
    r["rowA"], r["rowB"] = rows(periods)
    r["segs"] = [(SEG_SCAN if k == SCAN else SEG_ALT if k == ALT else reg71(k), cycles(d))
                 for k, d in segs]
    # the app scales an offset as off + off * (per - per0) / per0, |per - per0| <= per0 / 16
    assert r["stepNum"] * 17 // 16 < 2 ** 31 and max(abs(o) for o in offs) * r["per0"] // 16 < 2 ** 31, name
    b = struct.pack("<9BxH", r["vis"], r["rowA"], r["rowB"], r["nscan"], r["syncMin"],
                    r["nseg"], *(r["w"] + [0] * (3 - len(r["w"]))), periods)
    b += struct.pack("<3i", r["per0"], r["stepNum"], r["first"])
    b += struct.pack("<3i", *(offs + [0] * (3 - len(offs))))
    b += struct.pack("<I", r["pre"])
    for code, cyc in r["segs"] + [(0, 0)] * (SEG_MAX - len(segs)):
        b += struct.pack("<IHxx", cyc, code)
    assert len(b) == SIZE
    return r, b
