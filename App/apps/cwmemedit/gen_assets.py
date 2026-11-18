#!/usr/bin/env python3
"""CW Mem Edit texts, keymap and the configuration proxy to CW Mem's user block."""

import os
import struct
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets, cdefine

PROXY_MAGIC = cdefine("app_config_proxy.h", "APP_CONFIG_PROXY_MAGIC")
U = lambda n: cdefine("cwmem/cwmem_user.h", n)

a = Assets("CWMEMEDIT")
# app_config_proxy_t: magic, padded target name, target asset offset/length,
# reserved byte, then the marker expected at the target offset. The user
# block starts CW Mem's assets.
proxy = struct.pack("<I16sHBBI", PROXY_MAGIC, b"CW Mem".ljust(16, b"\0"),
                    0, U("MEMU_LEN"), 0, U("MEMU_MAGIC"))
if len(proxy) != 28:
    raise ValueError("configuration proxy descriptor layout changed")
a.prefix("CFG_PROXY", proxy)

a.text("T_TITLE", "CW MEM EDIT")
a.text("T_VERSION", os.environ.get("APP_VER", "dev"))
a.table("T_ITEM", ["CALL ", "7 ", "8 ", "9 "])
a.text("T_LINE1", " (LINE 1)")
a.text("T_NOCALL", "NOT SET")
a.text("T_MISSING", "Install CW Mem")
a.text("T_HINT_INDEX", "UP/DN SEL  MENU EDIT  EXIT SAVE")
a.text("T_HINT_EDIT1", "0-9 TAP, HOLD DIGIT  F ERASE")
a.text("T_HINT_EDIT2", "UP/DN MOVE  MENU OK  EXIT UNDO")
a.const("T_TITLE_CHARS", 11)
# Multi-tap keys, NUL-padded to a fixed stride; a long press types the digit.
KEYS = [" ", ".,?/=-", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"]
a.raw("KEYMAP", b"".join(k.encode().ljust(7, b"\0") for k in KEYS))
a.const("KEYMAP_STRIDE", 7)
a.main()
