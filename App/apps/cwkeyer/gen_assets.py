#!/usr/bin/env python3
"""CW Keyer variant read-only UI assets and receive profile."""

import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

USB_VARIANT = os.environ.get("CW_KEYER_USB") == "1"
TITLE = "CW USB" if USB_VARIANT else "CW KEYER"

a = Assets("CWUSB" if USB_VARIANT else "CWKEYER")
a.text("T_TITLE", TITLE)
# build.sh passes the version it packs into the .app header.
if USB_VARIANT:
    version = os.environ.get("APP_VER", "dev")
    a.table("T_VERSION", ["AUTO", "IAMB A", "IAMB B"], stride=16)
else:
    a.text("T_VERSION", os.environ.get("APP_VER", "dev"))
a.text("T_READY", "RX")
a.text("T_DENIED", "TX OFF")
a.table("T_MODE", ["STRAIGHT", "PADDLE"])
a.table("T_ORDER", ["DI-DA", "DA-DI"])
a.text("T_DBM", " dBm")
a.text("T_SPEED", "WPM ")
a.text("T_RX_PITCH", "TONE ")
a.text("T_BREAKIN", "BK ")
a.text("T_XTAL", "XTAL ")
# Existing App/bitmaps.c glyphs, copied verbatim: BITMAP_VFO_Lock and
# BITMAP_Ready (the latter is retained there in a commented resource block).
# asset_read is app-local: these existing designs must be packed here.
if USB_VARIANT:
    a.u8("BMP_LOCK", [0x3c, 0x26, 0x25, 0x25, 0x25, 0x26, 0x3c])
    a.u8("BMP_READY", [0x08, 0x10, 0x20, 0x10, 0x08, 0x04, 0x02])

a.u8("BMP_F", [0x3E, 0x7F, 0x41, 0x75, 0x75, 0x75, 0x7D, 0x7F, 0x3E])
a.u8("BMP_SPEAKER", [0x1C, 0x1C, 0x3E, 0x7F, 0x00,
                     0x22, 0x1C, 0x41, 0x22, 0x1C])
# BK4829 register / value pairs that configureCwRx writes, in this order,
# to set the CW receive path.
a.u16("RX_PROFILE", [
    0x42, 0x6B5A,   # RX level
    0x2A, 0x7400,   # RX filter 1
    0x2B, 0x0000,   # RX filter 2
    0x2F, 0x9890,   # RX filter 3
    0x54, 0x9009,   # audio 1
    0x55, 0x31A9,   # audio 2
    0x3D, 0x0000,   # audio filter
])
# Help page (MENU). The pack stores every text before the binary data, so
# these come after the main-screen texts and before BMP_F.
a.text("T_HELP", "HELP")
a.text("T_HELP_STEP", "Tuning step 100 Hz")
# The left column (help lines 1 to 6), then the right one; "" leaves a slot
# empty.
a.table("T_HELP_KEYS", [
    "0 SPEAKER", "1 MODE", "2 WPM", "3 TONE", "4 BREAK-IN", "5 ORDER",
    "6 XTAL", "USB KEY/PAD" if USB_VARIANT else "SIDE1/2 KEY",
    "F+ DECREASE", "UP/DN FREQ", "PTT STRAIGHT", "8 KEYER" if USB_VARIANT else "",
])
a.const("T_TITLE_CHARS", len(TITLE))
a.main()
