#!/usr/bin/env python3
"""Host checks for APRS Edit's proxy descriptor and APRS profile block."""
import os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "..", "gen_assets.py")
BUILD = os.path.join(HERE, "..", "build.sh")


def assets():
    with tempfile.TemporaryDirectory() as directory:
        binary = os.path.join(directory, "assets.bin")
        header = os.path.join(directory, "assets.h")
        subprocess.run([sys.executable, GEN, binary, header], check=True)
        blob = open(binary, "rb").read()
        defs = {name: int(value) for name, value in
                re.findall(r"#define (\w+) (\d+)u", open(header).read())}
    return blob, defs


def profile(text, pos=None, hemi=0, ssid=7, path=1, symbol=5, callsign=""):
    raw = text.encode("ascii")
    if len(raw) > 89 or any(c < 32 or c > 126 for c in raw):
        raise ValueError("invalid APRS comment")
    call = callsign.encode("ascii")
    if len(call) > 6 or any(not (48 <= c <= 57 or 65 <= c <= 90) for c in call):
        raise ValueError("invalid APRS callsign")
    pos = pos or [4, 8, 5, 0, 9, 0, 0, 0, 2, 1, 6, 2, 5]
    return struct.pack("<IBBBB", 0x37435041, len(raw), path << 4 | ssid,
                       symbol, hemi) + bytes(pos) + raw.ljust(89, b" ") + \
        bytes([len(call)]) + call.ljust(6, b" ")


def main():
    blob, defs = assets()
    preset_names = [b"CUSTOM", b"PORTABLE", b"MOBILE", b"QRP", b"TEST"]
    menu_names = [b"Call", b"SSID", b"Lat", b"Lon", b"Path", b"Symbol", b"Comm."]
    path_names = [b"DIRECT", b"WIDE1-1", b"WIDE1-1,WIDE2-1", b"ARISS"]
    presets = [
        b"Portable, UV-K5/K1 F4HWN firmware (github.com/armel)",
        b"Mobile, UV-K5/K1 F4HWN firmware (github.com/armel)",
        b"QRP, UV-K5/K1 F4HWN firmware (github.com/armel)",
        b"APRS test, UV-K5/K1 F4HWN firmware (github.com/armel)",
    ]
    descriptor = blob[:defs["CFG_PROXY_LEN"]]
    magic, target, offset, length, reserved, target_magic = struct.unpack(
        "<I16sHBBI", descriptor)
    checks = [
        defs["CFG_PROXY"] == 0,
        len(descriptor) == 28,
        magic == 0x31505843,
        target.rstrip(b"\0") == b"APRS TX",
        offset == 0,
        length == 117,
        reserved == 0,
        target_magic == 0x37435041,
        defs["PROFILE_LEN"] == 117,
        defs["SYM_COUNT"] == 48,
        defs["SYM_CODES"] + defs["SYM_CODES_LEN"] == defs["UI_SIZE"],
        defs["KEYMAP"] == defs["UI_SIZE"],
        defs["KEYMAP_LEN"] == 50,
        defs["SYM_BITMAPS"] == defs["KEYMAP"] + defs["KEYMAP_LEN"],
        defs["SYM_BITMAPS_LEN"] == 48 * 60,
        defs["PRESET_COUNT"] == len(preset_names),
        defs["TEMPLATE_COUNT"] == len(presets),
        all(blob[defs["T_MENU"] + i * defs["T_MENU_STRIDE"]:
                 defs["T_MENU"] + i * defs["T_MENU_STRIDE"] + len(name)] == name
            for i, name in enumerate(menu_names)),
        defs["T_PATHS_LEN"] == len(path_names) * defs["T_PATHS_STRIDE"],
        all(blob[defs["T_PATHS"] + i * defs["T_PATHS_STRIDE"]:
                 defs["T_PATHS"] + i * defs["T_PATHS_STRIDE"] + len(name)] == name
            for i, name in enumerate(path_names)),
        all(blob[defs["T_PRESET_NAMES"] + i * defs["T_PRESET_NAMES_STRIDE"]:
                 defs["T_PRESET_NAMES"] + i * defs["T_PRESET_NAMES_STRIDE"] + len(name)] == name
            for i, name in enumerate(preset_names)),
        defs["P_PRESET_LENGTHS"] == defs["SYM_BITMAPS"] + defs["SYM_BITMAPS_LEN"],
        defs["P_PRESETS"] == defs["P_PRESET_LENGTHS"] + defs["P_PRESET_LENGTHS_LEN"],
        all(blob[defs["P_PRESETS"] + i * 89:defs["P_PRESETS"] + i * 89 + len(message)] == message
            for i, message in enumerate(presets)),
        blob[defs["P_PRESET_LENGTHS"]:defs["P_PRESET_LENGTHS"] + len(presets)] ==
            bytes(len(message) for message in presets),
        all(len(message) <= defs["COMMENT_LIMIT"] for message in presets),
        defs["COMMENT_LIMIT"] == 64,
        len(blob) < 3840,
        "--exit-main" not in open(BUILD).read(),
        len(profile("Portable, UV-K5/K1 F4HWN firmware (github.com/armel)")) == length,
        profile("X" * 89)[4] == 89,
        profile("X")[5:8] == bytes([0x17, 5, 0]),
        profile("X", path=3)[5] == 0x37,
        profile("X", callsign="F4HWN")[110:117] == b"\x05F4HWN ",
        profile("X")[110:117] == b"\x00      ",
    ]
    try:
        profile("X" * 90)
        checks.append(False)
    except ValueError:
        checks.append(True)
    for callsign in ("F4HWN73", "f4hwn", "F4/HWN"):
        try:
            profile("X", callsign=callsign)
            checks.append(False)
        except ValueError:
            checks.append(True)
    ok = all(checks)
    print("APRS Edit model: all checks passed" if ok else "APRS Edit model: failure")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
