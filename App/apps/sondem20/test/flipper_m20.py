#!/usr/bin/env python3
"""Generate Flipper Zero Sub-GHz RAW files for Sonde M20 bench tests.

The files transmit a synthetic CRC-valid M20 frame on 433.650 MHz using the
Flipper's asynchronous 2-FSK preset. M20 differential Manchester chips are
sent directly as the two RF frequencies at 9600 chips/s.

  flipper_m20.py [out_dir] [--freq 433650000]
    -> m20_valid.sub, m20_inverted.sub, m20_badcrc.sub
"""
import argparse
import os
import sys

PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
CHIP_RATE = 9600
RAW_HEADER = 0x99994C99
FRAME_LEN = 70
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
    frame[0] = 0x45
    frame[1] = 0x20
    frame[0x04:0x06] = (0x0509).to_bytes(2, "little")
    put_be(frame, 0x08, 120000, 3)
    # 8 m/s east, 3 m/s south, 5.2 m/s up, in cm/s (mXXmod)
    put_be(frame, 0x0B, 800, 2)
    put_be(frame, 0x0D, -300 & 0xFFFF, 2)
    put_be(frame, 0x18, 520, 2)
    serial = (1234 << 10) | 29
    frame[0x12:0x15] = serial.to_bytes(3, "little")
    frame[0x15] = 7
    # Humidity: humval 30000, rh_cal 32389, RH sensor NTC 447 (20 C): 45 %RH
    frame[0x02:0x04] = (30000).to_bytes(2, "little")
    frame[0x2F:0x31] = (32389).to_bytes(2, "little")
    frame[0x06:0x08] = (447).to_bytes(2, "little")
    put_be(frame, 0x1C, 48800000, 4)
    put_be(frame, 0x20, 2300000, 4)
    frame[0x43] = 7
    crc_pos = frame[0] - 1
    crc = check_m10(frame[:crc_pos])
    frame[crc_pos:crc_pos + 2] = crc.to_bytes(2, "big")
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
    """Return signed runs; positive selects the higher RF frequency."""
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
    crc_pos = valid[0] - 1
    assert check_m10(valid[:crc_pos]) == int.from_bytes(valid[crc_pos:crc_pos + 2], "big")
    bad_crc = bytearray(valid)
    bad_crc[0x1C] ^= 1
    assert check_m10(bad_crc[:crc_pos]) != int.from_bytes(bad_crc[crc_pos:crc_pos + 2], "big")
    chips = frame_chips(valid)
    return {
        "valid": (chips, "206-1-01234, 48.8000N 2.3000E, ALT 1200m VS+5.2, T 24.1C RH 45%, "
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
        path = os.path.join(args.out, "m20_%s.sub" % name)
        runs = write_sub(path, chips, args.freq)
        duration_ms = sum(abs(value) for value in runs) / 1000
        print("%-28s %4.0f ms %4d runs  %s" %
              (path, duration_ms, len(runs), expected))


if __name__ == "__main__":
    main()
