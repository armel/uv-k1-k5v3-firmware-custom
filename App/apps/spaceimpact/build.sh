#!/usr/bin/env bash
# Build the Space Impact overlay blob inside the project toolchain image.
set -euo pipefail

APP="$(basename "$PWD")"
APP_NAME="SpaceImpact"
APP_VER="1.1"
APP_API_MIN=2
APP_VMA=${APP_VMA:-0x20000280}
OUT="${APP_NAME// /}"

CC=/opt/toolchain/bin/arm-none-eabi-gcc
OBJCOPY=/opt/toolchain/bin/arm-none-eabi-objcopy
command -v arm-none-eabi-gcc >/dev/null 2>&1 && { CC=arm-none-eabi-gcc; OBJCOPY=arm-none-eabi-objcopy; }

CFLAGS="-mcpu=cortex-m0plus -mthumb -Oz -std=gnu11 -ffreestanding -fno-builtin -fno-common \
  -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -Wextra"
LDFLAGS="-nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA} \
  -Wl,--gc-sections -Wl,-Map=${APP}.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments"

rm -f ./*.app ./*.elf ./*.bin

step() { printf '\r  🔨 %-13s [%d/4] %-8s' "$APP_NAME" "$1" "$2"; }
trap 'printf "\r  ❌ %-13s build failed            \n" "$APP_NAME"' ERR

step 1 assets  ; python3 ./gen_assets.py "${APP}_assets.bin" "${APP}_assets.h"
step 2 compile ; "$CC" $CFLAGS $LDFLAGS "${APP}_app.c" -lgcc -o "${APP}.elf"
step 3 objcopy ; "$OBJCOPY" -O binary "${APP}.elf" "${APP}.bin"
step 4 pack    ; python3 ../pack_app.py "${APP}.bin" "${OUT}.app" \
                   --name "$APP_NAME" --ver "$APP_VER" --api-min "$APP_API_MIN" \
                   --vma "${APP_VMA}" --screensaver --assets "${APP}_assets.bin" >/dev/null
trap - ERR

BYTES=$(wc -c < "${APP}.bin")
if [ "$BYTES" -gt 4096 ]; then
  printf '\r  🚨 %-13s OVERFLOWS 4 KiB (%d B)     \n' "$APP_NAME" "$BYTES"; exit 1
fi
printf '\r  ✅ %-13s %4d B  (%d%% of 4 KiB)  ->  %s.app   \n' \
       "$APP_NAME" "$BYTES" "$(( BYTES * 100 / 4096 ))" "$OUT"
