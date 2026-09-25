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
#   rx_bw     u16  REG_43 RX filter: the resident spectrum's scanStepBWRegValues
#                  for the same steps (App/app/spectrum.h)
#   substeps  u8   measurements per point, 1 or 2 (the app shifts, never
#                  divides): 25 kHz is the widest filter, so the 50 kHz step
#                  also measures halfway and keeps the maximum
#   close_db  u8   listening closes below the sweep floor plus this many dB:
#                  the audio uses the VFO's own filter (up to 25 kHz), whose
#                  noise sits higher than the narrower sweep filters (+6 dB
#                  over 6.25 kHz, +3 dB over 12.5 kHz, on top of 5 dB)
#   label          the span capsule: LABEL_LEN characters + NUL
SPANS = [("0.4M", 625,  0x4858, 1, 11),   # 6.25 kHz filter
         ("0.8M", 1250, 0x7F08, 1, 8),    # 12.5 kHz filter
         ("1.6M", 2500, 0x3628, 1, 5),    # 25 kHz filter
         ("3.2M", 5000, 0x3628, 2, 5)]    # 25 kHz filter, two points per step
LABEL_LEN = 4
if any(len(s[0]) != LABEL_LEN or s[3] not in (1, 2) for s in SPANS):
    sys.exit("SPANS: labels must have LABEL_LEN characters and substeps be 1 or 2")
RECORDS = [struct.pack(f"<HHBB{LABEL_LEN + 1}s", step, bw, sub, close, label.encode("ascii"))
           for label, step, bw, sub, close in SPANS]

# RX regions the sweep must stay inside (x10 Hz, inclusive): the RF path
# switches VHF/UHF at 280 MHz (BK4819_PickRXFilterPathBasedOnFrequency) and the
# chip covers 18..630 and 840..1300 MHz (App/frequencies.c, RX_freq_check).
REGIONS = [(1800000, 27999999), (28000000, 62999999), (84000000, 130000000)]

# Saved settings, in the order of the app's struct globals: magic, span,
# speed, yaw (signed), pitch. The defaults: 1.6 MHz, a line per sweep, front
# view tilted 25 degrees. 0x3E configs stored yaw + 9: the new magic resets them.
CFG_MAGIC = 0x3F
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
a.u8("CFG_DEFAULT", [CFG_MAGIC, 2, 0, 0, PITCH_DEF])
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
