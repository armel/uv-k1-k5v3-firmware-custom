#!/usr/bin/env python3
# Shared artwork helpers for overlay-app title screens (used by gen_assets.py).
#
# Canvas is a 128x64 1-bit picture. Canvas.pages() packs it the way the LCD
# buffers are laid out: 8 pages of 128 column bytes, LSB = top row; page 0 is
# the status line, pages 1..7 the frame buffer. An app streams the result into
# A->status_line and A->fb with two asset_read calls.
#
# The logo font draws letters as stroke skeletons: polylines on a 14-row grid
# (x right, y down), scaled, drawn with a square pen and sheared into italics.
# Corners are cut by diagonal segments (chamfered arcade look), and each word
# is outlined by a one-pixel ring drawn one blank pixel away from the letters.
W, H = 128, 64
PROMPT_TOP = 56   # page 7 stays blank: the app blinks its prompt there

LOGO = {
    "A": (9,  [[(0, 13), (0, 3), (3, 0), (6, 0), (9, 3), (9, 13)], [(0, 8), (9, 8)]]),
    "B": (9,  [[(0, 13), (0, 0), (7, 0), (9, 2), (9, 4), (7, 6), (0, 6)],
               [(7, 6), (9, 8), (9, 11), (7, 13), (0, 13)]]),
    "C": (9,  [[(9, 0), (3, 0), (0, 3), (0, 10), (3, 13), (9, 13)]]),
    "D": (9,  [[(0, 0), (6, 0), (9, 3), (9, 10), (6, 13), (0, 13), (0, 0)]]),
    "E": (9,  [[(9, 0), (0, 0), (0, 13), (9, 13)], [(0, 6), (6, 6)]]),
    "I": (6,  [[(0, 0), (6, 0)], [(3, 0), (3, 13)], [(0, 13), (6, 13)]]),
    "K": (9,  [[(0, 0), (0, 13)], [(9, 0), (3, 6), (0, 6)], [(3, 6), (9, 12), (9, 13)]]),
    "L": (8,  [[(0, 0), (0, 13), (8, 13)]]),
    "M": (12, [[(0, 13), (0, 0), (6, 6), (12, 0), (12, 13)]]),
    "N": (9,  [[(0, 13), (0, 0), (9, 13), (9, 0)]]),
    "O": (9,  [[(3, 0), (6, 0), (9, 3), (9, 10), (6, 13), (3, 13), (0, 10), (0, 3), (3, 0)]]),
    "P": (9,  [[(0, 13), (0, 0), (7, 0), (9, 2), (9, 5), (7, 7), (0, 7)]]),
    "R": (9,  [[(0, 13), (0, 0), (7, 0), (9, 2), (9, 5), (7, 7), (0, 7)], [(5, 7), (9, 11), (9, 13)]]),
    "S": (9,  [[(9, 0), (2, 0), (0, 2), (0, 4), (2, 6), (7, 6), (9, 8), (9, 11), (7, 13), (0, 13)]]),
    "T": (10, [[(0, 0), (10, 0)], [(5, 0), (5, 13)]]),
    "U": (9,  [[(0, 0), (0, 10), (3, 13), (6, 13), (9, 10), (9, 0)]]),
}
LOGO_KX, LOGO_KY = 1.0, 1.1    # default skeleton scale
LOGO_BRUSH = 3                 # stroke thickness in pixels
LOGO_GAP = 3                   # pixels between letters
LOGO_SHEAR = 4                 # one pixel of slant every LOGO_SHEAR rows
LOGO_RING = 2                  # outline distance from the letters (0 = none)

def line(pts, x0, y0, x1, y1):
    """Bresenham segment into the pts set."""
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        pts.add((x0, y0))
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy

def logo_glyph(ch, kx, ky):
    """(advance width, pixel set) of one logo letter, before shearing."""
    width, polylines = LOGO[ch]
    skeleton = set()
    for poly in polylines:
        pts = [(round(x * kx), round(y * ky)) for x, y in poly]
        for a, b in zip(pts, pts[1:]):
            line(skeleton, *a, *b)
    pixels = {(x + dx, y + dy) for x, y in skeleton
              for dx in range(LOGO_BRUSH) for dy in range(LOGO_BRUSH)}
    return round(width * kx) + LOGO_BRUSH, pixels

class Canvas:
    def __init__(self):
        self.px = [[0] * W for _ in range(H)]

    def set(self, x, y, on=1):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = on

    def rect(self, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y)

    def sprite(self, x, y, cols, scale=1):
        """Column-major sprite (LSB = top row), as the apps store them."""
        for i, c in enumerate(cols):
            for bit in range(8):
                if c >> bit & 1:
                    for dx in range(scale):
                        for dy in range(scale):
                            self.set(x + i * scale + dx, y + bit * scale + dy)

    def word(self, text, y, kx=LOGO_KX, ky=LOGO_KY):
        """Centered, outlined italic logo word; returns its letter height."""
        height = round(13 * ky) + LOGO_BRUSH
        glyphs = [logo_glyph(ch, kx, ky) for ch in text]
        total = sum(w for w, _ in glyphs) + LOGO_GAP * (len(glyphs) - 1) + (height - 1) // LOGO_SHEAR
        x = (W - total) // 2
        solid = set()
        for width, pixels in glyphs:
            for gx, gy in pixels:
                solid.add((x + gx + (height - 1 - gy) // LOGO_SHEAR, y + gy))
            x += width + LOGO_GAP
        grown = [solid]                    # solid dilated by 0, 1, ... pixels
        for _ in range(LOGO_RING):
            grown.append({(px + dx, py + dy) for px, py in grown[-1]
                          for dx in (-1, 0, 1) for dy in (-1, 0, 1)})
        ring = grown[-1] - grown[-2] if LOGO_RING else set()
        for px, py in solid | ring:
            self.set(px, py)
        return height

    def two_words(self, first, second):
        """Logo on two lines from the top of the screen; returns the bottom row."""
        top = LOGO_RING                    # keep the outline on screen
        height = self.word(first, top)
        second_top = top + height + 2 * LOGO_RING + 1
        return second_top + self.word(second, second_top) + LOGO_RING

    def pages(self):
        """Title-screen payload: status page then the 7 frame-buffer pages."""
        if any(self.px[y][x] for y in range(PROMPT_TOP, H) for x in range(W)):
            raise SystemExit("title: the bottom page is reserved for the prompt")
        out = bytearray()
        for page in range(H // 8):
            for x in range(W):
                b = 0
                for bit in range(8):
                    if self.px[page * 8 + bit][x]:
                        b |= 1 << bit
                out.append(b)
        return bytes(out)

    def preview(self, path, scale=4):
        """Binary PGM, LCD-like colours (dark pixels on a pale panel)."""
        with open(path, "wb") as f:
            f.write(b"P5 %d %d 255\n" % (W * scale, H * scale))
            for y in range(H):
                row = bytes((40 if self.px[y][x] else 200)
                            for x in range(W) for _ in range(scale))
                f.write(row * scale)
