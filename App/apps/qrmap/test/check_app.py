#!/usr/bin/env python3
# Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
# Licensed under the Apache License, Version 2.0 (see the C sources).
"""QR Map end to end on a stub API (host_app.c): what the screen shows for
each hand-off message and key, decoded from the frame buffer with OpenCV and
ZXing (the engine of many phone scanner apps), which must agree.
With host_dec406, every T.001 frame with a position also goes through EPIRB
406's key 7 message (host_dec406 -m): the code must hold that position
exactly, rounded to 1e-6 degree, and the beacon ID must show as the label.

   ./check_app.py ./host_app ../qrmap_assets.bin [host_dec406]
"""
import os, struct, subprocess, sys
from fractions import Fraction
import numpy as np, cv2, zxingcpp

HOST, ASSETS = sys.argv[1], sys.argv[2]
DEC406 = sys.argv[3] if len(sys.argv) > 3 else None
det = cv2.QRCodeDetector()
fails = 0

def run(msg, keys="", cfg=0):
    out = subprocess.run([HOST, ASSETS, "none" if msg is None else msg.hex(), keys, str(cfg)],
                         capture_output=True, text=True, check=True).stdout.split("\n")
    frames, texts, handoff, saved, i = [], [], None, None, 0
    while i < len(out):
        l = out[i]
        if l == "F":
            frames.append((texts, out[i + 1:i + 65])); texts = []; i += 65; continue
        if l.startswith("T "):
            _, x, y, s = l.split(" ", 3); texts.append((int(x), int(y), s))
        elif l.startswith("H "):
            handoff = l[2:]
        elif l.startswith("C "):
            saved = int(l[2:])
        i += 1
    return frames, handoff, saved

def decode(rows):
    img = np.array([[c == "#" for c in r] for r in rows])
    lcd = np.where(img, 30, 200).astype(np.uint8)
    lcd = cv2.copyMakeBorder(lcd, 6, 6, 6, 6, cv2.BORDER_CONSTANT, value=200)   # blank glass
    img = cv2.resize(lcd, None, fx=4, fy=4, interpolation=cv2.INTER_NEAREST)
    cv = det.detectAndDecode(img)[0]
    zx = [r.text for r in zxingcpp.read_barcodes(img) if r.format == zxingcpp.BarcodeFormat.QRCode]
    return cv if [cv] == zx or (not cv and not zx) else f"OpenCV {cv!r} / ZXing {zx!r}"   # both must agree

def check(name, cond, detail=""):
    global fails
    if not cond:
        fails += 1
        print(f"  FAIL {name} {detail}")

def pos(lat, lon, label=b""):          # centi-arcseconds
    return struct.pack("<ii", lat, lon) + label

def deg(v):                            # what the text must say: exact, rounded
    m = round(Fraction(abs(v), 360000) * 10**6)
    return ("-" if v < 0 and m else "") + f"{m // 10**6}.{m % 10**6:06d}"

# EPIRB 406 sends its decoded position unchanged: 48 07'24" N, 1 14'04" W
# (fine offsets in 4" steps) as centi-arcseconds, then the 15-hex ID.
LAT, LON = (48 * 3600 + 7 * 60 + 24) * 100, -(1 * 3600 + 14 * 60 + 4) * 100
EPIRB = pos(LAT, LON, b"1D0E5A3B2C00001\0")
C = f"{deg(LAT)},{deg(LON)}"
assert C == "48.123333,-1.234444", C
GEO, GOOGLE, TEXT = "geo:" + C, "https://maps.google.com/?q=" + C, C
APPLE = "https://maps.apple.com/?q=" + C
OSM = f"https://osm.org/?mlat={deg(LAT)}&mlon={deg(LON)}"

# A position from EPIRB 406: every format, UP/DOWN wrap, direct keys, return.
frames, handoff, saved = run(EPIRB, "DDDDD" + "U" + "4" + "1", cfg=0)
want = [GOOGLE, TEXT, OSM, GEO, APPLE, GOOGLE, APPLE, OSM, GOOGLE]   # start, D x5 (back), U, 4, 1
got = [decode(f[1]) for f in frames]
check("formats", got == want, f"{got}")
t = {s for _, _, s in frames[0][0]}
check("panel", {"1/5 GOOGLE", "LAT 48.123333", "LON -1.234444", "1D0E5A3B2C00001",
                "UP/DN FORMAT", "EXIT  BACK"} <= t, f"{sorted(t)}")
