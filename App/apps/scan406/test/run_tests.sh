#!/usr/bin/env bash
# Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
# Licensed under the Apache License, Version 2.0 (see the C sources).
# Scan 406 host tests: the whole app on a stub API and a simulated radio
# (host_app.c): first and second generation bursts on every channel,
# continuous carriers, blips, the trigger threshold, inherited VHF/AM/USB
# profiles, the documented 3 kHz ambiguity, AUTO/WATCH alarm timing, EPIRB 406
# missing, a strong beacon leaking into every channel, a late 70 cm test
# transmitter, the bench mode and the 121.5 MHz siren watch. Needs a host C compiler and python3.
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
# Late 70 cm test transmitter (BK4829 FSK): 120 ms of modem preamble (48 ones) ahead of the frame,
# after 184 ms of carrier, 204 ms on every other burst (FIFO word)
gen etx184 "FFFFFFFFFFFF$LONG" --lead-ms 184
gen etx204 "FFFFFFFFFFFF$LONG" --lead-ms 204
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
# 121.5 MHz watch: PA4 audio of a homing siren (1600 to 300 Hz, 2.7 sweeps a
# second, as the LimeSDR bench of 03/11/2026), looped by the test; and what
# must not raise the alarm: a plain carrier, voice-like tones, a rising sweep.
python3 - "$A" <<'PY'
import sys, numpy as np
d = sys.argv[1]; fs = 9600; rng = np.random.default_rng(7)
t = np.arange(int(fs * 370 * 27 / 1000)) / fs          # 27 whole sweeps: loops cleanly
def sweep(rate, hi, lo, rising=False):
    frac = (t * rate) % 1.0
    f = lo + (hi - lo) * frac if rising else hi - (hi - lo) * frac
    return np.sin(2 * np.pi * np.cumsum(f) / fs)
def out(name, x):
    np.clip(2048 + x, 0, 4095).astype("<u2").tofile(f"{d}/{name}.u16")
out("siren", 500 * sweep(1 / 0.37, 1600, 300) + rng.normal(0, 40, len(t)))
out("sirenweak", 200 * sweep(1 / 0.37, 1600, 300) + rng.normal(0, 150, len(t)))
out("siren2hz", 500 * sweep(2.0, 1500, 350) + rng.normal(0, 40, len(t)))
out("carrier", rng.normal(0, 30, len(t)))
out("rising", 500 * sweep(1 / 0.37, 1600, 300, rising=True) + rng.normal(0, 40, len(t)))
v = np.zeros(len(t)); k = 0                             # voice-like: tones of 80 to 300 ms
while k < len(t):
    n = int(fs * rng.uniform(.08, .3)); f = rng.uniform(150, 1200)
    if rng.random() < .75:
        v[k:k + n] = 450 * np.sin(2 * np.pi * f * np.arange(min(n, len(t) - k)) / fs)
    k += n
out("voice", v + rng.normal(0, 40, len(t)))
PY
cp host_app.c "$T/apps/scan406/test/"
$CC -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -o "$T/host_app" "$T/apps/scan406/test/host_app.c"
"$T/host_app" "$T/apps/scan406/scan406_assets.bin" "$A"
