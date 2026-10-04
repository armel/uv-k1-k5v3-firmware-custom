#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DAFSK_TX_HOST_TEST \
    -DENABLE_FEAT_F4HWN_OVERLAY_AFSK_TX -IApp -Itests/afsk_tx \
    App/app/afsk_core.c App/app/afsk_tx.c tests/afsk_tx/test_service.c \
    -o "$out/test_service"
"$out/test_service"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DAFSK_TX_HW_HOST_TEST \
    -DENABLE_FEAT_F4HWN_OVERLAY_AFSK_TX -IApp -Itests/afsk_tx \
    App/app/afsk_hw.c tests/afsk_tx/test_hardware.c -o "$out/test_hardware"
"$out/test_hardware"
