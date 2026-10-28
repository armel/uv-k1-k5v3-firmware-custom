#!/usr/bin/env python3
"""Generate synthetic MRZ-N1 / MP3-H1 Flipper Zero RAW bench files."""
import argparse
import math
import os
import struct
import sys

PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
CHIP_RATE = 2400
LEAD_US = 100000
TAIL_US = 20000
TRAIN_CHIPS = 32
GAP_CHIPS = 24
PER_LINE = 512
SN_C = 5667
SN_D = 39155
TEST_LAT_UDEG = 49_755_900
TEST_LON_UDEG = 2_352_200


def crc16(payload):
    crc = 0xFFFF
    for byte in payload:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def put_i16(frame, offset, value):
    struct.pack_into("<h", frame, offset, value)


def put_i32(frame, offset, value):
    struct.pack_into("<i", frame, offset, value)


def finish(frame, crc_len):
    struct.pack_into("<H", frame, 3 + crc_len, crc16(frame[3:3 + crc_len]))
    return frame


def ecef_position(lat_deg, lon_deg, alt_m):
    lat, lon = math.radians(lat_deg), math.radians(lon_deg)
    a = 6378137.0
    e2 = 6.69437999014e-3
    n = a / math.sqrt(1.0 - e2 * math.sin(lat) ** 2)
    x = (n + alt_m) * math.cos(lat) * math.cos(lon)
    y = (n + alt_m) * math.cos(lat) * math.sin(lon)
    z = (n * (1.0 - e2) + alt_m) * math.sin(lat)
    return tuple(round(v * 100.0) for v in (x, y, z))


def ecef_velocity(lat_deg, lon_deg, east, north, up):
    lat, lon = math.radians(lat_deg), math.radians(lon_deg)
    vx = -math.sin(lon) * east - math.sin(lat) * math.cos(lon) * north + math.cos(lat) * math.cos(lon) * up
    vy = math.cos(lon) * east - math.sin(lat) * math.sin(lon) * north + math.cos(lat) * math.sin(lon) * up
    vz = math.cos(lat) * north + math.sin(lat) * up
    return tuple(round(v) for v in (vx, vy, vz))


def ecef_frame(subcount, stamp, serial):
    frame = bytearray(50)
    frame[:3] = b"\xAA\xBF\x35"
    frame[3] = subcount
    frame[4:7] = bytes(stamp)
    lat = TEST_LAT_UDEG / 1e6
    lon = TEST_LON_UDEG / 1e6
    for offset, value in zip((8, 12, 16), ecef_position(lat, lon, 1200.0)):
        put_i32(frame, offset, value)
    for offset, value in zip((20, 22, 24), ecef_velocity(lat, lon, 800, -300, 520)):
        put_i16(frame, offset, value)
    frame[26] = 9
    put_i16(frame, 29, 2410)
    put_i16(frame, 31, 4500)
    frame[43] = subcount
    struct.pack_into("<I", frame, 44, serial)
    return finish(frame, 45)


def latlon_frame(subcount, stamp, serial):
    frame = bytearray(47)
    frame[:3] = b"\xAA\xBF\x35"
    frame[3] = subcount
    frame[4:7] = bytes(stamp)
    put_i32(frame, 7, TEST_LAT_UDEG)
    put_i32(frame, 11, TEST_LON_UDEG)
    put_i32(frame, 15, 120000)
    put_i16(frame, 19, 900)
    struct.pack_into("<H", frame, 21, 11100)
    frame[23] = 9
    put_i16(frame, 26, 2410)
    put_i16(frame, 28, 4500)
    frame[30:32] = b"\xFF\xFF"
    frame[40] = subcount
    struct.pack_into("<I", frame, 41, serial)
    return finish(frame, 42)


def frame_chips(frame):
    bits = bytes([0xAA]) + bytes(frame)
    chips = []
    for byte in bits:
        for shift in range(7, -1, -1):
            chips.extend((1, 0) if (byte >> shift) & 1 else (0, 1))
    return chips


def transmission(frames):
    chips = []
    for frame in frames:
        encoded = frame_chips(frame)
        for _ in range(6):
            chips.extend(i & 1 for i in range(TRAIN_CHIPS))
            chips.extend(encoded)
            chips.extend([0] * GAP_CHIPS)
    return chips


def raw_durations(chips):
    levels = [0] + list(chips) + [0]
    chip_us = 1e6 / CHIP_RATE
    times = [0.0, float(LEAD_US)]
    for i in range(len(chips)):
        times.append(LEAD_US + (i + 1) * chip_us)
    times.append(times[-1] + TAIL_US)
    runs = []
    start = 0
    current = levels[0]
    for i in range(1, len(levels) + 1):
        if i == len(levels) or levels[i] != current:
            duration = round(times[i]) - round(times[start])
            runs.append(-duration if current else duration)
            if i < len(levels):
                start = i
                current = levels[i]
    return runs


def write_sub(path, chips, frequency):
    runs = raw_durations(chips)
    lines = [
        "Filetype: Flipper SubGhz RAW File",
        "Version: 1",
        "Frequency: %d" % frequency,
        "Preset: %s" % PRESET,
        "Protocol: RAW",
    ]
    for i in range(0, len(runs), PER_LINE):
        lines.append("RAW_Data: " + " ".join(str(v) for v in runs[i:i + PER_LINE]))
    with open(path, "w", encoding="ascii") as output:
        output.write("\n".join(lines) + "\n")
    return runs


def cases(kind):
    make = ecef_frame if kind == "ecef" else latlon_frame
    crc_len = 45 if kind == "ecef" else 42
    valid = [make(0x0C, (12, 34, 56), SN_C), make(0x0D, (12, 34, 57), SN_D)]
    assert all(crc16(frame[3:3 + crc_len]) == int.from_bytes(frame[3 + crc_len:5 + crc_len], "little")
               for frame in valid)
    bad = [bytearray(frame) for frame in valid]
    for frame in bad:
        frame[7] ^= 1
    assert all(crc16(frame[3:3 + crc_len]) != int.from_bytes(frame[3 + crc_len:5 + crc_len], "little")
               for frame in bad)
    chips = transmission(valid)
    return {
        "valid": (chips, "FRAME 2, MRZC7C0D3, 49.7559N 2.3522E, ALT 1200m"),
        "inverted": ([1 ^ chip for chip in chips], "same frames, inverted RF polarity"),
        "badcrc": (transmission(bad), "rejected: FRAME must not increment"),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out", nargs="?", default="flipper")
    parser.add_argument("--freq", type=int, default=433650000)
    args = parser.parse_args()
    if not 433050000 <= args.freq <= 434790000:
        sys.exit("frequency must stay inside the 433 MHz SRD bench band")
    os.makedirs(args.out, exist_ok=True)
    combined = (SN_C * 100000 + SN_D) & 0xFFFFFF
    assert "%06X" % combined == "C7C0D3"
    for kind in ("ecef", "latlon"):
        for name, (chips, expected) in cases(kind).items():
            path = os.path.join(args.out, "mrz_%s_%s.sub" % (kind, name))
            runs = write_sub(path, chips, args.freq)
            duration_ms = sum(abs(value) for value in runs) / 1000
            print("%-34s %5.0f ms %5d runs  %s" %
                  (path, duration_ms, len(runs), expected))


if __name__ == "__main__":
    main()
