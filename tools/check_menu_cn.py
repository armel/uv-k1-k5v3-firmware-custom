#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Round-trip check for the generated App/ui/menu_cn.c: decode the octal
escapes back to text, verify GB2312 validity and the 48 px column width."""
import re
import sys

src = open("App/ui/menu_cn.c", "rb").read().decode("latin-1")
cases = re.findall(r'case (MENU_\w+): return "((?:\\[0-7]{3}|[^"])*)";', src)

def decode(s):
    out = b""
    i = 0
    while i < len(s):
        if s[i] == "\\" and s[i + 1].isdigit():
            out += bytes([int(s[i + 1:i + 4], 8)])
            i += 4
        else:
            out += s[i].encode("latin-1")
            i += 1
    return out

ok = True
for mid, esc in cases:
    raw = decode(esc)
    try:
        cn = raw.decode("gb2312")
    except Exception:
        print("DECODE FAIL:", mid, esc)
        ok = False
        continue
    w = 0
    i = 0
    while i < len(raw):
        if raw[i] >= 0xA1 and i + 1 < len(raw) and raw[i + 1] >= 0xA1:
            w += 16
            i += 2
        else:
            w += 8
            i += 1
    if w > 48:
        print("OVERWIDTH:", mid, cn, w, "px")
        ok = False
        continue
    print("%-20s %-10s %3d px" % (mid, cn, w))
print("---- ALL OK ----" if ok else "---- ERRORS ----")
sys.exit(0 if ok else 1)
