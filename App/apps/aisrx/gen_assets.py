#!/usr/bin/env python3
# AIS RX read-only assets.
#
# Everything up to the end of MSG_BYTES is one data block that app_main()
# copies once onto its stack (DATA_SIZE in aisrx_app.c): the word records
# first (frequencies, receive set-up), then the UI texts padded to four
# bytes, the status and Inland tables and the message lengths. Only the
# status-bar tail stays in flash, read straight into the status line.
#
#   ./gen_assets.py aisrx_assets.bin aisrx_assets.h
import os
import struct
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "AIS RX"                            # the version is in the .app header
WAIT_CAPS = "WAIT"                          # status-bar capsule until a vessel

UI = [
    ("T_TITLE", TITLE),
    ("T_WAIT", WAIT_CAPS),
    ("T_AIS", "AIS"),                       # over the channel, right of the frequency
    ("T_SOG", "SOG "),                      # "SOG 12.3kt COG 088" under way,
    ("T_KT", "kt "),                        # "SOG 0.0kt HDG 090" below 1 kt
    ("T_COG", "COG "),
    ("T_HDG", "HDG "),
    ("T_ENI", "ENI "),                      # Inland AIS: "ENI 01823456"
    ("T_DBM", "dBm  heard "),               # "-89dBm  heard 3  last 02m15"
    ("T_LAST", "  last "),                  # 32 characters at most, what a tiny row holds
    ("T_SYNC", "sync "),                    # "sync 3  crc 2" until a vessel
    ("T_CRC", "  crc "),
]

a = Assets("AISRX")

# Word-aligned records at offset 0 of the block (the stack copy is aligned).
# AIS1 and AIS2 in 10 Hz units: "AIS 1" / "AIS 2" right of the frequency.
a.prefix("AIS_FREQS", struct.pack("<2I", 16197500, 16202500))
# Receive set-up {register, bits cleared, bits set}, written after saving
# each register and restored in reverse order on exit:
#   REG_2B <10:8> RX HPF300, LPF3K and de-emphasis off, <2:0> cleared
#   REG_73 <4> AFC off
#   REG_43 the widest IF filter
#   REG_47 AF output 9: the bypass that keeps 9600 bit/s (Sonde M20's)
a.prefix("REG_SETUP", struct.pack("<12H",
         0x2B, 0x0007, 0x0700,
         0x73, 0x0000, 0x0010,
         0x43, 0xFFFF, 0x7E2C,
         0x47, 0xFFFF, 0x6942))

for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_WAIT_CHARS", len(WAIT_CAPS))

# Navigational status 0-14 of messages 1-3 (15 = not defined: none shown).
# Eight characters at most: "227123456 Moored" must fit an 18-character
# row.
STATUS = [
    "Underway",     # 0 under way using engine
    "Anchored",     # 1 at anchor
    "Disabled",     # 2 not under command
    "Restrict",     # 3 restricted manoeuvrability
    "Draught",      # 4 constrained by her draught
    "Moored",       # 5
    "Aground",      # 6
    "Fishing",      # 7 engaged in fishing
    "Sailing",      # 8 under way sailing
    "HSC",          # 9 reserved for high-speed craft
    "WIG",          # 10 reserved for wing in ground
    "Towing",       # 11 power-driven vessel towing astern
    "Pushing",      # 12 pushing ahead or towing alongside
    "",             # 13 reserved
    "AIS-SART",     # 14 search and rescue transmitter active
]
assert all(len(name) <= 8 for name in STATUS)
a.table("T_STATUS", STATUS, stride=9)

# Inland AIS (message 8, DAC 200, FI 10): load status 1-2, then the hazard
# code 1-4 (blue cones, B-flag) at index hazard + 1, as gpsd's AIVDM.
a.table("T_INLAND", ["Unloaded", "Loaded", "1 cone", "2 cones", "3 cones",
                     "B-flag"], stride=9)

# Bytes (CRC excluded) of each supported message type, 0 for the others:
# accept_message() looks one entry up instead of a chain of tests. 8
# (Inland) may be longer; 24 part A may carry one padding byte. Last item
# of the stack block.
MSG_BYTES = [0] * 25
for msg_type, nbytes in ((1, 21), (2, 21), (3, 21), (5, 53), (8, 21),
                         (18, 21), (19, 39), (24, 20)):
    MSG_BYTES[msg_type] = nbytes
a.u8("MSG_BYTES", MSG_BYTES)

# Speaker, three blank columns, then newer/older marks. The index bits are:
# bit 0 speaker, bit 1 newer vessel available, bit 2 older vessel available.
SPK = [0x1C, 0x1C, 0x3E, 0x7F, 0x00, 0x22, 0x1C, 0x41, 0x22, 0x1C]
UP = [0x04, 0x06, 0x07, 0x06, 0x04]
DN = [0x10, 0x30, 0x70, 0x30, 0x10]
tail = []
for index in range(8):
    tail += (SPK if index & 1 else [0] * len(SPK)) + [0] * 3
    tail += [(up if index & 2 else 0) | (down if index & 4 else 0)
             for up, down in zip(UP, DN)]
a.u8("BMP_TAIL", tail)
a.const("TAIL_W", len(tail) // 8)

if __name__ == "__main__":
    a.main()
