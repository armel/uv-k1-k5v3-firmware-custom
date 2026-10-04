#!/usr/bin/env python3
"""The independent app must never produce API1 or capability-free metadata."""
import importlib.util
from pathlib import Path
import struct
import zlib

source = Path(__file__).resolve().parents[2] / "App/apps/aprstx-resident/pack_resident.py"
spec = importlib.util.spec_from_file_location("pack_resident", source)
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)
payload = bytes(range(256))
blob = packer.pack(payload, 0x20000280)
magic, version, abi, api, size, crc, entry, flags, name, appver, vma, caps, reserved = struct.unpack(
    "<4sHBBIIHH16s16sII4s", blob[:64])
assert (magic, version, abi, api) == (b"FAP1", 1, 1, 2)
assert size == len(payload) and blob[64:] == payload
assert crc == zlib.crc32(payload) & 0xFFFFFFFF
assert entry == 0 and flags == 1 and caps == 8 and vma == 0x20000280
assert name.rstrip(b"\0") == b"APRSTX-R" and reserved == bytes(4)
for bad in (b"", bytes(4097)):
    try:
        packer.pack(bad, vma)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid payload accepted")
print("Resident APRS API2/cap8 metadata and CRC tests passed")
