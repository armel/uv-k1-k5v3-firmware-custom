#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_bin=$(mktemp "${TMPDIR:-/tmp}/aprstx-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O2 tests/aprstx/test_aprstx.c \
    App/apps/aprstx/protocol.c App/apps/aprstx/ui.c App/apps/aprstx/aprstx_app.c -o "$test_bin"
"$test_bin"
