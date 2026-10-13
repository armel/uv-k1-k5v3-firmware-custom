#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
FLAGS=(-std=c11 -Wall -Wextra -Werror -fno-builtin -fsanitize=address,undefined)
cc "${FLAGS[@]}" model_test.c -o "$TMP_DIR/model"
"$TMP_DIR/model"
cc "${FLAGS[@]}" -DDTMF_HOST_TEST app_test.c ../dtmfmonitor_app.c -o "$TMP_DIR/app"
"$TMP_DIR/app"
