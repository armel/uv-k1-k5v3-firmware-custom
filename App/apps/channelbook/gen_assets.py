#!/usr/bin/env python3
# Copyright 2026 Armel F4HWN
# Licensed under the Apache License, Version 2.0.
"""ChannelBook UI strings, tone names and reusable screen artwork."""
import os
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

W = 128


def tiny_font():
    """Load the resident 3x5 font so static UI chrome is pixel-identical."""
    source = (Path(__file__).resolve().parents[2] / "font.c").read_text()
    source = source.split("const uint8_t gFont3x5[", 1)[1]
    source = source.split("=", 1)[1].split(";", 1)[0]
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    font = [int(value, 0) for value in
            re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", source)]
    assert len(font) == 96 * 3
    return font


TINY = tiny_font()


def pages(pixels, width, height):
    assert height % 8 == 0
    return bytes(sum(1 << bit for bit in range(8)
                     if (x, page * 8 + bit) in pixels)
                 for page in range(height // 8) for x in range(width))


def tiny_row(text, y=2):
    """Centred 3x5 text, ready to copy directly into one framebuffer page."""
    row = bytearray(W)
    x = (W - len(text) * 4) // 2
    for char in text:
        glyph = TINY[(ord(char) - 32) * 3:][:3]
        for column, value in enumerate(glyph):
            row[x + column] |= value << y
        x += 4
    return bytes(row)


def capsule_row(text, chars=None):
    """Inverse 3x5 capsule matching GUI_DisplaySmallestInverse exactly."""
    chars = chars or len(text)
    end = 2 + chars * 4
    row = bytearray(end + 1)
    x = 2
    for char in text:
        glyph = TINY[(ord(char) - 32) * 3:][:3]
        for column, value in enumerate(glyph):
            row[x + column] |= value << 1
        x += 4
    row[0] ^= 0x3E
    for x in range(1, end):
        row[x] ^= 0x7F
    row[end] ^= 0x3E
    return bytes(row)


def tiny_action_width(action):
    key, label = action
    return len(key) * 4 + 3 + 2 + len(label) * 4 - 1


def draw_tiny_action(row, action, x):
    key, label = action
    capsule = capsule_row(key)
    row[x:x + len(capsule)] = capsule
    x += len(capsule) + 2
    for char in label:
        glyph = TINY[(ord(char) - 32) * 3:][:3]
        for column, value in enumerate(glyph):
            row[x + column] |= value << 1
        x += 4


def tiny_three_actions(left, center, right):
    """Place three key capsules and labels across one framebuffer row."""
    row = bytearray(W)
    positions = (0, (W - tiny_action_width(center)) // 2,
                 W - tiny_action_width(right))
    for action, x in zip((left, center, right), positions):
        draw_tiny_action(row, action, x)
    return bytes(row)


ALERT_W, ALERT_H = 23, 32


def alert():
    """Large radio-memory card with an exclamation mark."""
    px = set()
    for x in range(2, ALERT_W - 2):
        px.add((x, 3)); px.add((x, 27))
    for y in range(5, 26):
        px.add((0, y)); px.add((ALERT_W - 1, y))
    px.update(((1, 4), (ALERT_W - 2, 4), (1, 26), (ALERT_W - 2, 26)))
    for y in range(9, 18):
        px.update(((10, y), (11, y), (12, y)))
    px.update(((10, 22), (11, 22), (12, 22)))
    return pages(px, ALERT_W, ALERT_H)


a = Assets("CHANNELBOOK")
a.text("T_SCAN_LISTS", "SCAN LISTS")
a.text("T_ALPHANUM_INDEX", "ALPHANUM INDEX")
a.text("T_MEM_NUM_INDEX", "MEM NUM INDEX")
a.text("T_ALL", "ALL Channels")
a.text("T_NONE", "Out of list")
a.text("T_LIST", "LIST")
a.text("T_CHANNEL", "CHANNEL")
a.text("T_NOT_FOUND", "NOT FOUND")
a.text("T_EMPTY", "NO CHANNELS")
a.text("T_PROGRAM", "PROGRAM WITH CHIRP")
a.raw("A_ALPHA_GROUPS", b"".join(label.encode().ljust(8, b"\0") for label in
                               ("0-9", "ABC", "DEF", "GHI", "JKL",
                                "MNO", "PQRS", "TUV", "WXYZ", "NO NAME")))

a.raw("ART_ALERT", alert())
a.raw("ART_STATUS", capsule_row("CHANNELBOOK"))
a.raw("ART_FOOT_GROUP_SCAN", tiny_three_actions(("MENU", "OPEN"),
                                                ("F", "ALPHANUM"),
                                                ("EXIT", "QUIT")))
a.raw("ART_FOOT_GROUP_ALPHA", tiny_three_actions(("MENU", "OPEN"),
                                                 ("F", "MEM NUM"),
                                                 ("EXIT", "QUIT")))
a.raw("ART_FOOT_GROUP_RANGE", tiny_three_actions(("MENU", "OPEN"),
                                                 ("F", "LISTS"),
                                                 ("EXIT", "QUIT")))
# Keep the three list footers contiguous: the app selects one by catalog index.
a.raw("ART_FOOT_LIST", tiny_three_actions(("MENU", "TUNE"),
                                           ("F", "ALPHANUM"),
                                           ("EXIT", "BACK")))
a.raw("ART_FOOT_LIST_ALPHA", tiny_three_actions(("MENU", "TUNE"),
                                                 ("F", "MEM NUM"),
                                                 ("EXIT", "BACK")))
a.raw("ART_FOOT_LIST_RANGE", tiny_three_actions(("MENU", "TUNE"),
                                                 ("F", "LISTS"),
                                                 ("EXIT", "BACK")))
a.raw("ART_FOOT_SEARCH", tiny_row(""))
a.raw("ART_FOOT_EXIT", tiny_row("EXIT CLOSE"))
a.const("ALERT_W", ALERT_W)
a.const("ALERT_PAGES", ALERT_H // 8)
a.const("ALPHA_LABEL_LEN", 8)

if __name__ == "__main__":
    a.main()
