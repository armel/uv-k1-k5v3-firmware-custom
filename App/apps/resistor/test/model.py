#!/usr/bin/env python3
# Python model of resistor_app.c: the same state, keys, value formatting and
# rendering, through models of the firmware drawing calls (fonts taken from
# the uncompressed references kept in App/font.c). No compiler needed.
#
#   python3 test/model.py            # self-checks, then screens in test/out/
#
# The value formatter is checked against an exact Fraction reference over
# every band combination; the screens are PNG mockups of the LCD.
import os, re, struct, sys, zlib
from fractions import Fraction
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import gen_assets as GA

W, H = 128, 64
KEY_0, KEY_9, KEY_MENU, KEY_UP, KEY_DOWN, KEY_EXIT, KEY_STAR, KEY_F = 0, 9, 10, 11, 12, 13, 14, 15
TOL_Y, CODE_LINE, VALUE_Y, BANDS_X, FN_X = 8, 5, 49, 40, 80   # as in resistor_app.c

# ---------------------------------------------------------------- assets ---
_assets = GA.build()
_blob, _lines, TEXT_MAX = _assets.build()
OFF = {n: (o, l) for n, o, l in _lines}
CONST = dict(_assets.extra)
def asset(name): o, l = OFF[name]; return _blob[o:o + l]
def cstr(b): return b.split(b"\0")[0].decode()
def text(name): return cstr(asset(name))
def entry(name, i):
    o, _ = OFF[name]; st = CONST[name + "_STRIDE"]
    return cstr(_blob[o + i * st:o + (i + 1) * st])
MASKS = struct.unpack("<6H", asset("MASKS"))
LAYOUT, LAYOUT_X, DEFAULTS = asset("LAYOUT"), asset("LAYOUT_X"), asset("DEFAULTS")
OHM, PLUS_MINUS = asset("OHM"), asset("PLUS_MINUS")
ART_BODY, ART_FILL = asset("ART_BODY"), asset("ART_FILL")

# ----------------------------------------------------------------- fonts ---
FONT_C = os.path.join(HERE, "..", "..", "..", "font.c")
def parse_font(name, width):
    src = open(FONT_C).read()
    start = re.search(re.escape(name) + r"\s*\[[^=]*=\s*\{", src).end()
    body = src[start:src.index("};", start)]
    rows = []
    for line in body.splitlines():
        line = re.sub(r"/\*.*?\*/", "", line).split("//")[0]
        vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", line)]
        if len(vals) == width:
            rows.append(vals)
    return rows
BOLD = parse_font("gFontSmallBold", 6)    # '!'..
TINY = parse_font("gFont3x5", 3)          # ' '..

# ---------------------------------------------------------------- screen ---
class Screen:
    """Status line (page 0) + frame buffer pages 1..7, as column bytes."""
    def __init__(self): self.clear()
    def clear(self): self.pages = [bytearray(W) for _ in range(8)]
    def fb(self, line): return self.pages[1 + line]
    def buf(self, statusbar, line=0): return self.pages[0] if statusbar else self.fb(line)
    def pixel(self, statusbar, x, y):
        if x < W and y < (8 if statusbar else 56):
            self.buf(statusbar, y >> 3)[x] |= 1 << (y & 7)
    # UI_PrintStringSmallBold
    def print_bold(self, s, start, end, line):
        if end > start: start += ((end - start) - len(s) * 7 + 1) // 2
        for i, ch in enumerate(s):
            if " " < ch < "\x7f":
                for c in range(6):
                    self.fb(line)[start + i * 7 + 1 + c] = BOLD[ord(ch) - 33][c] & 0x7F
    # GUI_DisplaySmallest
    def print_tiny(self, s, x, y, statusbar):
        for ch in s:
            g = TINY[ord(ch) - 32]
            for i in range(3):
                for j in range(6):
                    if g[i] >> j & 1:
                        self.pixel(statusbar, x + i, y + j)
            x += 4
    # GUI_DisplaySmallestInverse
    def print_inverse(self, s, x, line, statusbar, end):
        self.print_tiny(s, x, line * 8 + 1, statusbar)
        b = self.buf(statusbar, line)
        b[x - 2] ^= 0x3E
        for i in range(x - 1, end): b[i] ^= 0x7F
        b[end] ^= 0x3E
    def battery(self):                    # placeholder for draw_battery
        b = self.pages[0]
        for x in range(110, 127):
            b[x] |= 0x3E if x in (110, 126) else 0x22 | (0x1C if x % 3 and 112 <= x <= 124 else 0)
        b[127] |= 0x1C
    def png(self, path, scale=4):
        fg, bg = (40, 40, 40), (180, 200, 160)
        raw = bytearray()
        for y in range(H):
            row = bytearray([0])
            for x in range(W):
                on = self.pages[y // 8][x] >> (y % 8) & 1
                row += bytes(fg if on else bg) * scale
            raw += bytes(row) * scale
        def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, H * scale, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))

