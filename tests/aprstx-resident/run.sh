#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_bin=$(mktemp "${TMPDIR:-/tmp}/aprstx-resident-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O2 -DAPRS_HOST_TEST=1 \
    tests/aprstx-resident/test_resident.c \
    App/apps/aprstx/protocol.c App/apps/aprstx/ui.c \
    App/apps/aprstx-resident/aprstx_resident_app.c \
    App/apps/aprstx-resident/tx_resident.c -o "$test_bin"
"$test_bin"
python3 tests/aprstx-resident/test_pack.py
