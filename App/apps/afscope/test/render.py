#!/usr/bin/env python3
"""The AF Scope screens as afscope_app.c draws them (draw, drawScope,
drawTune, col, transcribed with the resident drawing services of
../../af_lcd.py), from the model's state, written as LCD-look PNGs. Called
by model.py --png DIR."""
import os, sys
import model as M
sys.path.insert(0, os.path.join(M.HERE, "..", ".."))
from af_lcd import Lcd, cdiv, tiny, signed, png


def asset(name, i=0, n=None):
    off, size = M.OFF[name]
    return M.BLOB[off + i:off + i + (size if n is None else n)]


def text(name, i=0):
    return asset(name, i).split(b"\0")[0].decode()


def col(lcd, x, a, b, mid, half, amp):
    ya = M.clamp(mid - cdiv(a * half, amp), mid - half, mid + half)
    yb = M.clamp(mid - cdiv(b * half, amp), mid - half, mid + half)
    lcd.draw_line(x, ya, x, yb)


def peak_amp(app):
    return max(app.mx - app.dc, app.dc - app.mn, 8)


def rjust(o, w):
    """the C's rjust(): right-aligned in w characters, longer left as it is"""
    return o.rjust(w)


def cdef(name):
    import re
    return int(re.search(r"#define\s+%s\s+(\d+)" % name, M.C_SRC).group(1))


