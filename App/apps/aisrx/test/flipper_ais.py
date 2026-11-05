#!/usr/bin/env python3
"""Generate Flipper Zero Sub-GHz RAW files for the AIS RX overlay.

The stock Flipper cannot reach AIS 1/2 and must never be used there. These
files use a custom asynchronous 9.596 kbaud GFSK preset with +/-2.38 kHz
deviation on the 433.650 MHz bench channel. The radio is tuned to 433.650 MHz
FM wide; AIS RX sees a close bench approximation of the AIS GMSK waveform
through the discriminator path. Every file is checked through model_rx.py's
radio channel (low-pass 5 kHz, high-pass 1 kHz) before it is written.

Run:
  python3 -B flipper_ais.py [out_dir] [--freq 433650000]
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ais
import model_rx
from model_rx import Demodulator

PRESET = "FuriHalSubGhzPresetCustom"
STOCK_PRESET = "FuriHalSubGhzPreset2FSKDev238Async"
DEFAULT_FREQ = 433_650_000
BAUD = 9600
LEAD_US = 100_000
TAIL_US = 20_000
REPEATS = 1             # one frame per send, as a ship sends one report
PER_LINE = 512

# CC1101 asynchronous GFSK. MDMCFG4/3 select 9.5959 kbaud, MDMCFG2 selects
# GFSK/direct mode and DEVIATN selects 2.380 kHz. The remaining values follow
# Flipper's stock 2FSKDev238Async preset.
CUSTOM_PRESET = (
    0x02, 0x0D,  # IOCFG0: asynchronous serial data input
    0x0B, 0x06,  # FSCTRL1: 152.34 kHz IF
    0x08, 0x32,  # PKTCTRL0: asynchronous continuous mode
    0x07, 0x04,  # PKTCTRL1
    0x14, 0x00,  # MDMCFG0
    0x13, 0x02,  # MDMCFG1
    0x12, 0x14,  # MDMCFG2: GFSK, direct mode
    0x11, 0x83,  # MDMCFG3: data-rate mantissa
    0x10, 0x68,  # MDMCFG4: 270.8 kHz RX BW, data-rate exponent 8
    0x15, 0x04,  # DEVIATN: 2.380 kHz
    0x18, 0x18,  # MCSM0
    0x19, 0x16,  # FOCCFG
    0x1D, 0x91,  # AGCCTRL0
    0x1C, 0x00,  # AGCCTRL1
    0x1B, 0x07,  # AGCCTRL2
    0x20, 0xFB,  # WORCTRL
    0x22, 0x10,  # FREND0
    0x21, 0x56,  # FREND1
    0x00, 0x00,  # End of register list
    0xC0, 0x00, 0x00, 0x00,  # PATABLE
    0x00, 0x00, 0x00, 0x00,
)


def raw_durations(payload: bytes, invert: bool = False,
                  bad_crc: bool = False) -> list[int]:
    bits = []
    for _ in range(REPEATS):
        bits.extend(ais.wire_bits(payload, bad_crc=bad_crc))
    levels = ais.nrzi_levels(bits)
    if invert:
        levels = [1 - level for level in levels]
    values = [0] + levels + [0]
    times = [0.0, float(LEAD_US)]
    for index in range(len(levels)):
        times.append(LEAD_US + (index + 1) * 1_000_000.0 / BAUD)
    times.append(times[-1] + TAIL_US)

    out = []
    start = 0
    level = values[0]
    for index in range(1, len(values) + 1):
        if index == len(values) or values[index] != level:
            duration = round(times[index]) - round(times[start])
            if duration:
                out.append(-duration if level else duration)
            if index < len(values):
                start = index
                level = values[index]
    return out


def write_durations(path: str, durations: list[int], frequency: int,
                    preset: str = PRESET) -> None:
    lines = [
        "Filetype: Flipper SubGhz RAW File",
        "Version: 1",
        f"Frequency: {frequency}",
        f"Preset: {preset}",
    ]
    if preset == PRESET:
        lines.extend((
            "Custom_preset_module: CC1101",
            "Custom_preset_data: " + " ".join(
                f"{value:02X}" for value in CUSTOM_PRESET),
        ))
    lines.append("Protocol: RAW")
    for index in range(0, len(durations), PER_LINE):
        lines.append("RAW_Data: " + " ".join(
            str(value) for value in durations[index:index + PER_LINE]))
    with open(path, "w", encoding="ascii") as stream:
        stream.write("\n".join(lines) + "\n")


def write_sub(path: str, payload: bytes, frequency: int,
              invert: bool = False, bad_crc: bool = False,
              preset: str = PRESET) -> list[int]:
    durations = raw_durations(payload, invert, bad_crc)
    write_durations(path, durations, frequency, preset)
    return durations


def discriminator(durations: list[int], gfsk: bool) -> list[float]:
    """Discriminator output (Hz) of a RAW file: + run = one tone."""
    wave = []
    elapsed = 0
    for duration in durations:
        count = (round((elapsed + abs(duration)) * model_rx.FS_SIM / 1e6) -
                 round(elapsed * model_rx.FS_SIM / 1e6))
        elapsed += abs(duration)
        wave += [1.0 if duration > 0 else -1.0] * count
    if gfsk:
        wave = model_rx.gaussian(wave, 0.5, 9596)
    return [2380.0 * value for value in wave]


def test_frames() -> dict[str, tuple[bytes, bool, bool]]:
    payloads = ais.test_payloads()
    return {
        "classa": (payloads["classa"], False, False),
        "type2": (payloads["type2"], False, False),
        "type3": (payloads["type3"], False, False),
        "moored": (payloads["moored"], False, False),
        "classb": (payloads["classb"], False, False),
        "classb19": (payloads["classb19"], False, False),
        "static5": (payloads["static5"], False, False),
        "static24": (payloads["static24"], False, False),
        "static24b": (payloads["static24b"], False, False),
        "inland": (payloads["inland"], False, False),
        "inverted": (payloads["classa"], True, False),
        "badcrc": (payloads["classa"], False, True),
    }


def check_runs(name: str, durations: list[int]) -> None:
    if not durations or durations[0] <= 0 or any(value == 0 for value in durations):
        raise AssertionError(f"generated {name} has an invalid first RAW run")
    if any((a < 0) == (b < 0) for a, b in zip(durations, durations[1:])):
        raise AssertionError(f"generated {name} has adjacent RAW runs of one polarity")


def validate(frames: dict[str, tuple[bytes, bool, bool]]) -> None:
    for index, (name, (payload, invert, bad_crc)) in enumerate(frames.items()):
        durations = raw_durations(payload, invert, bad_crc)
        check_runs(name, durations)
        samples = model_rx.channel(discriminator(durations, True), lp=5000,
                                   hp=1000, noise=150, phase=(index & 3) * 0.25,
                                   seed=index + 1)
        receiver = Demodulator()
        got = receiver.run(samples)
        expected = [] if bad_crc else [payload] * REPEATS
        if got != expected or receiver.sync != REPEATS:
            raise AssertionError(
                f"generated {name} failed: {len(got)} frames, sync {receiver.sync}")


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
    if 156_000_000 <= args.freq <= 163_000_000:
        sys.exit("refusing the live maritime band; use a legal bench frequency")
    if not frequency_supported(args.freq):
        sys.exit("frequency is outside the Flipper Zero CC1101 operating bands")

    frames = test_frames()
    validate(frames)
    os.makedirs(args.out, exist_ok=True)
    for name, (payload, invert, bad_crc) in frames.items():
        path = os.path.join(args.out, f"ais_{name}.sub")
        durations = write_sub(path, payload, args.freq, invert, bad_crc)
        elapsed_ms = sum(abs(value) for value in durations) / 1000.0
        detail = "rejected CRC" if bad_crc else str(ais.parse(payload))
        print(f"{path:<34} {elapsed_ms:6.0f} ms {len(durations):4d} runs  {detail}")

    probe_payload = frames["classa"][0]
    probe_path = os.path.join(args.out, "probe_ais_stock.sub")
    probe_durations = write_sub(
        probe_path, probe_payload, args.freq, preset=STOCK_PRESET)
    print(f"{probe_path:<34} {sum(map(abs, probe_durations)) / 1000:6.0f} ms "
          f"{len(probe_durations):4d} runs  stock 2-FSK, one valid frame")

