#!/usr/bin/env python3
"""Generate synthetic Sonde M10 Flipper Zero RAW bench transmissions."""
import argparse
import os
import sys

PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
CHIP_RATE = 9615
RAW_HEADER = 0x99994C99
FRAME_LEN = 101
LEAD_US = 100000
TAIL_US = 10000
TRAIN_CHIPS = 64
PER_LINE = 512


def check_m10(msg):
    checksum = 0
    for byte in msg:
        byte = ((byte >> 1) | (byte << 7)) & 0xFF
        byte ^= byte >> 2
        t6 = (checksum ^ (checksum >> 2) ^ (checksum >> 4)) & 1
        t7 = ((checksum >> 1) ^ (checksum >> 3) ^ (checksum >> 5)) & 1
        t = (checksum & 0x3F) | (t6 << 6) | (t7 << 7)
        s = (checksum >> 7) & 0xFF
        s ^= s >> 2
        checksum = ((checksum & 0xFF) << 8) | ((byte ^ t ^ s) & 0xFF)
    return checksum


def put_be(frame, offset, value, size):
    value &= (1 << (size * 8)) - 1
    frame[offset:offset + size] = value.to_bytes(size, "big")


def test_frame():
    frame = bytearray(FRAME_LEN)
    frame[0] = 0x64
    frame[1] = 0x9F
    frame[2] = 0x20
    # 8 m/s east, 3 m/s south, 5.2 m/s up, in 1/200 m/s (m10mod)
    put_be(frame, 0x04, 1600, 2)
    put_be(frame, 0x06, -600 & 0xFFFF, 2)
    put_be(frame, 0x08, 1040, 2)
    put_be(frame, 0x0E, round(48.8 * (1 << 32) / 360), 4)
    put_be(frame, 0x12, round(2.3 * (1 << 32) / 360), 4)
    put_be(frame, 0x16, 1200000, 4)
    frame[0x1D] = 0
    frame[0x3E] = 0
    frame[0x3F:0x41] = (0xA599).to_bytes(2, "little")  # About 20 C.
    # Humidity counts, 55 %RH reference then sensor: ratio 0.9855, 45 %RH.
    frame[0x32:0x35] = (1000000).to_bytes(3, "little")
    frame[0x35:0x38] = (985500).to_bytes(3, "little")
    frame[0x5D] = 0x0B
    frame[0x5F] = 0xA5
    serial = (1 << 13) | 1234
    frame[0x60:0x62] = serial.to_bytes(2, "little")
    crc_pos = frame[0] - 1
    checksum = check_m10(frame[:crc_pos])
    frame[crc_pos:crc_pos + 2] = checksum.to_bytes(2, "big")
    return frame


def frame_chips(frame):
    chips = [(RAW_HEADER >> shift) & 1 for shift in range(31, -1, -1)]
    previous = 0
    for bit_pos in range((frame[0] + 1) * 8):
        bit = (frame[bit_pos >> 3] >> (7 - (bit_pos & 7))) & 1
        manchester = previous if bit else 1 ^ previous
        chips.extend((0, 1) if manchester else (1, 0))
        previous = manchester
    return chips


def raw_durations(chips):
    training = [i & 1 for i in range(TRAIN_CHIPS)]
    levels = [0] + training + list(chips) + [0]
    chip_us = 1e6 / CHIP_RATE
    times = [0.0, float(LEAD_US)]
    for i in range(len(training) + len(chips)):
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


def test_cases():
    valid = test_frame()
    bad_crc = bytearray(valid)
    bad_crc[0x0E] ^= 1
    chips = frame_chips(valid)
    return {
        "valid": (chips, "A05-B-11234, 48.8000N 2.3000E, ALT 1200m VS+5.2, T 20.0C RH 45%, "
                           "HS 9m/s HDG 111"),
        "inverted": ([1 ^ chip for chip in chips], "same frame, inverted RF polarity"),
        "badcrc": (frame_chips(bad_crc), "rejected: FRAME must not increment"),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out", nargs="?", default="flipper")
    parser.add_argument("--freq", type=int, default=433650000)
    args = parser.parse_args()
    if not 433050000 <= args.freq <= 434790000:
        sys.exit("frequency must stay inside the 433 MHz SRD bench band")
    os.makedirs(args.out, exist_ok=True)
    for name, (chips, expected) in test_cases().items():
        path = os.path.join(args.out, "m10_%s.sub" % name)
        runs = write_sub(path, chips, args.freq)
        duration_ms = sum(abs(value) for value in runs) / 1000
        print("%-28s %4.0f ms %4d runs  %s" %
              (path, duration_ms, len(runs), expected))


if __name__ == "__main__":
    main()
