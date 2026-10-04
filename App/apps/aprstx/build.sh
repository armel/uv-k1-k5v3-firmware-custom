#!/usr/bin/env bash
# Same freestanding build and pinned RAM VMA as the stock overlay examples.
set -euo pipefail
APP_VMA=${APP_VMA:-0x20000280}
CC=/opt/toolchain/bin/arm-none-eabi-gcc
OBJCOPY=/opt/toolchain/bin/arm-none-eabi-objcopy
command -v arm-none-eabi-gcc >/dev/null 2>&1 && { CC=arm-none-eabi-gcc; OBJCOPY=arm-none-eabi-objcopy; }
CFLAGS="-mcpu=cortex-m0plus -mthumb -Os -flto -fno-inline-functions-called-once -std=gnu11 -ffreestanding -fno-builtin -fno-common -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -Wextra -Werror -fstack-usage"
LDFLAGS="-nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA} -Wl,--gc-sections -Wl,-Map=aprstx.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments"
"$CC" $CFLAGS $LDFLAGS aprstx_app.c protocol.c ui.c tx_stock.c -lgcc -o aprstx.elf
"$OBJCOPY" -O binary aprstx.elf aprstx.bin
python3 ../pack_app.py aprstx.bin APRSTX.app --name APRSTX --ver 0.1-stock --api-min 1 --vma "$APP_VMA" --shortcut none
