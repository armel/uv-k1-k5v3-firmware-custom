#!/usr/bin/env python3
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

a = Assets("FLASHMAP")
a.text("T_TITLE", "FLASH MAP")
a.table("T_ZONE", [
    "CONFIG 0", "CALIBRATION", "CAL RESERVE", "BOOT LOGO", "FREE", "RX/TX LOG",
    "CONFIG 1", "CONFIG 2", "CONFIG 3", "CONFIG 4",
    "STATE A", "STATE B", "APP RESERVE", "SONIC 4K",
])
a.table("T_DESC", [
    "Main channels + settings",
    "Shared radio calibration",
    "Unused calibration space",
    "128 x 64 startup bitmap",
    "Unallocated flash space",
    "RX/TX log, 1024 records",
    "Multiboot slot, 128 KB",
    "Private channels + settings",
    "Overlay app slot",
    "Reserve for 62 app slots",
    "Sonic 4K interop area",
])
a.table("T_HEADER_STATE", ["EMPTY", "BAD HEADER", "UNNAMED"])
a.table("T_TYPE", ["CONF", "CAL", "LOGO", "FREE", "LOG", "FW", "MB", "APP", "4K",
                   "LIVE"])

# Multiboot state sector verdicts, indexed by MARK_* in flashmap_app.c.
a.table("T_STATE", ["Erased", "Corrupt record", "Legacy", "Gen "])
pair = "FW0, CONFIG 0"
a.text("T_LIVE_PAIR", pair)
a.const("LIVE_PAIR_FW", pair.index("FW") + 2)
a.const("LIVE_PAIR_CFG", pair.index("CONFIG ") + 7)

a.text("T_FW_MAX", "/118 KB")
a.table("T_UNIT", [" code", " assets"])

a.table("T_DUMP_MODE", ["HEX", "TEXT"])
a.u8("BMP_F", [0x3e, 0x7f, 0x41, 0x75, 0x75, 0x75, 0x7d, 0x7f, 0x3e])
a.main()