# ----------------------------------------------------------------- model ---
def flip(x): return x if 0 <= x < 10 else 9 - x      # colour <-> power of ten

class App:
    def __init__(self, cfg=b"\xff" * 7):
        self.type, self.roles = DEFAULTS[0], list(DEFAULTS[1:])
        if 3 <= cfg[0] <= 6 and all(cfg[1 + r] < 12 and MASKS[r] >> cfg[1 + r] & 1 for r in range(6)):
            self.type, self.roles = cfg[0], list(cfg[1:])
        self.sel, self.fn, self.star_dir, self.running = 0, False, 1, True
    def role(self, i): return LAYOUT[(self.type - 3) * 6 + i]
    def move_band(self, d):
        if d > 0: self.sel = 0 if self.sel + 1 >= self.type else self.sel + 1
        else: self.sel = (self.sel or self.type) - 1
    def step_colour(self, d):
        r = self.role(self.sel); c = self.roles[r]
        while True:
            c = (c + d) % 12
            if MASKS[r] >> c & 1: break
        self.roles[r] = c
    def key(self, k, repeat=False, nav=1):
        """nav: what nav_dir gives for UP (+1, or -1 with the other SET_NAV)."""
        d = nav if k == KEY_UP else -nav if k == KEY_DOWN else 0
        if d:
            self.fn = False; self.move_band(d); return
        if k == KEY_STAR:
            if not repeat:
                self.star_dir = -1 if self.fn else 1; self.fn = False
            self.step_colour(self.star_dir); return
        if repeat: return
        if k == KEY_F:
            self.fn = not self.fn; return
        self.fn = False
        if k <= KEY_9:
            r = self.role(self.sel)
            if MASKS[r] >> k & 1:
                self.roles[r] = k; self.move_band(1)
        elif k == KEY_MENU: self.cycle()
        elif k == KEY_EXIT: self.running = False
    def cycle(self):
        e = flip(self.roles[GA.ROLE_MULT])
        if self.type == 4 and e > -2:
            self.roles[GA.ROLE_D2] = 0; self.roles[GA.ROLE_MULT] = flip(e - 1)
        elif self.type == 6 and self.roles[GA.ROLE_D2] == 0 and e < 9:
            self.roles[GA.ROLE_MULT] = flip(e + 1)
        self.type = 3 if self.type == 6 else self.type + 1
        if self.sel >= self.type: self.sel = self.type - 1
    def value_text(self):
        """'4.7 k ' style: value, blank, prefix, blank cell for the ohm sign."""
        n = 3 if self.type >= 5 else 2
        digits = "".join(str(self.roles[i]) for i in range(n)).lstrip("0")
        k = 0
        if not digits:
            s = "0"
        else:
            p = len(digits) + flip(self.roles[GA.ROLE_MULT])
            while k < 3 and p > 3: p -= 3; k += 1
            if p <= 0: s = "0." + "0" * -p + digits
            elif p >= len(digits): s = digits + "0" * (p - len(digits))
            else: s = digits[:p] + "." + digits[p:]
            if "." in s: s = s.rstrip("0").rstrip(".")
        return s + " " + ("kMG"[k - 1] if k else "") + " "
    def render(self, scr):
        scr.clear()
        scr.print_inverse(text("T_TITLE"), 2, 0, True, 2 + len(text("T_TITLE")) * 4)
        scr.print_tiny(str(self.type) + text("T_BANDS"), BANDS_X, 1, True)
        if self.fn: scr.print_inverse(text("T_F"), FN_X, 0, True, FN_X + 4)
        scr.battery()
        s = self.value_text()
        scr.print_bold(s, 0, W - 1, 0)
        ox = (W - len(s) * 7) // 2 + (len(s) - 1) * 7 + 1
        for c, b in enumerate(OHM): scr.fb(0)[ox + c] |= b
        for p in range(GA.BODY_PAGES):
            scr.fb(GA.BODY_LINE + p)[:] = ART_BODY[p * W:(p + 1) * W]
        tol = self.roles[GA.ROLE_TOL] if self.type > 3 else GA.NO_TOL
        s = " " + entry("T_TOLV", tol)
        if self.type == 6: s += "  " + entry("T_TCV", self.roles[GA.ROLE_TC]) + text("T_PPM")
        tx = W // 2 - ((len(s) * 4 - 1) >> 1)
        scr.print_tiny(s, tx, TOL_Y, False)
        for c, b in enumerate(PLUS_MINUS): scr.fb(TOL_Y >> 3)[tx + c] |= (b << (TOL_Y & 7)) & 0xFF
        for i in range(self.type):
            x = LAYOUT_X[(self.type - 3) * 6 + i]
            role = self.role(i); colour = self.roles[role]
            for p in range(GA.BODY_PAGES):
                fb = scr.fb(GA.BODY_LINE + p)
                for c in range(x, x + GA.BAND_W):
                    fill = ART_FILL[p * W + c]
                    if i == self.sel: fb[c] |= fill
                    else: fb[c] = (fb[c] & ~fill & 0xFF) | (fill if c in (x, x + GA.BAND_W - 1) else 0)
            cx = x + GA.BAND_W // 2
            code = entry("T_COLOUR", colour)
            tx = cx - ((CONST["CODE_CHARS"] * 4 - 1) >> 1)
            if i == self.sel: scr.print_inverse(code, tx, CODE_LINE, False, tx + CONST["CODE_CHARS"] * 4)
            else: scr.print_tiny(code, tx, CODE_LINE * 8 + 1, False)
            v = str(colour) if role < GA.ROLE_MULT else entry("T_MULT", colour) if role == GA.ROLE_MULT else None
            if v: scr.print_tiny(v, cx - ((len(v) * 4 - 1) >> 1), VALUE_Y, False)

