#!/usr/bin/env python3
"""Generate synthetic Vaisala RS41 Flipper Zero RAW bench transmissions."""
import argparse
import math
import os
import sys

PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
BIT_RATE = 4800
FRAME_LEN = 320
LEAD_US = 100000
TAIL_US = 10000
TRAIN_BITS = 320
PER_LINE = 512
MASK = bytes.fromhex(
    "96 83 3E 51 B1 49 08 98 32 05 59 0E F9 44 C6 26 "
    "21 60 C2 EA 79 5D 6D A1 54 69 47 0C DC E8 5C F1 "
    "F7 76 82 7F 07 99 A2 2C 93 7C 30 63 F5 10 2E 61 "
    "D0 BC B4 B6 06 AA F4 23 78 6E 3B AE BF 7B 4C C1"
)
CLEAR_SYNC = bytes.fromhex("86 35 F4 40 93 DF 1A 60")


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def finish_block(frame, offset):
    length = frame[offset + 1]
    end = offset + 2 + length
    frame[end:end + 2] = crc16(frame[offset + 2:end]).to_bytes(2, "little")


def ecef(latitude, longitude, altitude):
    lat = math.radians(latitude)
    lon = math.radians(longitude)
    a = 6378137.0
    e2 = 6.69437999014e-3
    n = a / math.sqrt(1.0 - e2 * math.sin(lat) ** 2)
    x = (n + altitude) * math.cos(lat) * math.cos(lon)
    y = (n + altitude) * math.cos(lat) * math.sin(lon)
    z = (n * (1.0 - e2) + altitude) * math.sin(lat)
    return tuple(round(value * 100) for value in (x, y, z))


def ecef_velocity(latitude, longitude, east, north, up):
    """Local east/north/up speeds (m/s) as ECEF components in cm/s."""
    lat = math.radians(latitude)
    lon = math.radians(longitude)
    sl, cl, so, co = math.sin(lat), math.cos(lat), math.sin(lon), math.cos(lon)
    v = (-so * east - sl * co * north + cl * co * up,
         co * east - sl * so * north + cl * so * up,
         cl * north + sl * up)
    return tuple(round(value * 100) for value in v)


def base_frame(serial, battery):
    """Sync, frame type and the STATUS block shared by both layouts."""
    frame = bytearray(FRAME_LEN)
    frame[:8] = CLEAR_SYNC
    frame[0x38] = 0x0F

    status = 0x39
    frame[status] = 0x79
    frame[status + 1] = 40
    frame[status + 2:status + 4] = (1234).to_bytes(2, "little")
    frame[status + 4:status + 12] = serial
    frame[status + 12] = battery                # 0x45: battery, 0.1 V
    finish_block(frame, status)
    return frame


def put_ecef(frame, offset, position, velocity):
    """ECEF position (cm) and velocity (cm/s), the start of both position blocks."""
    for index, value in enumerate(ecef(*position)):
        frame[offset + index * 4:offset + 4 + index * 4] = value.to_bytes(4, "little", signed=True)
    for index, value in enumerate(ecef_velocity(position[0], position[1], *velocity)):
        frame[offset + 12 + index * 2:offset + 14 + index * 2] = value.to_bytes(2, "little", signed=True)


def test_frame():
    """Classic layout: GPS position block 0x7B at 0x112."""
    frame = base_frame(b"S1234567", 29)

    gps = 0x112
    frame[gps] = 0x7B
    frame[gps + 1] = 21
    # Climbing at 5.2 m/s with the wind carrying it east-south-east.
    put_ecef(frame, gps + 2, (48.8, 2.3, 1200), (8.0, -3.0, 5.2))
    frame[gps + 20] = 8
    finish_block(frame, gps)
    return frame


GNSS = 0xA1


