#!/usr/bin/env python3
# Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
# Licensed under the Apache License, Version 2.0 (see the C sources).
"""QR Map encoder checks, against the host build of qr.c (host_qr).

1. Bit-exact: every code equals the one the qrcode library makes with the
   same version (3), level (L), mode (byte) and mask, for messages of every length 0-53,
   random bytes, and the four map formats at the coordinate extremes.
2. Readable as on the radio: the code drawn as QR Map draws it, 2 pixels
   per module at (4, 3) on the 128 x 64 LCD, text beside it, decodes to the
   same text with OpenCV and with ZXing (the engine of many phone scanner
   apps). Required with blank glass around the pixels;
   with a dark bezel right at the screen edge (1.5 modules of quiet zone
   above and below) the count is only reported: how far the radio's bezel
   sits from the pixels is for the test on the radio to tell.

Needs qrcode and opencv-python (numpy).   ./check_qr.py ./host_qr
"""
import random, subprocess, sys
from fractions import Fraction
import numpy as np, cv2, qrcode, zxingcpp
from qrcode.util import QRData, MODE_8BIT_BYTE

HOST = sys.argv[1] if len(sys.argv) > 1 else "./host_qr"
N, QR_X, QR_Y, PANEL_X = 29, 4, 3, 68

def deg(v):          # centi-arcseconds to 6 decimals, rounded (exact fraction)
    m = round(Fraction(abs(v), 3600 * 100) * 10**6)
    return ("-" if v < 0 and m else "") + f"{m // 10**6}.{m % 10**6:06d}"

FORMATS = [("https://maps.google.com/?q=", ","), ("https://maps.apple.com/?q=", ","), ("geo:", ","),
           ("https://osm.org/?mlat=", "&mlon="), ("", ",")]
POS = [(0, 0), (32400000, 64800000), (-32400000, -64800000),      # centi-arcseconds
       (17324444, -444444), (-12040008, 54444444), (-1, 1), (35999999, -35999999)]

rnd = random.Random(406)
msgs = [bytes(rnd.randrange(256) for _ in range(n)) for n in range(54)]
msgs += [bytes(rnd.randrange(256) for _ in range(rnd.randrange(54))) for _ in range(200)]
texts = [(pre + deg(la) + mid + deg(lo)).encode() for pre, mid in FORMATS for la, lo in POS]
assert max(map(len, texts)) <= 53
msgs += texts

out = subprocess.run([HOST], input="".join(m.hex() + "\n" for m in msgs),
                     capture_output=True, text=True, check=True).stdout.split("\n")
masks = [0] * 8
for msg, line in zip(msgs, out):
    f = line.split()
    mask, rows = int(f[0]), [int(h, 16) for h in f[1:]]
    ours = np.array([[(r >> x) & 1 for x in range(N)] for r in rows], dtype=np.uint8)
    ref = qrcode.QRCode(version=3, error_correction=qrcode.constants.ERROR_CORRECT_L,
                        mask_pattern=mask, border=0)
    ref.add_data(QRData(msg, mode=MODE_8BIT_BYTE))
    ref.make(fit=False)
    theirs = np.array(ref.get_matrix(), dtype=np.uint8)
    if not np.array_equal(ours, theirs):
        sys.exit(f"FAIL bit-exact: {msg!r} mask {mask}")
    masks[mask] += 1
print(f"bit-exact: {len(msgs)} codes equal to the qrcode library's, masks used {masks}")

def screen(rows, scale, blur, edge):
    img = np.zeros((64, 128), dtype=np.uint8)          # 1 = dark pixel
    for y, r in enumerate(rows):
        for x in range(N):
            if (r >> x) & 1:
                img[QR_Y + 2 * y:QR_Y + 2 * y + 2, QR_X + 2 * x:QR_X + 2 * x + 2] = 1
    noise = np.random.default_rng(674)                 # text-like noise beside it
    for y in (0, 10, 17, 27, 42, 49):
        img[y:y + 5, PANEL_X:128] = noise.integers(0, 2, (5, 128 - PANEL_X))
    lcd = np.where(img, 30, 200).astype(np.uint8)      # grey LCD, not pure black/white
    lcd = cv2.copyMakeBorder(lcd, 6, 6, 6, 6, cv2.BORDER_CONSTANT, value=edge)
    lcd = cv2.resize(lcd, None, fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST)
    return cv2.GaussianBlur(lcd, (0, 0), blur) if blur else lcd

# Three views of each screen. OpenCV's detector alone misses a few codes at
# exactly x6 without blur, whatever the margins: a quirk of its sampling,
# not of the code (x4, x8 and a blurred x6, closer to a camera, all read).
VIEWS = [(4, 0), (8, 0), (6, 2)]
GLASS, BEZEL = 200, 10
det = cv2.QRCodeDetector()
lines = dict(zip(msgs, out))
bad, bezel, bezelZx = 0, 0, 0
def zx(img):
    r = [r.text for r in zxingcpp.read_barcodes(img) if r.format == zxingcpp.BarcodeFormat.QRCode]
    return r[0] if len(r) == 1 else ""
for t in texts:
    rows = [int(h, 16) for h in lines[t].split()[1:]]
    for scale, blur in VIEWS:
        img = screen(rows, scale, blur, GLASS)
        for dec, got in (("OpenCV", det.detectAndDecode(img)[0]), ("ZXing", zx(img))):
            if got != t.decode():
                bad += 1
                print(f"  {dec} (x{scale}, blur {blur}): {t.decode()!r} -> {got!r}")
        img = screen(rows, scale, blur, BEZEL)
        bezel += det.detectAndDecode(img)[0] == t.decode()
        bezelZx += zx(img) == t.decode()
n = len(texts) * len(VIEWS)
if bad:
    sys.exit(f"FAIL screen: {bad} of {2 * n} decodes failed")
print(f"screen: {len(texts)} map codes decoded from the simulated LCD, {len(VIEWS)} views each, "
      f"OpenCV and ZXing; with a dark bezel at the edge OpenCV {bezel} of {n}, ZXing {bezelZx} of {n}")
