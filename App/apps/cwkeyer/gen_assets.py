#!/usr/bin/env python3
"""CW Keyer read-only UI assets."""

import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "CW KEYER"

a = Assets("CWKEYER")
a.text("T_TITLE", TITLE)
a.text("T_READY", "RX")
a.text("T_DENIED", "TX OFF")
a.table("T_MODE", ["STRAIGHT", "AUTO", "IAMB A", "IAMB B"])
a.table("T_PORT", ["SIDE ", "USB "])
a.text("T_SPEED", "WPM ")
a.text("T_RX_PITCH", "TONE ")
a.text("T_BREAKIN", "BK ")
a.text("T_XTAL", "XTAL ")
a.u8("BMP_F", [0x3E, 0x7F, 0x41, 0x75, 0x75, 0x75, 0x7D, 0x7F, 0x3E])
a.u8("BMP_SPEAKER", [0x1C, 0x1C, 0x3E, 0x7F, 0x00,
                     0x22, 0x1C, 0x41, 0x22, 0x1C])
a.const("T_TITLE_CHARS", len(TITLE))
a.main()