# ---------------------------------------------------------------- checks ---
def reference(app):
    n = 3 if app.type >= 5 else 2
    return Fraction(int("".join(str(app.roles[i]) for i in range(n)))) * Fraction(10) ** flip(app.roles[GA.ROLE_MULT])

def parse(s):
    num, pre = s[:-1].split(" ")
    return Fraction(num) * {"": 1, "k": 1000, "M": 10 ** 6, "G": 10 ** 9}[pre]

def check_values():
    count = 0
    for t in (4, 5):
        app = App(); app.type = t
        n = 3 if t >= 5 else 2
        for code in range(10 ** n):
            ds = [int(c) for c in str(code).zfill(n)]
            for m in range(12):
                app.roles[:n] = ds; app.roles[GA.ROLE_MULT] = m
                s = app.value_text()
                assert parse(s) == reference(app), (ds, m, s)
                mant = s.split(" ")[0]
                assert len(mant.replace(".", "").lstrip("0")) <= 3 and len(s) <= 7, s
                assert not ("." in mant and mant.endswith(("0", "."))), s
                if reference(app) >= 1000: assert Fraction(mant) >= 1, s    # no 0.47k
                count += 1
    return count

def check_cycle():
    app = App()
    for i in range(300):
        before_t, before = app.type, reference(app)
        app.key(KEY_MENU)
        if before_t == 4 and flip(app.roles[GA.ROLE_MULT]) > -2:
            assert reference(app) == before, (before, app.roles)
        for r in range(6): assert MASKS[r] >> app.roles[r] & 1
        app.key(KEY_STAR if i % 3 else KEY_UP)

