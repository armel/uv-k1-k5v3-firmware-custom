#!/usr/bin/env python3
"""Generate Flipper Zero Sub-GHz RAW files for ACARS RX bench tests.

The stock Flipper cannot transmit in the 118-137 MHz aeronautical band. These
files instead use 433.650 MHz OOK: the carrier is keyed with the sign of the
ACARS 1200/2400 Hz MSK waveform. An AM receiver recovers a square-wave version
of the audio, whose fundamental is handled by the ACARS RX matched filter.

Run:
  flipper_acars.py [out_dir] [--freq 433650000]

Copy the generated .sub files to subghz/ on the Flipper, tune the radio to the
same frequency in AM, start ACARS RX, and send one file at a time.
"""

from __future__ import annotations

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model_rx import (
    ETB,
    Demodulator,
    crc_suffix,
    crc_update,
    make_frame,
    odd_parity,
)

PRESET = "FuriHalSubGhzPresetOok270Async"
DEFAULT_FREQ = 433_650_000
BAUD = 2400
CARRIER = 1800.0
LEAD_US = 100_000
TAIL_US = 20_000
PER_LINE = 512
TEST_PREKEY_BYTES = 32


def bytes_to_bits(data: bytes):
    for byte in data:
        for bit in range(8):
            yield (byte >> bit) & 1


def wire_bits(frame: bytes) -> list[int]:
    wire = bytes((0xFF,) * TEST_PREKEY_BYTES) + bytes((0xAB, 0x2A)) + frame
    return list(bytes_to_bits(wire))


def coefficients(bits: list[int]) -> list[float]:
    """Staggered I/Q signs expected by the ACARS RX MSK detector."""
    return [
        (1.0 if bit else -1.0) * (-1.0 if index & 2 else 1.0)
        for index, bit in enumerate(bits)
    ]


def msk_value(coeff: list[float], time_us: float) -> float:
    """Continuous-envelope MSK waveform at a time relative to burst start."""
    position = time_us * BAUD / 1_000_000.0
    symbol = int(position)
    i_value = q_value = 0.0
    for pulse in (symbol - 1, symbol):
        if not 0 <= pulse < len(coeff):
            continue
        within = position - pulse
        shape = math.sin(math.pi * within / 2.0)
        if pulse & 1:
            q_value += coeff[pulse] * shape
        else:
            i_value += coeff[pulse] * shape
    phase = 2.0 * math.pi * CARRIER * time_us / 1_000_000.0
    return i_value * math.cos(phase) - q_value * math.sin(phase)


def sub_durations(frame: bytes) -> list[int]:
    """Convert an ACARS frame to signed OOK durations in microseconds."""
    coeff = coefficients(wire_bits(frame))
    burst_us = math.ceil(len(coeff) * 1_000_000 / BAUD + 2_000_000 / BAUD)
    edges: list[int] = []
    high = True

    first_high = msk_value(coeff, 0.5) >= 0.0
    if first_high != high:
        edges.append(LEAD_US)
        high = first_high

    for tick in range(1, burst_us + 1):
        new_high = msk_value(coeff, tick + 0.5) >= 0.0
        if new_high != high:
            edges.append(LEAD_US + tick)
            high = new_high

    end = LEAD_US + burst_us
    if not high:
        if edges and edges[-1] == end:
            edges.pop()
        else:
            edges.append(end)
    end += TAIL_US

    points = [0] + edges + [end]
    durations: list[int] = []
    level = True
    for start, stop in zip(points, points[1:]):
        duration = stop - start
        if duration:
            durations.append(duration if level else -duration)
            level = not level
    return durations


def write_sub(path: str, frame: bytes, frequency: int) -> list[int]:
    durations = sub_durations(frame)
    lines = [
        "Filetype: Flipper SubGhz RAW File",
        "Version: 1",
        f"Frequency: {frequency}",
        f"Preset: {PRESET}",
        "Protocol: RAW",
    ]
    for index in range(0, len(durations), PER_LINE):
        values = durations[index:index + PER_LINE]
        lines.append("RAW_Data: " + " ".join(str(value) for value in values))
    with open(path, "w", encoding="ascii") as stream:
        stream.write("\n".join(lines) + "\n")
    return durations


def etb_frame(**kwargs) -> bytes:
    frame = make_frame(**kwargs)
    protected = frame[3:-3]
    protected = protected[:-1] + bytes((odd_parity(ETB),))
    return frame[:3] + protected + crc_suffix(protected) + b"\x7f"