def put_rf(app):
    f = app.rf10 // 100
    return "%d.%03d" % (f // 1000, f % 1000)


def big(lcd, x, s):
    for ch in s:
        i = 10 if ch == "-" else ord(ch) - 48
        g = asset("BIG", i * 20, 20)
        lcd.fb[0][x:x + 10] = g[:10]
        lcd.fb[1][x:x + 10] = g[10:]
        x += 12
    return x


def draw_scope(lcd, app):
    d = 1 << app.dsh
    amp = (2048 >> (app.gain - 1)) if app.gain else peak_amp(app)
    grid = asset("SCOPE_GRID")
    for i in range(5):
        lcd.fb[i][:] = grid[i * 128:(i + 1) * 128]
    p = app.i0
    last = app.smp[p] - app.dc
    for x in range(128):
        lo = hi = last
        for j in range(d):
            v = app.smp[p] - app.dc
            p += 1
            lo, hi, last = min(lo, v), max(hi, v), v
        col(lcd, x, lo, hi, 19, 18, amp)
    if app.pers:
        for i in range(5):
            for x in range(128):
                app.persbuf[i * 128 + x] |= lcd.fb[i][x]
                lcd.fb[i][x] = app.persbuf[i * 128 + x]
    if not app.fmean:
        o = " " * (cdef("F_FREQ") + 2)                # blank: no reading
    else:
        o = "%04d" % app.fmean if app.steady else "%04d / %04d" % (app.flo, app.fhi)
        o = rjust(o, cdef("F_FREQ")) + text("T_HZ")
    o += text("T_PP") + rjust(str(app.pp), cdef("F_PP")) + \
        text("T_DC") + rjust(signed(app.dc), cdef("F_DC"))
    tiny(lcd, 0, 41, o)
    o = "%s %s %s%s" % (rjust(put_rf(app), cdef("F_RF")), text("T_TB", app.tb * M.CONST["T_TB_STRIDE"]),
                        text("T_GAIN", app.gain * M.CONST["T_GAIN_STRIDE"]),
                        text("T_EDGE", app.edge * M.CONST["T_EDGE_STRIDE"]))
    if app.pers:
        o += text("T_PERS")
    tiny(lcd, 0, 49, o)


def draw_tune(lcd, app):
    tone = app.favg != 0
    o = str(app.ftone) if tone else text("T_NONE")
    x = big(lcd, 2, o)
    lcd.print_normal(text("T_HZ"), x + 1, 0, 1)
    lcd.print_tiny(text("T_STAB"), 98, 0)
    lcd.fb[2][:] = asset("TUNE_SCALE", app.span * 128, 128)
    if tone:
        lcd.print_bold("+-%d" % min(app.stab, 99), 98, 0, 1)
        best, dd = M.nearest([t for t, _ in M.TONES], app.ftone)
        n = M.clamp(cdiv(dd * 50, M.SPAN[app.span]), -50, 50)
        for x1, y1, x2, y2 in ((62 + n, 20, 64 + n, 20), (62 + n, 23, 64 + n, 23),
                               (62 + n, 20, 62 + n, 23), (64 + n, 20, 64 + n, 23)):
            lcd.draw_line(x1, y1, x2, y2)             # draw_rect
        o = "%s %s" % (text("T_TONES", best * M.CONST["T_TONES_STRIDE"]), signed(dd))
        tiny(lcd, 64 - len(o) * 2, 25, o)
    for i in range(0, 128, 4):
        lcd.dot(i, 39)
    amp = 8
    if tone:
        s, cnt = [0] * 64, [0] * 64
        p8, t0 = app.p8, app.t0
        inv, ph = cdiv(64 << 16, p8), 256 - (t0 & 255)
        for n2 in range((t0 >> 8) + 1, app.n):
            while ph >= p8:
                ph -= p8
            b = (ph * inv) >> 16
            s[b] += app.smp[n2] - app.dc; cnt[b] += 1
            ph += 256
        for b in range(64):
            if cnt[b]:
                s[b] = cdiv(s[b], cnt[b])
            elif b:
                s[b] = s[b - 1]
            amp = max(amp, abs(s[b]))
    else:
        amp = peak_amp(app)
    prev = 0
    for i in range(128):
        v = s[i & 63] if tone else app.smp[i] - app.dc
        col(lcd, i, prev if i else v, v, 39, 7, amp)
        prev = v
    o = "%s%s%s%s%s%s" % (rjust(put_rf(app), cdef("F_RF")), text("T_RSSI"), rjust(str(app.rssi), cdef("F_RSSI")),
                         text("T_DBM"),
                         text("T_PP"), rjust(str(app.pp), cdef("F_PP")))
    tiny(lcd, 0, 49, o)


def draw(app):
    lcd = Lcd()
    w, lx, sx = M.CONST["STATUS_W"], M.CONST["LOCK_X"], M.CONST["SPK_X"]
    lcd.status[0:w] = asset("STATUS", app.raw * w, w)
    if app.held or app.clip:
        lcd.status[lx:lx + 9] = asset("BMP_LOCK" if app.held else "BMP_CLIP")
    if app.spk:
        lcd.status[sx:sx + 10] = asset("BMP_SPK")
    lcd.battery()
    (draw_scope if app.view == M.V_SCOPE else draw_tune)(lcd, app)
    return lcd


def screens(out):
    os.makedirs(out, exist_ok=True)
    shots = []
    # SCOPE: a 1750 Hz call tone, 3.3 ms per division, speaker on
    app = M.App(M.V_SCOPE, tb=2, spk=True)
    app.run(M.tones(app.fs, [(1750, 500, 0.3)], app.n, noise=20, seed=2))
    shots.append(("scope_1750", draw(app)))
    # SCOPE: Bell 202 at 0.8 ms per division, RAW, held
    app = M.App(M.V_SCOPE, tb=0, raw=True)
    app.run(M.afsk(app.fs, -3.0, app.n, seed=4))
    app.held = True
    shots.append(("scope_afsk", draw(app)))
    # SCOPE: 13 ms per division over the start of a packet (peak detect), x1
    app = M.App(M.V_SCOPE, tb=4, gain=1)
    sig = M.hiss(app.fs, 200, sigma=300, seed=3) + M.afsk(app.fs, -3.0, app.n - 200, seed=5)
    app.run(sig)
    shots.append(("scope_burst", draw(app)))
    # SCOPE: persistence over 24 blocks of Bell 202, 1.7 ms per division
    app = M.App(M.V_SCOPE, tb=1, pers=1)
    sig = M.afsk(app.fs, -3.0, 24 * app.n, seed=6)
    for b in range(24):
        app.analyze(sig[b * app.n:(b + 1) * app.n])
        lcd = draw(app)
    shots.append(("scope_pers", lcd))
    # TUNE: a 1752 Hz call tone, then voice (no reading)
    app = M.App(M.V_TUNE)
    app.run(M.tones(app.fs, [(1752, 400, 0.3)], 6 * app.n, noise=20, seed=7))
    shots.append(("tune_1750", draw(app)))
    app = M.App(M.V_TUNE)
    app.run(M.voice(app.fs, 4 * app.n))
    shots.append(("tune_voice", draw(app)))
    names = []
    for name, lcd in shots:
        path = os.path.join(out, "afscope_%s.png" % name)
        png([lcd], path, scale=6, cols=1, gap=12)
        names.append(path)
    sheet = os.path.join(out, "afscope_screens.png")
    png([lcd for _, lcd in shots], sheet)
    return names + [sheet]
