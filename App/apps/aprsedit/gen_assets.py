#!/usr/bin/env python3
"""APRS Edit texts, symbol table and declarative APRS TX profile proxy."""
import os, struct, sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets, cdefine
from aprs_symbols_20 import BITMAPS, BYTES_PER_ICON, CODES, WIDTH

PROXY_MAGIC = cdefine("app_config_proxy.h", "APP_CONFIG_PROXY_MAGIC")
PROFILE_MAGIC = cdefine("aprs_profile.h", "APRS_PROFILE_MAGIC")
PROFILE_LEN = cdefine("aprs_profile.h", "APRS_PROFILE_LEN")
COMMENT_MAX = cdefine("aprs_profile.h", "APRS_COMMENT_MAX")
TARGET = b"APRS TX"
# The editor caps the comment at what fits above its bottom capsules in the
# normal font without scrolling: 6 rows of 11 characters, less room for the
# insertion cursor after the last one. The profile field stays COMMENT_MAX wide.
COMMENT_LIMIT = 64

# app_config_proxy_t: magic, padded target name, target asset offset/length,
# reserved byte, then the marker expected at the target offset.
proxy = struct.pack("<I16sHBBI", PROXY_MAGIC, TARGET.ljust(16, b"\0"),
                    0, PROFILE_LEN, 0, PROFILE_MAGIC)
if len(proxy) != 28:
    raise ValueError("configuration proxy descriptor layout changed")

UI = [
    ("T_TITLE", "APRS EDIT"),
    ("T_MODE_UP", "ABC"),
    ("T_MODE_LO", "abc"),
    ("T_MODE_NUM", "123"),
    ("T_AUTO", "AUTO"),
    ("T_INVALID", "Invalid value"),
    ("T_MISSING1", "TX/FW MISMATCH"),
    ("T_MISSING2", "UPDATE REQUIRED"),
]

MENU = ["Call", "SSID", "Lat", "Lon", "Path", "Symbol", "Comm."]
PATHS = ["DIRECT", "WIDE1-1", "WIDE1-1,WIDE2-1", "ARISS"]   # APRS TX path order
PRESETS = [
    ("PORTABLE", "Portable, UV-K5/K1 F4HWN firmware (github.com/armel)"),
    ("MOBILE", "Mobile, UV-K5/K1 F4HWN firmware (github.com/armel)"),
    ("QRP", "QRP, UV-K5/K1 F4HWN firmware (github.com/armel)"),
    ("TEST", "APRS test, UV-K5/K1 F4HWN firmware (github.com/armel)"),
]
PRESET_NAMES = ["CUSTOM"] + [name for name, _ in PRESETS]

a = Assets("APRSEDIT")
a.prefix("CFG_PROXY", proxy)
ui_size = len(proxy)
for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
    ui_size += len(value) + 1 + pad
menu_stride = 12
a.table("T_MENU", MENU, stride=menu_stride)
ui_size += len(MENU) * menu_stride
path_stride = -(-(max(len(value) for value in PATHS) + 1) // 4) * 4
a.table("T_PATHS", PATHS, stride=path_stride)
ui_size += len(PATHS) * path_stride
preset_name_stride = 12
a.table("T_PRESET_NAMES", PRESET_NAMES, stride=preset_name_stride)
ui_size += len(PRESET_NAMES) * preset_name_stride
a.raw("SYM_CODES", "".join(CODES).encode("ascii"))
ui_size += len(CODES) * 2
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len("APRS EDIT"))
a.const("COMMENT_MAX", COMMENT_MAX)
a.const("COMMENT_LIMIT", COMMENT_LIMIT)
a.const("PROFILE_LEN", PROFILE_LEN)
a.const("SYM_COUNT", len(CODES))
KEYS = [" ", ".,?!", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"]
a.raw("KEYMAP", b"".join(value.encode("ascii").ljust(5, b"\0") for value in KEYS))
a.const("KEYMAP_STRIDE", 5)
# The shared icons fill rows 1-20 of their three pages. Moved down SYM_SHIFT
# rows here, they still fit (rows 4-23): the app copies them to lines 2-4 and
# the icon lands 4 px below the table/code row, at no code cost.
SYM_SHIFT = 3
shifted = bytearray()
for icon in range(len(CODES)):
    base = icon * BYTES_PER_ICON
    for page in range(3):
        for x in range(WIDTH):
            column = sum(BITMAPS[base + p * WIDTH + x] << (8 * p) for p in range(3))
            column <<= SYM_SHIFT
            if column >> 24:
                raise ValueError("shifted APRS symbol leaves its three pages")
            shifted.append(column >> (8 * page) & 0xFF)
a.raw("SYM_BITMAPS", bytes(shifted))
a.const("SYM_W", WIDTH)
a.const("SYM_BYTES", BYTES_PER_ICON)
a.const("PRESET_COUNT", len(PRESET_NAMES))
a.const("TEMPLATE_COUNT", len(PRESETS))
if COMMENT_LIMIT > COMMENT_MAX:
    raise ValueError("the editor's comment limit exceeds the profile field")
messages = bytearray()
lengths = bytearray()
for index, (_, message) in enumerate(PRESETS):
    if len(message) > COMMENT_LIMIT:
        raise ValueError(f"preset {index} exceeds the editor's comment limit")
    encoded = message.encode("ascii")
    lengths.append(len(encoded))
    messages.extend(encoded.ljust(COMMENT_MAX, b" "))
a.raw("P_PRESET_LENGTHS", bytes(lengths))
a.raw("P_PRESETS", bytes(messages))

if __name__ == "__main__":
    a.main()
