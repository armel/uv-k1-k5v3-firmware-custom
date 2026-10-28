#!/usr/bin/env python3
# Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
# Licensed under the Apache License, Version 2.0 (see the C sources).
# QR Map read-only assets: the screen texts and the QR code formats.
#
# A format is one fixed-stride entry of three NUL-terminated parts: the name
# shown on screen, the text before the latitude, the text between latitude
# and longitude. The longest one, with the longest coordinates
# ("-90.000000", "-180.000000"), must fit the 53 bytes of a version 3-L code.
#
#   ./gen_assets.py qrmap_assets.bin qrmap_assets.h
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets

QR_MAX = 53                                  # qr.h
COORDS = len("-90.000000") + len("-180.000000")

FORMATS = [                                   # the first one is the default
    ("GOOGLE", "https://maps.google.com/?q=", ","),       # Android and iOS, app or browser
    ("APPLE",  "https://maps.apple.com/?q=",  ","),       # iOS Maps, a browser elsewhere
    ("GEO",    "geo:",                        ","),       # Android: the map app chooser
    ("OSM",    "https://osm.org/?mlat=",      "&mlon="),  # OpenStreetMap, marker
    ("TEXT",   "",                            ","),       # plain decimal degrees
]

a = Assets("QRMAP")
a.text("T_LAT", "LAT ")
a.text("T_LON", "LON ")
a.text("T_KEYS", "UP/DN FORMAT")
a.text("T_BACK", "EXIT  BACK")
a.text("T_QUIT", "EXIT  QUIT")
# Launched from the menu, or a message without a valid position.
a.text("T_NOPOS", "QR MAP\0\0NO POSITION RECEIVED.\0OPEN IT FROM AN APP THAT\0"
                  "SENDS ONE: EPIRB 406, KEY 7.\0\0EXIT  LEAVE")
a.const("NOPOS_LINES", 7)
entries = []
for name, pre, mid in FORMATS:
    assert len(pre) + len(mid) + COORDS <= QR_MAX, name
    entries.append(f"{name}\0{pre}\0{mid}")
a.table("T_FMT", entries)
a.const("N_FMT", len(FORMATS))
a.main()
