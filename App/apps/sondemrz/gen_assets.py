#!/usr/bin/env python3
import math
import os
import struct
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "Sonde MRZ"
UI = [
    ("T_TITLE", TITLE),
    ("T_NOPOS", "no position"),
    ("T_ALT", "ALT "),
    ("T_M", "m"),
    ("T_TEMP", "T "),
    ("T_C", "C "),
    ("T_RH", "RH "),
    ("T_FRAME", "FRAME "),
    ("T_VS", " VS"),
    ("T_HS", "HS "),
    ("T_HDG", "m/s HDG "),
    ("T_DBM", "dBm"),
    ("T_KHZ", "kHz"),
]
a = Assets("SONDE_MRZ")
ui_size = 0
for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
    ui_size += len(value) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.u8("BMP_SIGNAL", [0x08, 0x1C, 0x1C, 0x08, 0x00, 0x22, 0x1C, 0x41, 0x22, 0x1C])
a.u8("BMP_SPK", [0x1C, 0x1C, 0x3E, 0x7F, 0x00, 0x22, 0x1C, 0x41, 0x22, 0x1C])
a.u8("BMP_F", [0x3E, 0x7F, 0x41, 0x75, 0x75, 0x75, 0x7D, 0x7F, 0x3E, 0x00])
a.const("ICON_W", 10)
a.u16("ATT_REG13", [0x03DF, 0x03DD, 0x03DB, 0x03D9, 0x0379, 0x0139])
a.const("ATT_COUNT", 6)
ATT_REC = 16
ATT_LBL = ["ATT 0dB", "ATT 6dB", "ATT 15dB", "ATT 27dB", "ATT BYP", "ATT BYP+"]
a.raw("ATT_LBL", b"".join((bytes([128 - 4 * len(s)]) + s.encode()).ljust(ATT_REC, b"\0")
                          for s in ATT_LBL))
a.const("ATT_REC", ATT_REC)
# CORDIC atan(2^-i), in centi-arcseconds, loaded into the overlay decoder.
a.raw("MRZ_ARC", b"".join(struct.pack("<I", round(math.degrees(math.atan(2.0 ** -i)) * 360000.0))
                           for i in range(24)))
a.main()
