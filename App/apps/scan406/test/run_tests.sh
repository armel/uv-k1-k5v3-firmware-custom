#!/usr/bin/env bash
# Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
# Licensed under the Apache License, Version 2.0 (see the C sources).
# Scan 406 host tests: the whole app on a stub API and a simulated radio
# (host_app.c): first and second generation bursts on every channel,
# continuous carriers, blips, the trigger threshold, WATCH mode, EPIRB 406
# missing and the bench mode. Needs a host C compiler and python3.
set -euo pipefail
cd "$(dirname "$0")"
CC=${HOSTCC:-clang}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# app_api.h without its ARM layout asserts (64-bit pointers), in a copy of the
# tree so the app's "../app_api.h" finds it.
mkdir -p "$T/apps/scan406/test"
python3 - "$T/apps/app_api.h" <<'PY'
import re, sys
api = open("../../app_api.h").read()
open(sys.argv[1], "w").write(re.sub(r"_Static_assert\(.*?\);", "", api, flags=re.S))
PY
python3 ../gen_assets.py "$T/apps/scan406/scan406_assets.bin" "$T/apps/scan406/scan406_assets.h"
cp ../scan406_app.c "$T/apps/scan406/"
mkdir -p "$T/apps/epirb406"
cp ../../epirb406/dec406.c ../../epirb406/dec406.h "$T/apps/epirb406/"

# Burst audio as PA4 sees it, from the carrier start (no gap before or after).
E=../../epirb406/test
A="$T/audio"; mkdir -p "$A"
gen() { python3 "$E/gen406.py" --gap-ms 0 --out "$A/$1.u16" --frame "$2" "${@:3}"; }
LONG=$(python3 "$E/frame406.py")
gen long   "$LONG"
gen short  "$(python3 "$E/frame406.py" --short)"
gen self   "$(python3 "$E/frame406.py" --selftest)"
gen flip   "$(python3 "$E/frame406.py" --flip 50)"     # PDF-1: BCH-1 fails
gen flip2  "$(python3 "$E/frame406.py" --flip 120)"    # PDF-2: BCH-1 ok, BCH-2 fails
gen inv    "$LONG" --invert
gen foff   "$LONG" --foff 3000
gen weak   "$LONG" --cnr 15
gen cut    "$LONG"
python3 - "$A/cut.u16" <<'PY'
import sys
p = sys.argv[1]; d = open(p, "rb").read()
open(p, "wb").write(d[:2 * 9600 * 300 // 1000])   # 300 ms: frame sync, then cut
PY
python3 - "$A/nosync.u16" <<'PY'
import sys, numpy as np
rng = np.random.default_rng(3)                     # 0.5 s of plain carrier: DC and a little noise
np.clip(2048 + rng.normal(0, 3, 4800), 0, 4095).astype("<u2").tofile(sys.argv[1])
PY
cp host_app.c "$T/apps/scan406/test/"
$CC -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -o "$T/host_app" "$T/apps/scan406/test/host_app.c"
"$T/host_app" "$T/apps/scan406/scan406_assets.bin" "$A"
