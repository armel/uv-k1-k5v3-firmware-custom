#!/usr/bin/env python3

# Copyright (c) 2026
#
# Licensed under the MIT License (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at the root of this repository.
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#     limitations under the License.
#

from binascii import crc_hqx
from itertools import cycle


# Quansheng stock updater pack format:
# - raw firmware split at 0x2000
# - 16-byte ASCII version field inserted there
# - XOR obfuscation with the table below
# - CRC16/XMODEM appended at the end
OBFUSCATION = bytes(
    [
        0x47, 0x22, 0xC0, 0x52, 0x5D, 0x57, 0x48, 0x94, 0xB1, 0x60, 0x60, 0xDB, 0x6F, 0xE3, 0x4C, 0x7C,
        0xD8, 0x4A, 0xD6, 0x8B, 0x30, 0xEC, 0x25, 0xE0, 0x4C, 0xD9, 0x00, 0x7F, 0xBF, 0xE3, 0x54, 0x05,
        0xE9, 0x3A, 0x97, 0x6B, 0xB0, 0x6E, 0x0C, 0xFB, 0xB1, 0x1A, 0xE2, 0xC9, 0xC1, 0x56, 0x47, 0xE9,
        0xBA, 0xF1, 0x42, 0xB6, 0x67, 0x5F, 0x0F, 0x96, 0xF7, 0xC9, 0x3C, 0x84, 0x1B, 0x26, 0xE1, 0x4E,
        0x3B, 0x6F, 0x66, 0xE6, 0xA0, 0x6A, 0xB0, 0xBF, 0xC6, 0xA5, 0x70, 0x3A, 0xBA, 0x18, 0x9E, 0x27,
        0x1A, 0x53, 0x5B, 0x71, 0xB1, 0x94, 0x1E, 0x18, 0xF2, 0xD6, 0x81, 0x02, 0x22, 0xFD, 0x5A, 0x28,
        0x91, 0xDB, 0xBA, 0x5D, 0x64, 0xC6, 0xFE, 0x86, 0x83, 0x9C, 0x50, 0x1C, 0x73, 0x03, 0x11, 0xD6,
        0xAF, 0x30, 0xF4, 0x2C, 0x77, 0xB2, 0x7D, 0xBB, 0x3F, 0x29, 0x28, 0x57, 0x22, 0xD6, 0x92, 0x8B,
    ]
)

VERSION_OFFSET = 0x2000
VERSION_SIZE = 16
CRC_SIZE = 2


def _vector_table_looks_valid(image: bytes) -> bool:
    if len(image) < 8:
        return False

    stack_pointer = int.from_bytes(image[0:4], "little")
    reset_vector = int.from_bytes(image[4:8], "little")

    return (stack_pointer & 0xFFF00000) == 0x20000000 and (reset_vector & 0xFFF00000) == 0x08000000


def is_raw_image(image: bytes) -> bool:
    return _vector_table_looks_valid(image)


def has_valid_vendor_crc(image: bytes) -> bool:
    if len(image) < CRC_SIZE:
        return False

    expected_crc = crc_hqx(image[:-CRC_SIZE], 0)
    actual_crc = int.from_bytes(image[-CRC_SIZE:], "little")

    return expected_crc == actual_crc


def is_packed_image(image: bytes) -> bool:
    if len(image) <= VERSION_OFFSET + VERSION_SIZE + CRC_SIZE:
        return False

    if is_raw_image(image):
        return False

    return has_valid_vendor_crc(image)


def decode_packed_image(image: bytes) -> tuple[bytes, str]:
    if not is_packed_image(image):
        raise ValueError("not a packed Quansheng firmware image")

    decoded = bytes(a ^ b for a, b in zip(image[:-CRC_SIZE], cycle(OBFUSCATION)))
    version_bytes = decoded[VERSION_OFFSET:VERSION_OFFSET + VERSION_SIZE]
    version = version_bytes.split(b"\x00", 1)[0].decode("ascii", errors="replace")

    raw = decoded[:VERSION_OFFSET] + decoded[VERSION_OFFSET + VERSION_SIZE:]
    if not is_raw_image(raw):
        raise ValueError("decoded firmware does not look like a raw MCU image")

    return raw, version
