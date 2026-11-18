#!/usr/bin/env python3
"""CW Keyer variant read-only UI assets and receive profile."""

import os
import struct
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

USB_VARIANT = os.environ.get("CW_KEYER_USB") == "1"
MEM_VARIANT = os.environ.get("CW_KEYER_MEM") == "1"
TITLE = "CW USB" if USB_VARIANT else "CW MEM" if MEM_VARIANT else "CW KEYER"

a = Assets("CWUSB" if USB_VARIANT else "CWMEM" if MEM_VARIANT else "CWKEYER")
a.text("T_TITLE", TITLE)
# build.sh passes the version it packs into the .app header.
if USB_VARIANT:
    version = os.environ.get("APP_VER", "dev")
    a.table("T_VERSION", ["AUTO", "IAMB A", "IAMB B"], stride=16)
else:
    a.text("T_VERSION", os.environ.get("APP_VER", "dev"))
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

# BMP_F gets a blank tenth column: both icons are drawn with one length.
a.u8("BMP_F", [0x3E, 0x7F, 0x41, 0x75, 0x75, 0x75, 0x7D, 0x7F, 0x3E, 0x00])
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
if MEM_VARIANT:
    a.text("T_HELP_STEP", "Any key stops a message")
# The left column, then the right one; "" leaves a slot empty. The keyers
# use lines 0 to 6: on the right, PTT and the contacts level with 0 and
# 1 KEY MODE, then 7, 8, F and UP/DN with its 100 Hz step. CW Mem uses
# lines 1 to 6 and a hint line below.
a.table("T_HELP_KEYS", [
    "1-9 SEND", "0 EDIT", "F+UP/DN NR", "UP/DN FREQ", "MENU HELP", "",
    "EXIT BACK", "KEY/PTT BACK", "", "", "", "",
] if MEM_VARIANT else [
    "0 SPEAKER", "1 KEY MODE", "2 WPM", "3 TONE", "4 BREAK-IN", "5 ORDER", "6 XTAL",
] + ([
    "PTT KEYS CW", "USB KEY/PAD", "7 CW MEM APP", "8 KEYER", "F+ DECREASE",
    "UP/DN 100 Hz", "",
] if USB_VARIANT else [
    "PTT KEYS CW", "SIDE1/2 KEY", "7 CW MEM APP", "F+ DECREASE", "UP/DN 100 Hz",
    "", "",
]))
a.const("T_TITLE_CHARS", len(TITLE))

if MEM_VARIANT:
    from app_assets import cdefine
    U = lambda n: cdefine("cwmem/cwmem_user.h", n)
    # The user block (cwmem/cwmem_user.h) starts the assets: CW Mem Edit
    # writes it in place through the configuration proxy. The callsign is
    # empty (boot message LINE 1) until the editor sets one.
    CUSTOM = ["UR RST 599 599", "NAME", "QTH"]
    user = struct.pack("<I", U("MEMU_MAGIC")) + b"\0" * U("MEMU_CALL_MAX")
    user += b"".join(m.encode().ljust(U("MEMU_MSG_STRIDE"), b"\0") for m in CUSTOM)
    assert len(user) == U("MEMU_LEN") and len(CUSTOM) == U("MEMU_MSG_COUNT")
    a.prefix("MEM_USER", user)
    # CW Mem: the six messages. \x01 is the callsign, \x02 the serial
    # number (NR, three digits at least). Fixed stride, read whole by
    # asset_read; packed raw so they do not raise TEXT_MAX.
    CALL, NR = "\x01", "\x02"
    MSGS = [f"CQ CQ CQ DE {CALL} {CALL} K", CALL, f"5NN {NR}", f"TU {CALL}",
            "TU 73", "QRZ?"]
    MSG_STRIDE = 32
    assert all(len(m) < MSG_STRIDE for m in MSGS)
    a.raw("MSG", b"".join(m.encode().ljust(MSG_STRIDE, b"\0") for m in MSGS))
    a.const("MSG_STRIDE", MSG_STRIDE)
    # Main-screen capsules, three columns: 2 and 3 get the callsign and NR
    # appended, 7 to 9 the start of their custom message.
    a.table("T_LABEL", ["1 CQ", "2 ", "3 5NN ", "4 TU", "5 TU 73", "6 QRZ?",
                        "7 ", "8 ", "9 "])
    a.text("T_NOEDIT", "NO EDIT")
    a.text("T_WPM", " WPM")
    a.table("T_START", ["Open CW Mem from", "CW Keyer or CW USB", "with key 7"])
    # Morse for ',' to 'Z': root bit 1, then dit 0 / dah 1 per element.
    MORSE = {
        "A": ".-", "B": "-...", "C": "-.-.", "D": "-..", "E": ".", "F": "..-.",
        "G": "--.", "H": "....", "I": "..", "J": ".---", "K": "-.-", "L": ".-..",
        "M": "--", "N": "-.", "O": "---", "P": ".--.", "Q": "--.-", "R": ".-.",
        "S": "...", "T": "-", "U": "..-", "V": "...-", "W": ".--", "X": "-..-",
        "Y": "-.--", "Z": "--..", "0": "-----", "1": ".----", "2": "..---",
        "3": "...--", "4": "....-", "5": ".....", "6": "-....", "7": "--...",
        "8": "---..", "9": "----.", ",": "--..--", "-": "-....-", ".": ".-.-.-",
        "/": "-..-.", ":": "---...", ";": "-.-.-.", "=": "-...-", "?": "..--..",
        "@": ".--.-.",
    }
    def code(m):
        c = 1
        for e in m:
            c = c << 1 | (e == "-")
        assert c < 256
        return c
    a.u8("MORSE", [code(MORSE[chr(c)]) if chr(c) in MORSE else 0
                   for c in range(ord(","), ord("Z") + 1)])
a.main()
