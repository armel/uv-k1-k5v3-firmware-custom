#!/usr/bin/env python3
# Pack an overlay-app raw binary into a .app blob: 64-byte header + code
# [+ read-only assets]. The host installer writes the code at slot+0x1000 and
# the assets (the bytes after code_size) at slot+APP_ASSET_OFFSET.
# Header layout mirrors app_overlay.h : app_header_t (little-endian, packed).
# The format/version constants (magic, header + ABI/API levels, overlay budget) are
# read straight from the C headers the firmware itself compiles, so there is a
# SINGLE source of truth - the packer can never silently drift from the loader.
# CRC-32 is zlib/PKZIP (init 0xFFFFFFFF, poly 0xEDB88320, final XOR) to match
# the firmware's mb_ext_image_crc32 / mb_crc32_bytes.
#
#   ./pack_app.py breakout/breakout.bin breakout/breakout.app --name Breakout \
#       --ver 1.0 --vma 0x20000280 --api-min 1
#   ./pack_app.py game.bin Game.app --name Game --vma 0x20000280 --api-min 2 \
#       --assets levels.bin      # served to the app by api->asset_read()
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
ABI_MAJOR      = cdefine("app_api.h",     "APP_ABI_MAJOR")
API_LEVEL      = cdefine("app_api.h",     "APP_API_LEVEL")
OVERLAY_MAX    = cdefine("app_overlay.h", "APP_OVERLAY_MAX")   # 4 KiB overlay budget
ASSET_MAX      = cdefine("app_overlay.h", "APP_ASSET_MAX")     # header-sector asset area
API_ASSETS     = cdefine("app_api.h",     "APP_API_ASSETS")    # first level serving assets
FLAG_COMMITTED = cdefine("app_overlay.h", "APP_FLAG_COMMITTED")
FLAG_SCREEN_SAVER = cdefine("app_overlay.h", "APP_FLAG_SCREEN_SAVER")
FLAG_SHORTCUT_SHIFT = cdefine("app_overlay.h", "APP_FLAG_SHORTCUT_SHIFT")
SHORTCUTS = {
    "none": 0,
    "fm": cdefine("app_overlay.h", "APP_SHORTCUT_FM"),
    "foxhunt": cdefine("app_overlay.h", "APP_SHORTCUT_FOXHUNT"),
    "beacon": cdefine("app_overlay.h", "APP_SHORTCUT_BEACON"),
    "beam": cdefine("app_overlay.h", "APP_SHORTCUT_BEAM"),
}
CAPABILITIES = {
    "fm": cdefine("app_overlay.h", "APP_CAP_FM"),
    "beam": cdefine("app_overlay.h", "APP_CAP_BEAM"),
}

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
    ap.add_argument("--api-min", type=int, required=True,
                    help="minimum append-only firmware API level required by this app")
    ap.add_argument("--vma", type=lambda x: int(x, 0), required=True,
                    help="RAM VMA the app was linked at (must match the firmware overlay)")
    ap.add_argument("--screensaver", action="store_true",
                    help="allow the resident BLTime screen saver while this app is idle")
    ap.add_argument("--shortcut", choices=SHORTCUTS, default="none",
                    help="resident quick action advertised by this app")
    ap.add_argument("--require", action="append", choices=CAPABILITIES, default=[],
                    help="resident capability required by this app (repeatable)")
    ap.add_argument("--assets",
                    help=f"read-only asset file served by api->asset_read (<= {ASSET_MAX} B)")
    a = ap.parse_args()

    if not 1 <= a.api_min <= API_LEVEL:
        sys.exit(f"--api-min must be between 1 and current API level {API_LEVEL}")

    code = open(a.infile, "rb").read()
    if len(code) == 0:
        sys.exit("empty input")
    if len(code) > OVERLAY_MAX:
        sys.exit(f"code {len(code)} B exceeds overlay budget {OVERLAY_MAX} B")

    assets = open(a.assets, "rb").read() if a.assets else b""
    if len(assets) > ASSET_MAX:
        sys.exit(f"assets {len(assets)} B exceed the {ASSET_MAX} B asset area")
    if assets and a.api_min < API_ASSETS:
        sys.exit(f"--assets needs --api-min {API_ASSETS} or higher (asset_read)")
    asset_crc = (zlib.crc32(assets) & 0xFFFF) if assets else 0

    crc = zlib.crc32(code) & 0xFFFFFFFF
    flags = (FLAG_COMMITTED |
             (FLAG_SCREEN_SAVER if a.screensaver else 0) |
             (SHORTCUTS[a.shortcut] << FLAG_SHORTCUT_SHIFT))
    required_caps = 0
    for capability in a.require:
        required_caps |= CAPABILITIES[capability]
    header = struct.pack(
        "<4sHBBIIHH16s16sIIHH",
        MAGIC, HDR_VERSION, ABI_MAJOR, a.api_min,
        len(code), crc, a.entry, flags,
        field(a.name, 16), field(a.ver, 16), a.vma,
        required_caps, len(assets), asset_crc,
    )
    assert len(header) == 64, len(header)

    with open(a.outfile, "wb") as f:
        f.write(header)
        f.write(code)
        f.write(assets)

    print(f"{a.outfile}: name={a.name!r} ver={a.ver!r} "
          f"abi={ABI_MAJOR} api>={a.api_min} caps=0x{required_caps:08x} "
          f"vma=0x{a.vma:08x} "
          f"code={len(code)} B crc32=0x{crc:08x} assets={len(assets)} B "
          f"-> blob {64 + len(code) + len(assets)} B")

if __name__ == "__main__":
    main()
