#!/usr/bin/env python3
# APRS TX read-only assets: the screen texts and the fixed part of the frame.
#
# The UI texts come first, each padded to a multiple of 4 bytes: draw() reads
# them in one asset_read into a word-aligned stack block, so every text costs a
# 2-byte sp-relative add instead of a literal-pool load and a pool word.
# The frame parts are AX.25-encoded here (addresses shifted left, SSID bytes):
# the app adds the source (profile or boot-message callsign + SSID), the path (the first 0,
# 1 or 2 entries of WIDE, or ARISS alone; end-of-address bit set by the app), the position and
# the FCS. APRS Edit updates the mutable profile prefix. Edit the station
# defaults below, then rebuild;
# test/tx_model.py checks the resulting frame.
#
#   ./gen_assets.py aprstx_assets.bin aprstx_assets.h
import os, re, struct, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets, cdefine
from aprs_symbols_20 import BITMAPS, BYTES_PER_ICON, CODES, WIDTH

APRS_PROFILE_MAGIC = cdefine("aprs_profile.h", "APRS_PROFILE_MAGIC")
APRS_PROFILE_LEN = cdefine("aprs_profile.h", "APRS_PROFILE_LEN")
APRS_COMMENT_MAX = cdefine("aprs_profile.h", "APRS_COMMENT_MAX")
APRS_CALL_MAX = cdefine("aprs_profile.h", "APRS_CALL_MAX")

# ---- station settings ----
SSID = 7                                   # F4HWN-7 (0-15, 0 = no SSID)
DEST = "APZK5"                             # APZ = experimental software
PATH = 1                                   # index in PATHS below
WIDE = ["WIDE1-1", "WIDE2-1"]              # path n = the first n entries
ARISS = "ARISS"                            # last path: the ISS digipeater alias
PATHS = ["DIRECT", "WIDE1-1", "WIDE1-1,2-1", ARISS]   # shown on the radio, per path
LAT, LON = "4850.90N", "00216.25E"         # default position: DDMM.hhN, DDDMM.hhE
SYMBOL = "/["                              # table, code: '[' = person
COMMENT = "Portable, UV-K5/K1 F4HWN firmware (github.com/armel)"
# info field: "!" + LAT + SYMBOL[0] + LON + SYMBOL[1] + COMMENT

TITLE = "APRS TX"
TX_CAPS = "TRANSMIT"                       # status-bar capsule while on the air

UI = [
    ("T_TITLE",  TITLE),
    ("T_TX",     TX_CAPS),
    ("T_DENIED", "TX denied"),
    ("T_NOCALL", "No callsign"),
    ("T_NOEDIT", "No APRS Edit"),
    ("T_LVL",    "lvl "),
    ("T_TW",     "  tw "),
    ("T_SENT",   "  sent "),
]


def addr(call, last=False):
    call, _, ssid = call.upper().partition("-")
    if not (1 <= len(call) <= 6 and call.isalnum()):
        sys.exit(f"bad AX.25 address {call!r}")
    b = bytes(ord(c) << 1 for c in call.ljust(6))
    return b + bytes([0x60 | (int(ssid or 0) << 1) | (1 if last else 0)])


if not 0 <= SSID <= 15:
    sys.exit("SSID must be 0-15")
if len(WIDE) != 2 or len(PATHS) != len(WIDE) + 2 or not 0 <= PATH < len(PATHS):
    sys.exit("PATHS lists DIRECT, 1 and 2 WIDE entries (frame buffer), then ARISS; PATH indexes it")
if any(len(s) > 18 - 5 for s in PATHS):
    sys.exit("a PATHS name does not fit the line after 'PATH '")
if not (re.fullmatch(r"\d{4}\.\d{2}[NS]", LAT) and re.fullmatch(r"\d{5}\.\d{2}[EW]", LON)):
    sys.exit("LAT must be DDMM.hhN/S, LON DDDMM.hhE/W")
if len(SYMBOL) != 2 or len(COMMENT) > APRS_COMMENT_MAX:
    sys.exit(f"SYMBOL is 2 characters, COMMENT at most {APRS_COMMENT_MAX} (frame buffer)")
# 13 digits (DDMMhh DDDMMhh), then the hemispheres: bit 0 south, bit 1 west
POS = [int(c) for c in LAT + LON if c.isdigit()]
HEMI = (LAT[-1] == "S") | (LON[-1] == "W") << 1

a = Assets("APRSTX")
comment = COMMENT.encode("ascii")
a.prefix("F_PROFILE", struct.pack("<IBBBB", APRS_PROFILE_MAGIC, len(comment),
         PATH << 4 | SSID, CODES.index(SYMBOL), HEMI) + bytes(POS) +
         comment.ljust(APRS_COMMENT_MAX, b" ") + bytes([0]) + b" " * APRS_CALL_MAX)
if APRS_PROFILE_LEN != 8 + len(POS) + APRS_COMMENT_MAX + 1 + APRS_CALL_MAX:
    raise ValueError("APRS profile layout changed")
a.const("COMMENT_MAX", APRS_COMMENT_MAX)
ui_size = APRS_PROFILE_LEN
for name, s in UI:
    pad = -(len(s) + 1) % 4                 # keep every offset word-aligned
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)                 # the UI block read by draw()
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_TX_CHARS", len(TX_CAPS))
a.raw("F_DEST", addr(DEST))
a.raw("F_PATH", b"".join(addr(h) for h in WIDE + [ARISS]))
a.raw("SYM_CODES", "".join(CODES).encode("ascii"))
a.raw("SYM_BITMAPS", BITMAPS)
a.const("SYM_COUNT", len(CODES))
a.const("SYM_W", WIDTH)
a.const("SYM_BYTES", BYTES_PER_ICON)
a.u8("BMP_F", [0x3e,0x7f,0x41,0x75,0x75,0x75,0x7d,0x7f,0x3e])   # F armed, as FoxHunt / FM / Beacon
# scroll marks (status bar, x = 75, as APRS RX): up only, down only, both; up
# in bits 0-2 (rows hidden above), down in bits 4-6 (rows hidden below)
UP, DN = [0x04, 0x06, 0x07, 0x06, 0x04], [0x10, 0x30, 0x70, 0x30, 0x10]
a.u8("BMP_SCROLL", UP + DN + [u | d for u, d in zip(UP, DN)])
a.const("BMP_SCROLL_W", 5)
a.main()
