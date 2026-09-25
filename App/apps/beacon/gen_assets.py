#!/usr/bin/env python3
# Beacon read-only assets: texts, fox identifiers, Morse table and icons.
#
#   ./gen_assets.py beacon_assets.bin beacon_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

a = Assets("BEACON")
a.text("T_BEACON", "BEACON")
a.text("T_TX", "TX")
a.text("T_IDLE", "IDLE")
a.text("T_FOX", "FOX ")
a.text("T_CALL", "CALL")
a.text("T_CARR", "CARR")
a.text("T_TONE", "TONE")
a.text("T_TX_OFF", "TX OFF")
# Fox identifiers by foxFox (0..FOX_MO); FOX_CALL sends "<call> " + entry 0.
a.table("FOX_ID", ["MOE", "MOI", "MOS", "MOH", "MO5", "MO"])
# Sentinel-prefixed Morse (leading 1, then elements MSB-first: 0 dit, 1 dah):
# A..Z, then 0..9, then '/'.
a.u8("MORSE", [0x05,0x18,0x1A,0x0C,0x02,0x12,0x0E,0x10,0x04,0x17,0x0D,0x14,0x07,
               0x06,0x0F,0x16,0x1D,0x0A,0x08,0x03,0x09,0x11,0x0B,0x19,0x1B,0x1C,
               0x3F,0x2F,0x27,0x23,0x21,0x20,0x30,0x38,0x3C,0x3E,
               0x32])
a.u8("BMP_TX",   [0x1c,0x22,0x41,0x1c,0x22,0x00,0x08,0x1c,0x1c,0x08,0x00,0x22,0x1c,0x41,0x22,0x1c])
a.u8("BMP_LOCK", [0x7c,0x46,0x45,0x45,0x45,0x45,0x45,0x46,0x7c])
a.u8("BMP_F",    [0x3e,0x7f,0x41,0x75,0x75,0x75,0x7d,0x7f,0x3e])
# Powers of ten, 10^8 down to 1: numbers (the frequency too) are printed by
# subtraction, so the app links no division.
a.u32("PLACE", [10 ** k for k in range(8, -1, -1)])
a.main()
