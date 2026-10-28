#!/usr/bin/env python3
import math
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "Sonde M10"
UI = [
    ("T_TITLE", TITLE),
    ("T_NOPOS", "no position"),
    ("T_ALT", "ALT "),
    ("T_M", "m"),
    ("T_TEMP", "T "),
    ("T_C", "C "),
    ("T_RH", "RH "),
    ("T_FRAME", "FRAME "),
    ("T_VS", " VS"),
    ("T_HS", "HS "),
    ("T_HDG", "m/s HDG "),
    ("T_DBM", "dBm"),
    ("T_KHZ", "kHz"),
]
a = Assets("SONDE_M10")
ui_size = 0
for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
    ui_size += len(value) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.u8("BMP_SIGNAL", [0x08, 0x1C, 0x1C, 0x08, 0x00, 0x22, 0x1C, 0x41, 0x22, 0x1C])
a.u8("BMP_SPK", [0x1C, 0x1C, 0x3E, 0x7F, 0x00, 0x22, 0x1C, 0x41, 0x22, 0x1C])
a.u8("BMP_F", [0x3E, 0x7F, 0x41, 0x75, 0x75, 0x75, 0x7D, 0x7F, 0x3E, 0x00])
a.const("ICON_W", 10)        # every status icon: F and speaker share one read
a.u16("ATT_REG13", [0x03DF, 0x03DD, 0x03DB, 0x03D9, 0x0379, 0x0139])
a.const("ATT_COUNT", 6)
# Footer labels, one NUL-padded record per attenuation step (ATT_REG13 order).
ATT_REC = 16
ATT_LBL = ["ATT 0dB", "ATT 6dB", "ATT 15dB", "ATT 27dB", "ATT BYP", "ATT BYP+"]
# Each record: the x that right-aligns the label (3x5 font, 4 px a char),
# then the NUL-padded text.
a.raw("ATT_LBL", b"".join((bytes([128 - 4 * len(s)]) + s.encode()).ljust(ATT_REC, b"\0")
                          for s in ATT_LBL))
a.const("ATT_REC", ATT_REC)

# M10 Shibaura PB5-41E thermistor. Tabulate the Steinhart-Hart conversion
# every 5 C so the overlay only needs integer interpolation at runtime.
SH = (1.07303516e-03, 2.41296733e-04, 2.26744154e-06, 6.52855181e-08)
RS = (12.1e3, 36.5e3, 475.0e3)
RP = (1e20, 330.0e3, 2000.0e3)
T_MIN, T_STEP, T_N = -120, 5, 37


def t_of_r(r):
    l = math.log(r)
    return 1.0 / (SH[0] + SH[1] * l + SH[2] * l * l + SH[3] * l ** 3) - 273.15


def r_of_t(t):
    lo, hi = 1.0, 1e12
    for _ in range(200):
        mid = math.sqrt(lo * hi)
        if t_of_r(mid) > t:
            lo = mid
        else:
            hi = mid
    return lo


tab = []
for sc in range(3):
    for k in range(T_N):
        r = r_of_t(T_MIN + T_STEP * k)
        x = RS[sc] / r + RS[sc] / RP[sc]
        tab.append(max(0, min(4095, int(round(4095.0 / (x + 1.0))))))
a.u16("TEMP_TAB", tab)
a.const("T_N", T_N)
a.const("T_OFS10", -T_MIN * 10)
a.main()