def test_frames() -> dict[str, bytes]:
    basic = make_frame(text="FLIPPER ZERO ACARS TEST")
    long = make_frame(
        address=".LONG01",
        label="Q0",
        flight="AF4321",
        text=(
            "THIS LONG ACARS MESSAGE CHECKS DISPLAY WRAPPING AND SCROLLING "
            "OVER SEVERAL ROWS ON THE RADIO SCREEN"
        ),
    )
    h1 = make_frame(
        address=".H1TEST",
        label="H1",
        flight="AF0622",
        text="#M1B/B6 LHWE1YA.ADS.TEST",
    )
    block = etb_frame(
        address=".BLOCK1", label="B1", flight="BA0248", text="ETB BLOCK END TEST"
    )
    repair = bytearray(make_frame(text="ONE PARITY BIT MUST BE REPAIRED"))
    repair[10] ^= 0x80
    badcrc = bytearray(make_frame(text="BAD CRC MUST STAY ON WAIT SCREEN"))
    badcrc[-3] ^= 0x01
    return {
        "basic": basic,
        "long": long,
        "h1_arinc": h1,
        "etb": block,
        "repair": bytes(repair),
        "badcrc": bytes(badcrc),
    }


def samples_from_durations(
    durations: list[int], sample_phase: float = 0.0
) -> list[int]:
    """Reconstruct ideal AM-detector audio for the host decoder smoke test."""
    boundaries = []
    elapsed = 0
    for duration in durations:
        elapsed += abs(duration)
        boundaries.append(elapsed)
    samples = []
    segment = 0
    period_us = 1_000_000 / 19_200
    count = math.ceil((elapsed - sample_phase) / period_us)
    for index in range(count):
        time_us = sample_phase + index * period_us
        while segment + 1 < len(boundaries) and time_us >= boundaries[segment]:
            segment += 1
        level = 1 if durations[segment] > 0 else -1
        samples.append(2048 + 700 * level)
    return samples


def validate(frames: dict[str, bytes]):
    for name in ("basic", "long", "h1_arinc", "etb"):
        durations = sub_durations(frames[name])
        for phase_step in range(8):
            sample_phase = phase_step * 1_000_000 / 19_200 / 8
            samples = samples_from_durations(durations, sample_phase)
            if not Demodulator().run(samples):
                raise AssertionError(
                    f"generated {name} waveform was not decoded at phase {phase_step}"
                )
    for name in ("repair", "badcrc"):
        samples = samples_from_durations(sub_durations(frames[name]))
        if Demodulator().run(samples):
            raise AssertionError(f"strict host decoder unexpectedly accepted {name}")

    repair = bytearray(frames["repair"][3:-3])
    repair_crc = frames["repair"][-3:-1]
    parity_errors = [
        index for index, value in enumerate(repair)
        if bin(value).count("1") % 2 == 0
    ]
    if len(parity_errors) != 1:
        raise AssertionError("repair frame must contain exactly one parity error")
    corrected = False
    for bit in range(8):
        repair[parity_errors[0]] ^= 1 << bit
        crc = 0
        for value in repair + repair_crc:
            crc = crc_update(crc, value)
        if crc == 0:
            corrected = True
            break
        repair[parity_errors[0]] ^= 1 << bit
    if not corrected:
        raise AssertionError("repair frame cannot be restored to a valid BCS")

    badcrc = frames["badcrc"]
    crc = 0
    for value in badcrc[3:-1]:
        crc = crc_update(crc, value)
    if crc == 0:
        raise AssertionError("badcrc frame unexpectedly has a valid BCS")


def frequency_supported(frequency: int) -> bool:
    return any(low <= frequency <= high for low, high in (
        (300_000_000, 348_000_000),
        (387_000_000, 464_000_000),
        (779_000_000, 928_000_000),
    ))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("out", nargs="?", default="flipper")
    parser.add_argument("--freq", type=int, default=DEFAULT_FREQ)
    args = parser.parse_args()
    if 118_000_000 <= args.freq <= 137_000_000:
        sys.exit("refusing the live aeronautical band; use a legal bench frequency")
    if not frequency_supported(args.freq):
        sys.exit("frequency is outside the Flipper Zero CC1101 operating bands")

    frames = test_frames()
    validate(frames)
    os.makedirs(args.out, exist_ok=True)
    for name, frame in frames.items():
        path = os.path.join(args.out, f"acars_{name}.sub")
        durations = write_sub(path, frame, args.freq)
        elapsed_ms = sum(abs(value) for value in durations) / 1000.0
        print(f"{path:<34} {elapsed_ms:6.0f} ms {len(durations):5d} edges")
