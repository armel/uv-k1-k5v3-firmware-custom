#!/usr/bin/env python3
# Spectrum3D read-only assets: texts, bitmaps and the setting tables.
#
#   ./gen_assets.py spectrum3d_assets.bin spectrum3d_assets.h
import math, os, struct, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

# One record per span of the 64-point sweep. The app reads a record whole into
# its struct globals, from `step` on, so the fields must keep the order and
# sizes of that struct (the app checks the total with a static assert):
#   step      u16  point spacing (x10 Hz)
#   rx_bw     u16  REG_43 RX filter during the sweep: the widest (25 kHz, the
#                  resident spectrum's scanStepBWRegValues for its 25 kHz step,
#                  App/app/spectrum.h) at every span, so that a signal between
#                  two measurements still falls well inside the filter
#   sub_shift u8   log2 of the measurements per point, 0 to 2 (the app shifts,
#                  never divides), which keeps them 12.5 kHz apart at most, the
#                  channel raster: every channel is then within 6.25 kHz of a
#                  measurement, where a narrower filter or a 25 kHz spacing left
#                  a channel between two points at the filter's edge. A point
#                  keeps the maximum of its measurements.
#   close_db  u8   listening closes below the sweep floor plus this many dB:
#                  the audio's VFO filter (up to 25 kHz) has no more noise than
#                  the 25 kHz sweep filter, so 5 dB at every span
#   label          the span capsule, LABEL_LEN characters + NUL: the window
#                  either side of the centre (the 64 points run from 32 below
#                  it to 31 above), as ±200K for the 400 kHz window. PM is the
#                  3x5 font's ± (gFont3x5[0x7F - 0x20] in App/font.c).
WIDE = 0x3628                            # REG_43: 25 kHz filter
PM = "\x7f"
SPANS = [(PM + "200K", 625,  WIDE, 0, 5),    # 6.25 kHz points
         (PM + "400K", 1250, WIDE, 0, 5),    # 12.5 kHz points
         (PM + "800K", 2500, WIDE, 1, 5),    # 25 kHz points, 2 measurements each
         (PM + "1.6M", 5000, WIDE, 2, 5)]    # 50 kHz points, 4 measurements each
LABEL_LEN = 5
MEASURE_MAX = 1250                       # widest spacing of the measurements (x10 Hz)
if any(len(s[0]) != LABEL_LEN or s[3] not in (0, 1, 2) or s[1] >> s[3] > MEASURE_MAX
       for s in SPANS):
    sys.exit("SPANS: labels must have LABEL_LEN characters, sub_shift be 0 to 2 "
             "and the measurements be at most 12.5 kHz apart")
RECORDS = [struct.pack(f"<HHBB{LABEL_LEN + 1}s", step, bw, shift, close, label.encode("ascii"))
           for label, step, bw, shift, close in SPANS]

# RX regions the sweep must stay inside (x10 Hz, inclusive): the RF path
# switches VHF/UHF at 280 MHz (BK4819_PickRXFilterPathBasedOnFrequency) and the
# chip covers 18..630 and 840..1300 MHz (App/frequencies.c, RX_freq_check).
REGIONS = [(1800000, 27999999), (28000000, 62999999), (84000000, 130000000)]

# Saved settings, in the order of the app's struct globals: magic, span,
# speed, yaw (signed), pitch. The defaults: ±200 kHz, a line per sweep, front
# view tilted 25 degrees. 0x3E configs stored yaw + 9: the new magic resets them.
CFG_MAGIC = 0x3F
SPAN_DEF = [s[0] for s in SPANS].index(PM + "200K")
PITCH_DEF = 5          # 25 degrees
SPEED_COUNT = 3        # 1 << speed sweeps per landscape line: 1, 2, 4 (peak-held)

a = Assets("SPECTRUM3D")
a.text("T_TITLE", "SPECTRUM3D")
a.text("T_HOLD", "HOLD")
a.raw("SPAN_REC", b"".join(RECORDS))
a.const("SPAN_REC_SIZE", len(RECORDS[0]))
a.const("SPAN_LABEL_LEN", LABEL_LEN)
a.const("SPAN_COUNT", len(SPANS))
a.u32("REGION", [edge for region in REGIONS for edge in region])
a.const("REGION_COUNT", len(REGIONS))
a.u8("CFG_DEFAULT", [CFG_MAGIC, SPAN_DEF, 0, 0, PITCH_DEF])
a.const("CFG_MAGIC", CFG_MAGIC)
a.const("PITCH_DEF", PITCH_DEF)
a.const("SPEED_COUNT", SPEED_COUNT)
a.u8("BMP_F", [0x3e,0x7f,0x41,0x75,0x75,0x75,0x7d,0x7f,0x3e])   # F-armed icon, as Beacon
a.u8("BMP_SPEAKER", [0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c])  # as FoxHunt
# sin() in Q8 from -45 to 135 degrees by 5, 0 degrees at SINQ_ZERO: the yaw
# (-45..45), the pitch (10..60) and their cosines, sin(90 - angle), index it
# directly, with no sign handling in the app.
a.i16("SINQ", [round(math.sin(math.radians(d)) * 256) for d in range(-45, 136, 5)])
a.const("SINQ_ZERO", 9)
a.main()
