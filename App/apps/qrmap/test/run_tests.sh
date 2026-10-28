#!/usr/bin/env bash
# Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
# Licensed under the Apache License, Version 2.0 (see the C sources).
# QR Map host tests. The encoder (qr.c) built for the host is checked against
# the qrcode library and decoded with OpenCV from a simulated LCD; then the
# whole app runs on a stub API and its screens are decoded, EPIRB 406's
# T.001 positions included. Needs a host C compiler, python3, qrcode,
# opencv-python and zxing-cpp
# (pip install qrcode opencv-python-headless zxing-cpp).
set -euo pipefail
cd "$(dirname "$0")"
CC=${HOSTCC:-clang}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

$CC -O2 -Wall -Wextra -o "$T/host_qr" host_qr.c
python3 check_qr.py "$T/host_qr"

# The app on the host: app_api.h without its ARM layout asserts (64-bit
# pointers), in a copy of the tree so the app's "../app_api.h" finds it.
mkdir -p "$T/apps/qrmap/test"
python3 - "$T/apps/app_api.h" <<'PY'
import re, sys
api = open("../../app_api.h").read()
open(sys.argv[1], "w").write(re.sub(r"_Static_assert\(.*?\);", "", api, flags=re.S))
PY
python3 ../gen_assets.py "$T/apps/qrmap/qrmap_assets.bin" "$T/apps/qrmap/qrmap_assets.h"
cp ../qrmap_app.c ../qr.c ../qr.h "$T/apps/qrmap/"
cp host_app.c "$T/apps/qrmap/test/"
$CC -O1 -Wall -Wextra -o "$T/host_app" "$T/apps/qrmap/test/host_app.c"
# EPIRB 406's decoder, for its key 7 message (host_dec406 -m)
$CC -O2 -o "$T/host_dec406" ../../epirb406/test/host_dec406.c ../../epirb406/dec406.c
python3 check_app.py "$T/host_app" "$T/apps/qrmap/qrmap_assets.bin" "$T/host_dec406"
