#!/usr/bin/env python3
# KISS TNC read-only assets: the demodulator's initial state and cosine tables,
# as APRS RX builds them (keep the two in step), and the screen texts.
#
#   ./gen_assets.py kisstnc_assets.bin kisstnc_assets.h
import math, os, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

FS = 9600

def cos_table(f):
    per = FS // math.gcd(FS, f)
    return [int(round(127 * math.cos(2 * math.pi * f * n / FS))) for n in range(per)]

a = Assets("KISSTNC")
a.text("T_TITLE", "KISS TNC")
# status capsule, by state: RX only, host connected, none
a.table("T_CAPS", ["RX ONLY", "USB", "NO USB"])
a.text("T_KEYS", "1 spk 3 RX # lock")
def field(line, xl, xv, label):
    return bytes([line, xl, xv]) + label.encode().ljust(5, b"\0")
# a grid of 7 px columns: the labels at 0 and 9, the values from 4 and 13; the
# counts, then the last station's RSSI and slicer count (line 2) and call (3)
a.raw("FIELDS", field(0, 0, 28, "RX") + field(0, 63, 91, "TX") + field(1, 0, 28, "BAD")
      + field(1, 63, 91, "LOS") + field(2, 0, 28, "dBm") + field(2, 63, 91, "DEC")
      + field(3, 0, 28, "FRM"))
a.raw("BMP_LOCK", bytes([0x7c, 0x46, 0x45, 0x45, 0x45, 0x45, 0x45, 0x46, 0x7c]))
# SetHardware answers, as Dire Wolf's: the name, and the bytes queued follow
a.text("T_HWTNC", "TNC:KISS TNC 2.0")
a.text("T_HWTXBUF", "TXBUF:")
# dem_t prefix: dc, eight filter/correlator words, pm/ps, r/ks, ring; then the
# cosine tables, so one read initializes the whole front end (as APRS RX).
# The start of the app's state g: the KISS parameters by command (TXDELAY 27,
# 40 flags = 267 ms as APRS TX; P 63, one chance in four; SLOTTIME 10, 100 ms;
# TXTAIL 0, 3 flags; FULLDUPLEX 0), no key held, no KISS command yet (0xF0),
# the tone gains, the parser inside a frame (a first frame without its leading
# FEND still parses).
# The tone gains (BK4829 REG_70 <14:8>) put 2200 Hz 6 dB over 1200 Hz, as a
# voice path's pre-emphasis leaves it, measured on a UV-K1 with an SDR: narrow
# 47 / 80 give 1.0 / 2.0 kHz of deviation, 2.3 kHz at the peak; wide 35 / 68
# give 1.5 / 3.0 kHz, 3.2 kHz at the peak (the firmware doubles the deviation on
# wide). The gain is not linear above 66: 99 gave 2.9 kHz on narrow. A narrower
# VFO (6.25 kHz) does not send: no gains.
a.raw("G_INIT", bytes([0, 27, 63, 10, 0, 0, 19, 0xF0, 47, 80, 0, 0, 35, 68, 1]))
a.u32("DEMOD_INIT", [2048 << 4] + [0] * 8 + [64, 64] + [0] * 18)
a.i8("COS1200", cos_table(1200))            # 8 entries
a.i8("COS2200", cos_table(2200))            # 48 entries
a.main()
