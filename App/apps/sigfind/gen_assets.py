#!/usr/bin/env python3
# Sig Finder (Signal Finder) read-only assets: texts, attenuator ladder and
# icons (bytes copied verbatim from App/bitmaps.c).
#
#   ./gen_assets.py sigfind_assets.bin sigfind_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

a = Assets("SIGFIND")
a.text("T_TITLE", "SIG DF")
a.text("T_DB", "dB")
a.text("T_DBM", "dBm")
a.text("T_PK", "PK ")
# swept-tone detector: status tag and the line above the graph
a.text("T_ELT", "ELT")
a.text("T_ELT_SP", "ELT ")
a.text("T_AF_SP", "AF ")
a.text("T_HZ", "Hz")
a.text("T_AMP", " A")
a.text("T_SCORE", " S")
# bottom-left tag: FOLLOW at each decay rate (decayIdx), then SWEEP
a.table("T_MODE", ["FOL .5dB/s", "FOL 1dB/s", "FOL 2dB/s", "FOL 4dB/s", "SWEEP"])
# attenuator tag by attStep: dB below the bypass steps, then the bypasses;
# auto (A) first, then manual (M)
ATT = ["0dB", "6dB", "15dB", "27dB", "BYP", "BYP+"]
a.table("T_ATT", ["A " + x for x in ATT] + ["M " + x for x in ATT])
# listen mode label: AM / FM from the VFO, USB from the app
a.table("T_AF", ["AM", "FM", "USB"])
a.u16("ATT_REG13", [0x03DF, 0x03DD, 0x03DB, 0x03D9, 0x0379, 0x0139])   # REG_13 gain bits by attStep
a.u8("BMP_SIGNAL",  [0x08,0x1c,0x1c,0x08,0x00,0x22,0x1c,0x41,0x22,0x1c])
a.u8("BMP_SPEAKER", [0x1c,0x1c,0x3e,0x7f,0x00,0x22,0x1c,0x41,0x22,0x1c])
a.u8("BMP_F",       [0x3e,0x7f,0x41,0x75,0x75,0x75,0x7d,0x7f,0x3e])
a.u8("BMP_LOCK",    [0x7c,0x46,0x45,0x45,0x45,0x45,0x45,0x46,0x7c])
# Powers of ten, 10^8 down to 1: numbers (the frequency too) are printed by
# subtraction, so the app links no division.
a.u32("PLACE", [10 ** k for k in range(8, -1, -1)])
a.main()
