#!/usr/bin/env python3
"""The LCD of the AF Scope / AF Spectro models: gStatusLine and gFrameBuffer
with the resident drawing services transcribed (UI_PrintStringSmall*,
GUI_DisplaySmallest*, UI_DrawLineBuffer, the battery) and the firmware's own
fonts from App/font.c, written as LCD-look PNGs. Used by both apps'
test/render.py."""
import os, re, struct, zlib


def cdiv(n, d):
    """C division: truncated toward zero."""
    q = abs(n) // abs(d)
    return q if (n < 0) == (d < 0) else -q


W = 128
FONT_C = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "font.c")
SRC = open(FONT_C).read()


def parse_font(name, width):
    start = re.search(re.escape(name) + r"\s*\[[^=]*=\s*\{", SRC).end()
    rows = []
    for line in SRC[start:SRC.index("};", start)].splitlines():
        line = re.sub(r"/\*.*?\*/", "", line).split("//")[0]
        vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", line)]
        if len(vals) == width:
            rows.append(vals)
    return rows


NORMAL = parse_font("gFontSmall", 6)          # '!'..
BOLD = parse_font("gFontSmallBold", 6)
TINY = parse_font("gFont3x5", 3)              # ' '..


class Lcd:
    """gStatusLine and gFrameBuffer[7][128], with the resident services."""
    def __init__(self):
        self.status = bytearray(W)
        self.fb = [bytearray(W) for _ in range(7)]

    def buf(self, statusbar, line):
        return self.status if statusbar else self.fb[line]

    def pixel(self, statusbar, x, y, on=True):
        if 0 <= x < W and 0 <= y < (8 if statusbar else 56):
            b = self.buf(statusbar, y >> 3)
            if on:
                b[x] |= 1 << (y & 7)
            else:
                b[x] &= ~(1 << (y & 7)) & 0xFF

    def print_small(self, s, start, end, line, font):
        if end > start:
            start += ((end - start) - len(s) * 7 + 1) // 2
        for i, ch in enumerate(s):
            if " " < ch < "\x7f":
                for c in range(6):
                    self.fb[line][start + i * 7 + c] = font[ord(ch) - 33][c] & 0x7F

    def print_normal(self, s, start, end, line): self.print_small(s, start, end, line, NORMAL)
    def print_bold(self, s, start, end, line): self.print_small(s, start, end, line, BOLD)

    def print_tiny(self, s, x, y, statusbar=False):
        for ch in s:
            g = TINY[ord(ch) - 32]
            for i in range(3):
                for j in range(6):
                    if g[i] >> j & 1:
                        self.pixel(statusbar, x + i, y + j)
            x += 4

    def print_inverse(self, s, x, line, statusbar, end):
        self.print_tiny(s, x, line * 8 + 1, statusbar)
        b = self.buf(statusbar, line)
        b[x - 2] ^= 0x3E
        for i in range(x - 1, end):
            b[i] ^= 0x7F
        b[end] ^= 0x3E

    def draw_line(self, x1, y1, x2, y2):
        """UI_DrawLineBuffer: vertical spans, else one pixel per x"""
        if x1 == x2:
            for y in range(min(y1, y2), max(y1, y2) + 1):
                self.pixel(False, x1, y)
        else:
            a = cdiv((y2 - y1) * 1000, x2 - x1)
            b = y1 - cdiv(a * x1, 1000)
            for i in range(min(x1, x2), max(x1, x2) + 1):
                self.pixel(False, i, cdiv(i * a, 1000) + b)

    def battery(self):
        for x in range(110, 127):
            for y in range(1, 6):
                if x in (110, 126) or y in (1, 5) or (112 <= x <= 124 and 2 < y < 5 and x % 3):
                    self.pixel(True, x, y)
        for y in range(2, 5):
            self.pixel(True, 127, y)

    def dot(self, x, y):
        self.pixel(False, x, y)

    def rows(self):
        out = []
        for y in range(64):
            b = self.status if y < 8 else self.fb[(y - 8) >> 3]
            out.append([b[x] >> (y & 7) & 1 for x in range(W)])
        return out


def tiny(lcd, x, y, s):
    """the apps' tiny(): str cut at the screen edge"""
    lcd.print_tiny(s[:(128 - x) >> 2], x, y)


def signed(v):
    return ("+" if v > 0 else "") + str(v)


def tenths(v):
    return ("-" if v < 0 else "") + "%d.%d" % (abs(v) // 10, abs(v) % 10)


def png(lcds, path, scale=5, cols=2, gap=24):
    bg, fg, grid, frame = (214, 216, 230), (38, 38, 70), (200, 202, 218), (60, 60, 60)
    rows = (len(lcds) + cols - 1) // cols
    cw, ch = W * scale, 64 * scale
    tw, th = cols * cw + (cols + 1) * gap, rows * ch + (rows + 1) * gap
    img = [[frame] * tw for _ in range(th)]
    for n, lcd in enumerate(lcds):
        px = lcd.rows()
        ox, oy = gap + (n % cols) * (cw + gap), gap + (n // cols) * (ch + gap)
        for y in range(ch):
            for x in range(cw):
                g = x % scale == scale - 1 or y % scale == scale - 1
                img[oy + y][ox + x] = fg if px[y // scale][x // scale] else (grid if g else bg)
    raw = b"".join(b"\0" + bytes(c for p in row for c in p) for row in img)
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", tw, th, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


