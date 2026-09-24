#!/usr/bin/env python3
# Beam read-only assets: status texts and the packet obfuscation key.
#
#   ./gen_assets.py beam_assets.bin beam_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

a = Assets("BEAM")
# Indexed by beam_app.c: 0/1 = READY in TX/RX mode, then status + 1.
a.table("T_STATE", ["BEAM TX", "BEAM RX", "SENDING", "SENT", "WAITING",
                    "RECEIVED", "MEM FULL", "ERROR"])
a.u16("OBFUSCATION", [0x6C16, 0xE614, 0x912E, 0x400D, 0x3521, 0x40D5, 0x0313, 0x80E9])
a.main()
