#!/usr/bin/env bash
set -euo pipefail
APP_VMA=${APP_VMA:-0x20000280}
CC=/opt/toolchain/bin/arm-none-eabi-gcc
OBJCOPY=/opt/toolchain/bin/arm-none-eabi-objcopy
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    CC=arm-none-eabi-gcc
    OBJCOPY=arm-none-eabi-objcopy
fi

CFLAGS="-mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common
    -fomit-frame-pointer -ffunction-sections -fdata-sections -fstack-usage -flto -Wall -Wextra -Werror"
LDFLAGS="-nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA}
    -Wl,--gc-sections -Wl,-Map=aprstx-resident.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments"

# Compile separately to keep useful per-function .su files beside each object.
"$CC" $CFLAGS -c aprstx_resident_app.c -o aprstx_resident_app.o
"$CC" $CFLAGS -c tx_resident.c -o tx_resident.o
"$CC" $CFLAGS -c ../aprstx/protocol.c -o protocol.o
"$CC" $CFLAGS -c ../aprstx/ui.c -o ui.o
"$CC" $CFLAGS $LDFLAGS aprstx_resident_app.o tx_resident.o protocol.o ui.o \
    -lgcc -o aprstx-resident.elf
"$OBJCOPY" -O binary aprstx-resident.elf aprstx-resident.bin
python3 pack_resident.py aprstx-resident.bin APRSTX-R.app --vma "$APP_VMA"
printf 'APRSTX-R: %s bytes, API >=2, AFSK capability 0x8\n' "$(wc -c < aprstx-resident.bin)"
