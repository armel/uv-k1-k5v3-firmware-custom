#!/usr/bin/env python3
"""Build the firmware asset pack and the release package.

The .fw_assets section of the ELF (fonts, menu texts, QR codes: see
App/fw_assets.h) is linked at addresses that do not exist on the MCU and is
stripped from the firmware image. This script extracts it and writes:

  --pack  the asset pack, exactly as stored in the asset zone of a firmware
          slot: a 32-byte FWA1 header followed by the section bytes;
  --out   the release package: firmware image + asset pack + 16-byte trailer.
          UV Studio flashes the image through the bootloader, then writes the
          pack over the serial link.

FWA1 header (little endian):
  0  u32  magic "FWA1"
  4  u16  version (1)
  6  u16  header size (32)
  8  u32  payload size
  12 u32  payload CRC-32 (zlib)
  16 u8[16] GNU build-id (MD5) of the firmware

Package trailer (little endian, last 16 bytes of the file):
  0  u32  magic "FWAT"
  4  u32  image size
  8  u32  pack size (header + payload)
  12 u32  CRC-32 (zlib) of image + pack
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

PACK_MAGIC = 0x31415746      # "FWA1"
PACK_VERSION = 1
PACK_HEADER_SIZE = 32
TRAILER_MAGIC = 0x54415746   # "FWAT"
ZONE_SIZE = 0x4000           # MB_ASSET_SLOT_SIZE
IMAGE_MAX = 0x1D800          # MB_INT_APP_SIZE
BUILD_ID_LEN = 16
NT_GNU_BUILD_ID = 3


def read_sections(elf: bytes) -> dict[str, bytes]:
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise ValueError("not a little-endian ELF32 file")
    e_shoff, = struct.unpack_from("<I", elf, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
    headers = []
    for i in range(e_shnum):
        sh = struct.unpack_from("<IIIIIIIIII", elf, e_shoff + i * e_shentsize)
        headers.append(sh)
    strtab = headers[e_shstrndx]
    names = elf[strtab[4]:strtab[4] + strtab[5]]
    sections = {}
    for sh_name, sh_type, _flags, _addr, sh_offset, sh_size, *_ in headers:
        name = names[sh_name:names.index(b"\0", sh_name)].decode()
        if sh_type == 8:         # SHT_NOBITS
            continue
        sections[name] = elf[sh_offset:sh_offset + sh_size]
    return sections


def build_id(note: bytes) -> bytes:
    if len(note) < 12:
        raise ValueError(".note.gnu.build-id is missing (link with --build-id=md5)")
    namesz, descsz, note_type = struct.unpack_from("<III", note, 0)
    if namesz != 4 or note[12:16] != b"GNU\0" or note_type != NT_GNU_BUILD_ID:
        raise ValueError("unexpected .note.gnu.build-id layout")
    if descsz != BUILD_ID_LEN:
        raise ValueError(f"build-id is {descsz} bytes, firmware expects {BUILD_ID_LEN} (MD5)")
    return note[16:16 + descsz]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--pack", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    sections = read_sections(args.elf.read_bytes())
    payload = sections.get(".fw_assets", b"")
    if not payload:
        raise ValueError("the ELF has no .fw_assets payload")
    if PACK_HEADER_SIZE + len(payload) > ZONE_SIZE:
        raise ValueError(f"asset pack is {PACK_HEADER_SIZE + len(payload)} bytes, zone is {ZONE_SIZE}")

    image = args.image.read_bytes()
    if not image or len(image) > IMAGE_MAX:
        raise ValueError(f"firmware image is {len(image)} bytes, limit is {IMAGE_MAX}")

    header = struct.pack(
        "<IHHII16s",
        PACK_MAGIC,
        PACK_VERSION,
        PACK_HEADER_SIZE,
        len(payload),
        zlib.crc32(payload) & 0xFFFFFFFF,
        build_id(sections.get(".note.gnu.build-id", b"")),
    )
    pack = header + payload
    trailer = struct.pack(
        "<IIII",
        TRAILER_MAGIC,
        len(image),
        len(pack),
        zlib.crc32(image + pack) & 0xFFFFFFFF,
    )

    args.pack.write_bytes(pack)
    args.out.write_bytes(image + pack + trailer)
    print(f"Firmware image {len(image)} B, asset pack {len(pack)} B "
          f"({ZONE_SIZE - len(pack)} B free in the zone)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"pack_fw_assets: {error}", file=sys.stderr)
        sys.exit(1)
