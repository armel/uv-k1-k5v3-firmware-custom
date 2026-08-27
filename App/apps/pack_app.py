#!/usr/bin/env python3
# Pack an overlay-app raw binary into a .app blob: 64-byte header + code.
# Header layout mirrors app_overlay.h : app_header_t (little-endian, packed).
# The format/version constants (magic, header + ABI version, overlay budget) are
# read straight from the C headers the firmware itself compiles, so there is a
# SINGLE source of truth - the packer can never silently drift from the loader.
# CRC-32 is zlib/PKZIP (init 0xFFFFFFFF, poly 0xEDB88320, final XOR) to match
# the firmware's mb_ext_image_crc32 / mb_crc32_bytes.
#
#   ./pack_app.py breakout/breakout.bin breakout/breakout.app --name Breakout --ver 1.0
import argparse, os, re, struct, zlib, sys

HERE = os.path.dirname(os.path.abspath(__file__))

def cdefine(header: str, name: str) -> int:
    """Value of a `#define <name> <int-literal>` in a sibling C header.
    Accepts a decimal or 0x-hex literal with optional u/U/l/L suffixes. Keeps the
    blob format tied to the firmware's own headers (single source of truth)."""
    pat = re.compile(r"^\s*#define\s+" + re.escape(name) +
                     r"\s+(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\b")
    with open(os.path.join(HERE, header)) as f:
        for line in f:
            m = pat.match(line)
            if m:
                return int(m.group(1), 0)
    sys.exit(f"{header}: #define {name} not found")

# Single source of truth: the C headers the firmware also compiles.
MAGIC          = cdefine("app_overlay.h", "APP_MAGIC").to_bytes(4, "little")  # 0x31504146 -> b"FAP1"
HDR_VERSION    = cdefine("app_overlay.h", "APP_HDR_VERSION")
ABI_VERSION    = cdefine("app_api.h",     "APP_ABI_VERSION")
OVERLAY_MAX    = cdefine("app_overlay.h", "APP_OVERLAY_MAX")   # 4 KiB overlay budget
FLAG_COMMITTED = 1                                             # APP_FLAG_COMMITTED, bit 0

def field(s: str, n: int) -> bytes:
    b = s.encode("ascii", "strict")[: n - 1]
    return b + b"\x00" * (n - len(b))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("outfile")
    ap.add_argument("--name", default="app")
    ap.add_argument("--ver", default="1.0")
    ap.add_argument("--entry", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--vma", type=lambda x: int(x, 0), required=True,
                    help="RAM VMA the app was linked at (must match the firmware overlay)")
    a = ap.parse_args()

    code = open(a.infile, "rb").read()
    if len(code) == 0:
        sys.exit("empty input")
    if len(code) > OVERLAY_MAX:
        sys.exit(f"code {len(code)} B exceeds overlay budget {OVERLAY_MAX} B")

    crc = zlib.crc32(code) & 0xFFFFFFFF
    header = struct.pack(
        "<4sHHIIHH16s16sI8s",
        MAGIC, HDR_VERSION, ABI_VERSION,
        len(code), crc, a.entry, FLAG_COMMITTED,
        field(a.name, 16), field(a.ver, 16), a.vma, b"\x00" * 8,
    )
    assert len(header) == 64, len(header)

    with open(a.outfile, "wb") as f:
        f.write(header)
        f.write(code)

    print(f"{a.outfile}: name={a.name!r} ver={a.ver!r} vma=0x{a.vma:08x} "
          f"code={len(code)} B crc32=0x{crc:08x} -> blob {64 + len(code)} B")

if __name__ == "__main__":
    main()
