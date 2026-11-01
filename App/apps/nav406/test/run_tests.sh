#!/usr/bin/env bash
# Copyright 2026 Johan Denoyer F4WAT, https://github.com/jdenoy
# Licensed under the Apache License, Version 2.0 (see nav406_app.c).
# Nav 406 host tests: the app source on a stub API (host_nav406.c). Needs a
# host C compiler and python3. Set FB_WALK / FB_STOP to file paths to get the
# dial's frame buffer (8 pages x 128 columns, LSB at the top).
set -euo pipefail
cd "$(dirname "$0")"
CC=${HOSTCC:-clang}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# app_api.h without its ARM layout asserts (64-bit pointers), in a copy of the
# tree so the app's "../app_api.h" finds it; app_config_proxy.h reduced to the
# one define the app uses (the full one pulls the firmware headers).
mkdir -p "$T/apps/nav406"
python3 - "$T/apps/app_api.h" <<'PY'
import re, sys
api = open("../../app_api.h").read()
open(sys.argv[1], "w").write(re.sub(r"_Static_assert\(.*?\);", "", api, flags=re.S))
PY
grep "define APP_CONFIG_LOCAL_MAX" ../../app_config_proxy.h > "$T/apps/app_config_proxy.h"
(cd .. && APP_VER=test python3 ./gen_assets.py "$T/assets.bin" "$T/apps/nav406/nav406_assets.h")
# The Cortex-M0+ inline asm (PRIMASK) has no meaning on the host.
sed -E 's/__asm volatile \(.*\);/;/; s/__asm volatile \(.*\)$/0/' ../nav406_app.c > "$T/apps/nav406/nav406_app.c"
"$CC" -w -lm -DAPP_SRC="\"$T/apps/nav406/nav406_app.c\"" -o "$T/host_nav406" host_nav406.c
ASSETS="$T/assets.bin" "$T/host_nav406"
