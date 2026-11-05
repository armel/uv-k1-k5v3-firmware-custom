#!/usr/bin/env bash
# Host tests for decm10. Needs gcc or clang and is not part of the firmware build.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-gcc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "decm10 host tests"
"$CC" -std=c11 -Wall -Wextra -O2 -o "$TMP/host_decm10" "$HERE/host_decm10.c"
"$TMP/host_decm10"
echo "sondem10 host tests passed"
