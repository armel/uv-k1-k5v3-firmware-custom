#!/usr/bin/env python3
"""CW Decode read-only UI, icon and Morse assets."""

import os
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "CW DECODE"
UI = [
    ("T_TITLE", TITLE),
    ("T_RSSI", "RSSI "),
    ("T_THR", "THR "),
    ("T_DBM", "dBm"),
    ("T_WPM", "WPM "),
    ("T_MORSE", "MORSE"),
    ("T_WAIT", "WAITING"),
]

a = Assets("CWDECODE")
ui_size = 0
for name, text in UI:
    padding = -(len(text) + 1) % 4
    a.text(name, text + "\0" * padding)
    ui_size += len(text) + 1 + padding
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))


def load_small_font():
    """Decode the firmware's packed 6x7 font exactly as UI_PrintStringBuffer."""
    with open(os.path.join(HERE, "..", "..", "font.c"), encoding="ascii") as source:
        match = re.search(
            r"const uint8_t gFontSmallPacked\[FONT_SMALL_PACKED_SIZE\]\s*=\s*"
            r"\{(.*?)\n\};", source.read(), re.DOTALL)
    if not match:
        raise RuntimeError("gFontSmallPacked not found in App/font.c")
    packed = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})",
                                                          match.group(1)))
    expected = (94 * 6 * 7 + 7) // 8
    if len(packed) != expected:
        raise RuntimeError(f"unexpected packed small-font size: {len(packed)}")

    def glyph(char):
        bit = (ord(char) - ord(" ") - 1) * 6 * 7
        columns = []
        for _ in range(6):
            byte = bit >> 3
            word = packed[byte] | packed[byte + 1] << 8
            columns.append((word >> (bit & 7)) & 0x7F)
            bit += 7
        return columns

    return glyph


def calibration_screen():
    """Pre-render framebuffer pages 1..4 of the fixed calibration screen."""
    frame = bytearray(7 * 128)
    glyph = load_small_font()

    def print_normal(text, start, end, page):
        if end > start:
            start += ((end - start - len(text) * 7) + 1) // 2
        for index, char in enumerate(text):
            if " " < char < chr(127):
                offset = page * 128 + start + index * 7 + 1
                frame[offset:offset + 6] = bytes(glyph(char))

    text = "CALIBRATING"
    width = len(text) * 7
    text_x = (128 - width) // 2
    end = text_x + width + 1
    print_normal(text, text_x, 0, 2)
    frame[2 * 128 + text_x - 1] ^= 0x7F
    for x in range(text_x, end):
        frame[2 * 128 + x] ^= 0xFF
        frame[1 * 128 + x] ^= 0x80
    frame[2 * 128 + end] ^= 0x7F
    print_normal("KEEP CHANNEL QUIET", 0, 127, 4)
    return frame[128:5 * 128]

morse = bytearray(64)
for index, char in {
    2: "E", 3: "T", 4: "I", 5: "A", 6: "N", 7: "M",
    8: "S", 9: "U", 10: "R", 11: "W", 12: "D", 13: "K", 14: "G", 15: "O",
    16: "H", 17: "V", 18: "F", 20: "L", 22: "P", 23: "J",
    24: "B", 25: "X", 26: "C", 27: "Y", 28: "Z", 29: "Q",
    42: "+", 49: "=", 50: "/",
    32: "5", 33: "4", 35: "3", 39: "2", 47: "1",
    48: "6", 56: "7", 60: "8", 62: "9", 63: "0",
}.items():
    morse[index] = ord(char)
a.raw("MORSE_TREE", morse)

# Decimal places are cached once by the app and avoid divisions in the
# frequently refreshed signal and frequency formatting paths.
a.u32("DECIMAL_PLACES", [10 ** exponent for exponent in range(9, -1, -1)])

up = [0x04, 0x06, 0x07, 0x06, 0x04]
down = [0x10, 0x30, 0x70, 0x30, 0x10]
a.u8("BMP_SCROLL", [
    (u if state & 1 else 0) | (d if state & 2 else 0)
    for state in range(4) for u, d in zip(up, down)
])
a.const("BMP_SCROLL_W", len(up))

f_icon = [0x3E, 0x7F, 0x41, 0x75, 0x75, 0x75, 0x7D, 0x7F, 0x3E]
a.u8("BMP_F", f_icon)
# Inverse "AGC" capsule, same style as the title and the F icon.
a.u8("BMP_AGC", [0x3E, 0x7F, 0x43, 0x75, 0x43, 0x7F, 0x63, 0x5D, 0x45, 0x7F,
                 0x63, 0x5D, 0x5D, 0x7F, 0x3E])
a.raw("BMP_CAL_SCREEN", calibration_screen())
a.main()