def check_keys():
    app = App()
    assert app.type == 4 and app.value_text() == "4.7 k " and app.sel == 0
    for k in (1, 0, 3, 1):                      # brown black orange brown: 10 k 1 %
        app.key(k)
    assert app.value_text() == "10 k " and app.roles[GA.ROLE_TOL] == 1 and app.sel == 0
    app.key(KEY_DOWN); assert app.sel == 3      # arrows wrap around
    app.key(KEY_UP); app.key(KEY_UP); assert app.sel == 1
    app.key(KEY_UP, nav=-1); assert app.sel == 0  # the other SET_NAV
    app.sel = 3
    app.key(0); assert app.roles[GA.ROLE_TOL] == 1 and app.sel == 3   # black: no tolerance
    for _ in range(12):                         # the tolerance cycles over 10 colours
        app.key(KEY_STAR); assert MASKS[GA.ROLE_TOL] >> app.roles[GA.ROLE_TOL] & 1
    app.sel = 2                                 # multiplier, orange
    app.key(KEY_F); assert app.fn
    app.key(KEY_STAR); assert app.roles[GA.ROLE_MULT] == 2 and not app.fn   # F STAR: back
    app.key(KEY_STAR, repeat=True); assert app.roles[GA.ROLE_MULT] == 1     # held: still back
    app.key(KEY_STAR); assert app.roles[GA.ROLE_MULT] == 2                  # new press: forward
    app.key(KEY_F); app.key(KEY_F); assert not app.fn                       # F F: disarmed
    app.key(KEY_F); app.key(KEY_UP); assert not app.fn                      # any key disarms
    app.sel = 2; app.roles[GA.ROLE_MULT] = 0                                # black, back:
    app.key(KEY_F); app.key(KEY_STAR); app.key(KEY_STAR, True); app.key(KEY_STAR, True)
    assert app.roles[GA.ROLE_MULT] == 9                                     # SR, GD, WH
    assert App(cfg=bytes([6, 1, 0, 0, 2, 1, 1])).value_text() == "10 k "
    assert App(cfg=bytes([6, 1, 0, 0, 2, 0, 1])).type == 4   # black tolerance: defaults
    app.key(KEY_EXIT); assert not app.running

if __name__ == "__main__":
    print("values checked:", check_values())
    check_cycle(); check_keys()
    print("cycle and keys OK")
    out = os.path.join(HERE, "out"); os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png"): os.remove(os.path.join(out, f))
    scr = Screen()
    shots = [("4bands_4k7", App(), [KEY_UP, KEY_UP]),
             ("3bands_220", App(cfg=bytes([3, 2, 2, 0, 1, 10, 1])), [KEY_DOWN]),
             ("5bands_fn_armed", App(), [KEY_MENU, KEY_UP, KEY_UP, KEY_UP, KEY_F]),
             ("6bands_tc", App(cfg=bytes([6, 2, 3, 4, 5, 6, 7])), [KEY_DOWN]),
             ("4bands_0r47", App(cfg=bytes([4, 4, 7, 0, 11, 2, 1])), [])]
    for name, app, keys in shots:
        for k in keys: app.key(k)
        app.render(scr)
        scr.png(os.path.join(out, name + ".png"))
        print(f"{name}: {app.value_text()!r}")
