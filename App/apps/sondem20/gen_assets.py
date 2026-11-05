#!/usr/bin/env python3
import math, os, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "Sonde M20"
UI = [
    ("T_TITLE",  TITLE),
    ("T_NOPOS",  "no position"),
    ("T_ALT",    "ALT "),
    ("T_M",      "m"),
    ("T_TEMP",   "T "),
    ("T_C",      "C "),
    ("T_RH",     "RH "),
    ("T_FRAME",  "FRAME "),
    ("T_VS", " VS"),
    ("T_HS", "HS "),
    ("T_HDG", "m/s HDG "),
    ("T_DBM",    "dBm"),
    ("T_KHZ",    "kHz"),
]
a = Assets("SONDE_M20")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
SIGNAL = [0x08, 0x1c, 0x1c, 0x08, 0x00, 0x22, 0x1c, 0x41, 0x22, 0x1c]
SPK = [0x1c, 0x1c, 0x3e, 0x7f, 0x00, 0x22, 0x1c, 0x41, 0x22, 0x1c]
a.u8("BMP_SIGNAL", SIGNAL)
a.u8("BMP_SPK", SPK)
a.u8("BMP_F", [0x3e, 0x7f, 0x41, 0x75, 0x75, 0x75, 0x7d, 0x7f, 0x3e, 0x00])
a.const("ICON_W", 10)        # every status icon: F and speaker share one read
a.u16("ATT_REG13", [0x03DF, 0x03DD, 0x03DB, 0x03D9, 0x0379, 0x0139])
a.const("ATT_COUNT", 6)
# Footer labels, one NUL-padded record per attenuation step (ATT_REG13 order).
ATT_REC = 16
ATT_LBL = ["ATT 0dB", "ATT 6dB", "ATT 15dB", "ATT 27dB", "ATT BYP", "ATT BYP+"]
a.raw("ATT_LBL", b"".join(s.encode().ljust(ATT_REC, b"\0") for s in ATT_LBL))
a.const("ATT_REC", ATT_REC)

# M20 main thermistor (auto_rx m20mod get_Temp): ADC12 of a divider per range,
# Steinhart-Hart in ln(R).  Tabulate ADC(T) every 5 C; decreasing with T.
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
# Row 3: the RH sensor NTC (m20mod get_Tntc2): beta 3650, 2.2 k at 25 C, read
# through 22.1 k: ADC = 4095 R / (R + 22.1 k).
for k in range(T_N):
    r = 2.2e3 * math.exp(3650.0 * (1.0 / (T_MIN + T_STEP * k + 273.15) - 1.0 / 298.15))
    tab.append(max(0, min(4095, int(round(4095.0 * r / (r + 22.1e3))))))
a.u16("TEMP_TAB", tab)
a.const("T_N", T_N)
a.const("T_OFS10", -T_MIN * 10)
a.main()
