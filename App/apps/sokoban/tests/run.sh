#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -fno-builtin -fsanitize=address,undefined)
cc "${flags[@]}" tests/model_test.c -o "$task_tmp/model"
"$task_tmp/model"
python3 tests/test_levels.py
python3 gen_assets.py "$task_tmp/assets.bin" "$task_tmp/sokoban_assets.h"
cc "${flags[@]}" -I "$task_tmp" tests/app_test.c -o "$task_tmp/app"
"$task_tmp/app" "$task_tmp/assets.bin"
