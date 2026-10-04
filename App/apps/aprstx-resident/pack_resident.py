#!/usr/bin/env python3
"""Package API2/cap8 on both baseline SDK and the extended resident SDK.

The baseline packer cannot name a future capability or API level. Import its
format constants and ASCII field encoder, and emit the same 64-byte format.
The API2/cap8 requirement is mandatory and is never downgraded for stock.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import zlib


def pack(payload, vma):
    sdk = Path(__file__).resolve().parent.parent / "pack_app.py"
    spec = importlib.util.spec_from_file_location("stock_packer", sdk)
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    if not payload or len(payload) > base.OVERLAY_MAX:
        raise ValueError("overlay payload must contain 1..4096 bytes")
    header = struct.pack(
        "<4sHBBIIHH16s16sII4s", base.MAGIC, base.HDR_VERSION,
        base.ABI_MAJOR, 2, len(payload), zlib.crc32(payload) & 0xFFFFFFFF,
        0, base.FLAG_COMMITTED, base.field("APRSTX-R", 16),
        base.field("0.1.0", 16), vma, 0x00000008, bytes(4))
    assert len(header) == 64
    return header + payload


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("infile", type=Path)
    parser.add_argument("outfile", type=Path)
    parser.add_argument("--vma", type=lambda value: int(value, 0), required=True)
    args = parser.parse_args()
    args.outfile.write_bytes(pack(args.infile.read_bytes(), args.vma))