check("return hand-off", handoff == "EPIRB 406 0", f"{handoff}")
check("format saved", saved == 0, f"{saved}")

# The saved format comes back; junk in the config falls back to geo:.
frames, _, saved = run(EPIRB, "", cfg=2)
check("cfg 2", decode(frames[0][1]) == GEO and saved == 2)
frames, _, _ = run(EPIRB, "", cfg=0xFF)
check("cfg junk", decode(frames[0][1]) == GOOGLE)

# Every centi-arcsecond rounds to the nearest 1e-6 degree: extremes, both
# signs, a rest of 359999 (0.999997, the largest), a sweep, no "-0.000000".
cases = [(32400000, 64800000), (-32400000, -64800000), (0, 0), (-1, 1), (-17, 17),
         (-18, 18), (32399999, -64799999), (359999, -360001), (-12040008, 54444444)]
cases += [(v, -v - 7) for v in range(0, 3600, 61)]
for lat, lon in cases:
    frames, _, _ = run(pos(lat, lon), "5")
    want, got = f"{deg(lat)},{deg(lon)}", decode(frames[1][1])
    check(f"text {lat},{lon}", got == want, f"{got} != {want}")

# No position: from the menu (no hand-off back), too short, out of range.
for name, msg in [("menu", None), ("short", pos(1, 2)[:7]),
                  ("lat range", pos(32400001, 0)), ("lon range", pos(0, -64800001))]:
    frames, handoff, _ = run(msg)
    t = [s for _, _, s in frames[0][0]]
    check(f"nopos {name}", "NO POSITION RECEIVED." in t and decode(frames[0][1]) == "", f"{t}")
    check(f"nopos {name} hand-off", (handoff is None) == (msg is None), f"{handoff}")

# A sender in the sondes' layout (sonde_info_t: id[12] before latS / lonS,
# both in centi-arcseconds): it copies latitude, longitude, then its ID into
# a 20-byte message, as the README shows. M20-like ID, metre-level position.
SONDE_LAT, SONDE_LON = 17589123, 1234567          # 48.858675 N, 3.429353 E
frames, handoff, _ = run(pos(SONDE_LAT, SONDE_LON, b"ME1234567\0\0\0"), "5")
want = f"{deg(SONDE_LAT)},{deg(SONDE_LON)}"
t = [s for x, y, s in frames[1][0] if y == 27]
check("sonde layout", decode(frames[1][1]) == want and t == ["ME1234567"], f"{decode(frames[1][1])} {t}")

# Label: up to 15 characters shown, unprintable bytes as '?', stops at a NUL.
for label, shown in [(b"F4WAT-9", "F4WAT-9"), (b"\x01ab\x7f", "?ab?"),
                     (b"ABCDEFGHIJKLMNOPQRSTUVWX", "ABCDEFGHIJKLMNO"), (b"AB\0CD", "AB")]:
    frames, _, _ = run(pos(0, 0, label))
    t = [s for x, y, s in frames[0][0] if y == 27]
    check(f"label {label!r}", t == [shown], f"{t}")

# EPIRB 406 -> QR Map, over the T.001 catalogue (positions in arc seconds).
n406 = 0
if DEC406:
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", "..", "epirb406", "test"))
    from t001frames import catalogue
    for name, desc, frame, hid, p, fine in catalogue():
        msg = subprocess.run([DEC406, "-m", frame], capture_output=True, text=True,
                             check=True).stdout.strip()
        if p is None:
            check(f"406 {name} no position", msg == "-", msg)
            continue
        frames, handoff, _ = run(bytes.fromhex(msg), "", cfg=4)   # TEXT
        want = f"{deg(p[0] * 100)},{deg(p[1] * 100)}"
        got = decode(frames[0][1])
        label = [s for x, y, s in frames[0][0] if y == 27]
        check(f"406 {name}", got == want and label == [hid], f"{got} {label} != {want} {hid}")
        n406 += 1

if fails:
    sys.exit(f"FAIL app: {fails} checks")
print("app: formats, keys, panel, config, extremes, no-position pages and labels OK"
      + (f"; {n406} EPIRB 406 positions exact through the key 7 message" if DEC406 else ""))
