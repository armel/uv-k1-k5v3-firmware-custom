#!/usr/bin/env bash
# Host tests for decm20. Needs a C compiler (gcc or clang). Run from anywhere.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-gcc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "decm20 host tests"
"$CC" -std=c11 -Wall -Wextra -O2 -o "$TMP/host_decm20" "$HERE/host_decm20.c"
"$TMP/host_decm20"
echo "sondem20 host tests passed"