def gnss_frame():
    """Multi-GNSS firmware layout (rs41mod: fw 0x50dd): no classic GPS
    blocks; PTU 0x7A at 0x65, 0x96 (likely an authentication tag) at 0x93,
    position/date/time 0x82 at 0xA1, satellites 0x83 at 0xCB, padding 0x76
    at 0xF8 up to the end of the 320 bytes."""
    frame = base_frame(b"X7654321", 31)

    def block(offset, ident, length, payload=b""):
        frame[offset] = ident
        frame[offset + 1] = length
        frame[offset + 2:offset + 2 + len(payload)] = payload
        finish_block(frame, offset)

    block(0x65, 0x7A, 42)
    block(0x93, 0x96, 10, bytes(range(0x51, 0x5B)))
    # Falling at 12.3 m/s after the burst, the wind carrying it north-west.
    put_ecef(frame, GNSS + 2, (45.2, 5.7, 2500), (-6.0, 4.0, -12.3))
    frame[GNSS + 20:GNSS + 22] = (2026).to_bytes(2, "little")
    frame[GNSS + 22:GNSS + 28] = bytes((10, 19, 12, 0, 0, 0))
    block(GNSS, 0x82, 38, frame[GNSS + 2:GNSS + 40])
    block(0xCB, 0x83, 41)
    block(0xF8, 0x76, FRAME_LEN - 0xF8 - 4)
    return frame


def wire_bits(frame):
    wire = bytes(value ^ MASK[index & 63] for index, value in enumerate(frame))
    return [(value >> bit) & 1 for value in wire for bit in range(8)]


def raw_durations(bits):
    training = [index & 1 for index in range(TRAIN_BITS)]
    levels = [0] + training + list(bits) + [0]
    bit_us = 1e6 / BIT_RATE
    times = [0.0, float(LEAD_US)]
    for index in range(len(training) + len(bits)):
        times.append(LEAD_US + (index + 1) * bit_us)
    times.append(times[-1] + TAIL_US)

    runs = []
    start = 0
    current = levels[0]
    for index in range(1, len(levels) + 1):
        if index == len(levels) or levels[index] != current:
            duration = round(times[index]) - round(times[start])
            runs.append(-duration if current else duration)
            if index < len(levels):
                start = index
                current = levels[index]
    return runs


def write_sub(path, bits, frequency):
    runs = raw_durations(bits)
    lines = [
        "Filetype: Flipper SubGhz RAW File",
        "Version: 1",
        "Frequency: %d" % frequency,
        "Preset: %s" % PRESET,
        "Protocol: RAW",
    ]
    for index in range(0, len(runs), PER_LINE):
        lines.append("RAW_Data: " + " ".join(str(value) for value in runs[index:index + PER_LINE]))
    with open(path, "w", encoding="ascii") as output:
        output.write("\n".join(lines) + "\n")
    return runs


def corrupt(frame, offset):
    bad = bytearray(frame)
    bad[offset] ^= 1
    return bad


def test_cases():
    """Classic layout (rs41_*) and multi-GNSS layout (rs41_gnss_*)."""
    valid = test_frame()
    gnss = gnss_frame()
    bits = wire_bits(valid)
    gnss_bits = wire_bits(gnss)
    return {
        "valid": (bits, "S1234567, 48.8000N 2.3000E, ALT about 1200m VS+5.2, "
                           "BAT 2.9V, HS 9m/s HDG 110 or 111"),
        "inverted": ([1 ^ bit for bit in bits], "same frame, inverted RF polarity"),
        "badcrc": (wire_bits(corrupt(valid, 0x39 + 4)), "rejected: FRAME must not increment"),
        "badpos": (wire_bits(corrupt(valid, 0x112 + 4)),
                   "S1234567, BAT 2.9V, FRAME increments, no position"),
        "gnss_valid": (gnss_bits, "X7654321, 45.2000N 5.7000E, ALT about 2500m VS-12.3, "
                                  "BAT 3.1V, HS 7m/s HDG 303 or 304"),
        "gnss_inverted": ([1 ^ bit for bit in gnss_bits], "same frame, inverted RF polarity"),
        "gnss_badpos": (wire_bits(corrupt(gnss, GNSS + 4)),
                        "X7654321, BAT 3.1V, FRAME increments, no position"),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out", nargs="?", default="flipper")
    parser.add_argument("--freq", type=int, default=433650000)
    args = parser.parse_args()
    if not 433050000 <= args.freq <= 434790000:
        sys.exit("frequency must stay inside the 433 MHz SRD bench band")
    os.makedirs(args.out, exist_ok=True)
    for name, (bits, expected) in test_cases().items():
        path = os.path.join(args.out, "rs41_%s.sub" % name)
        runs = write_sub(path, bits, args.freq)
        duration_ms = sum(abs(value) for value in runs) / 1000
        print("%-30s %4.0f ms %4d runs  %s" %
              (path, duration_ms, len(runs), expected))


if __name__ == "__main__":
    main()
