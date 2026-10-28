#!/usr/bin/env bash
# Host tests for decmrz. Needs gcc or clang and is not part of the firmware build.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-gcc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "decmrz host tests"
"$CC" -std=c11 -Wall -Wextra -O2 -o "$TMP/host_decmrz" \
    "$HERE/host_decmrz.c"
"$TMP/host_decmrz"
echo "sondemrz host tests passed"
