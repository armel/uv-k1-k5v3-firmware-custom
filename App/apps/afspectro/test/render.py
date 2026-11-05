#!/usr/bin/env python3
"""The AF Spectro screens as afspectro_app.c draws them (draw, header, big,
transcribed with the resident drawing services of ../../af_lcd.py), from the
model's state, written as LCD-look PNGs. Called by model.py --png DIR."""
import os, sys
import model as M
sys.path.insert(0, os.path.join(M.HERE, "..", ".."))
from af_lcd import Lcd, tiny, tenths, png


def asset(name, i=0, n=None):
    off, size = M.OFF[name]
    return M.BLOB[off + i:off + i + (size if n is None else n)]


def text(name, i=0):
    return asset(name, i).split(b"\0")[0].decode()


def header(lcd, app):
    """the C's header(): fixed columns, 4 digits with leading zeros"""
    if app.two:
        lo, hi, tw = app.two_tones()
        o = ("%04d / %04d" % (lo, hi)).rjust(11) + text("T_HZ") + text("T_TW") + tenths(tw).rjust(5) + text("T_DB")
    else:
        o = text("T_PK") + "%04d" % app.f1 + text("T_HZ")
        if app.tmax:
            o += text("T_MIN") + "%04d" % app.tmin + text("T_MAX") + "%04d" % app.tmax
    tiny(lcd, 0, 1, o)


def draw(app):
    lcd = Lcd()
    w, lx, sx = M.CONST["STATUS_W"], M.CONST["LOCK_X"], M.CONST["SPK_X"]
    lcd.status[0:w] = asset("STATUS", app.raw * w, w)
    if app.held or app.clip:
        lcd.status[lx:lx + 9] = asset("BMP_LOCK" if app.held else "BMP_CLIP")
    if app.spk:
        lcd.status[sx:sx + 10] = asset("BMP_SPK")
    lcd.battery()
    header(lcd, app)
    axis = asset("AXIS", app.fsi * 128, 128)
    if app.view == M.V_SONO:
        for p in range(M.SON_PAGES):
            lcd.fb[1 + p][:] = bytes(app.son[p * 128:(p + 1) * 128])
        lcd.fb[6][:] = axis
    else:
        for k in range(M.BINS):
            v = app.px(app.S[k])
            if v:
                lcd.draw_line(k, 32 - v, k, 31)
            if app.hold[k] > v:
                lcd.dot(k, 31 - app.hold[k])
        lcd.fb[4][:] = axis
        lcd.fb[5][:] = bytes(app.strip[:128])
        lcd.fb[6][:] = bytes(app.strip[128:256])
    return lcd


def frames(app, sig):
    k = M.BLOCKS_9K6 << app.fsi
    for f0 in range(0, len(sig) // M.N - k + 1, k):
        for b in range(f0, f0 + k):
            app.analyze(sig[b * M.N:(b + 1) * M.N])
        app.frame()
    return app


def screens(out):
    os.makedirs(out, exist_ok=True)
    N = M.N
    shots = []
    # an APRS packet after hiss: SPECTRO, then the same in SONO
    app = M.App(0, M.V_SPECTRO)
    frames(app, M.hiss(9600, 30 * N, seed=1))
    frames(app, M.afsk(9600, -4.0, 30 * N, seed=2))
    frames(app, M.hiss(9600, 12 * N, seed=3))
    frames(app, M.afsk(9600, -4.0, 30 * N, seed=4))
    shots.append(("spectro_aprs", draw(app)))
    app.view = M.V_SONO
    shots.append(("sono_aprs", draw(app)))
    # a homing beacon's sweep, SONO, RAW
    app = frames(M.App(0, M.V_SONO, raw=True), M.sweep(9600, 120 * N))
    shots.append(("sono_sweep", draw(app)))
    # DTMF '1', SPECTRO, held
    app = frames(M.App(0, M.V_SPECTRO), M.tones(9600, [(697, 350, 0), (1209, 250, 1)], 30 * N, noise=10))
    app.held = True
    shots.append(("spectro_dtmf", draw(app)))
    names = []
    for name, lcd in shots:
        path = os.path.join(out, "afspectro_%s.png" % name)
        png([lcd], path, scale=6, cols=1, gap=12)
        names.append(path)
    sheet = os.path.join(out, "afspectro_screens.png")
    png([lcd for _, lcd in shots], sheet)
    return names + [sheet]
