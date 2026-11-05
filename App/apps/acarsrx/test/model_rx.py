#!/usr/bin/env python3
"""Host model for the ACARS RX overlay.

The radio path is modelled at 19.2 kHz: DC removal, coherent MSK detection,
carrier/clock tracking, and the ACARS character/CRC state machine. The
synthetic transmitter stays separate from the receiver so the tests exercise
the complete wire format.
"""

from __future__ import annotations

import math
import random

FS = 19_200
BAUD = 2_400
SPB = FS // BAUD
SYN = 0x16
SOH = 0x01
ETX = 0x03
ETB = 0x17
MAX_TEXT = 240


def crc_update(crc: int, byte: int) -> int:
    """ACARS CRC-16, polynomial 0x1021 with each byte entering LSB first."""
    for bit in range(8):
        top = crc & 0x8000
        crc = (crc << 1) & 0xFFFF
        if byte & (1 << bit):
            crc |= 1
        if top:
            crc ^= 0x1021
    return crc


def odd_parity(value: int) -> int:
    value &= 0x7F
    if bin(value).count("1") % 2 == 0:
        value |= 0x80
    return value


def crc_suffix(data: bytes) -> bytes:
    crc = 0
    for byte in data:
        crc = crc_update(crc, byte)
    for suffix in range(65_536):
        low, high = suffix & 0xFF, suffix >> 8
        if crc_update(crc_update(crc, low), high) == 0:
            return bytes((low, high))
    raise AssertionError("CRC suffix not found")


def make_frame(
    address: str = ".FTEST1",
    label: str = "H1",
    flight: str = "AF1234",
    text: str = "PARIS TEST",
) -> bytes:
    address = address[:7].ljust(7)
    label = label[:2].ljust(2)
    flight = flight[:6].ljust(6)
    body = "2" + address + "\x15" + label + "A" + "\x02" + "M01A" + flight + text
    protected = bytes(odd_parity(ord(ch)) for ch in body) + bytes((odd_parity(ETX),))
    return bytes((SYN, SYN, SOH)) + protected + crc_suffix(protected) + b"\x7f"


def bytes_to_bits(data: bytes):
    for byte in data:
        for bit in range(8):
            yield (byte >> bit) & 1


def modulate(
    frame: bytes,
    *,
    noise: float = 0.0,
    amplitude: float = 900.0,
    dc: float = 2048.0,
    phase: float = 0.37,
    lead_samples: int = 37,
    seed: int = 1,
    rate_error: float = 0.0,
    carrier_offset: float = 0.0,
) -> list[int]:
    """Generate MSK as staggered I/Q half-sine pulses at 2400 bit/s."""
    rng = random.Random(seed)
    samples = [round(dc + rng.gauss(0.0, noise)) for _ in range(lead_samples)]
    wire = bytes((0xFF,) * 16) + bytes((0xAB, 0x2A)) + frame
    bits = list(bytes_to_bits(wire))
    coefficients = [
        (1.0 if bit else -1.0) * (-1.0 if index & 2 else 1.0)
        for index, bit in enumerate(bits)
    ]
    for index in range(len(bits) * SPB + 2 * SPB):
        position = index * (1.0 + rate_error)
        symbol = int(position / SPB)
        i_value = q_value = 0.0
        for pulse in (symbol - 1, symbol):
            if not 0 <= pulse < len(coefficients):
                continue
            within = position - pulse * SPB
            shape = math.sin(math.pi * within / (2 * SPB))
            if pulse & 1:
                q_value += coefficients[pulse] * shape
            else:
                i_value += coefficients[pulse] * shape
        carrier_phase = phase + 2.0 * math.pi * (
            1800.0 * (1.0 + rate_error) + carrier_offset
        ) * index / FS
        value = dc + amplitude * (
            i_value * math.cos(carrier_phase) - q_value * math.sin(carrier_phase)
        )
        samples.append(round(value + rng.gauss(0.0, noise)))
    # Idle tail: the receiver decides two bits late and may sit a bit off.
    samples.extend(round(dc + rng.gauss(0.0, noise)) for _ in range(SPB * 32))
    return samples


def frame_crc(data) -> int:
    crc = 0
    for value in data:
        crc = crc_update(crc, value)
    return crc


def parity_errors(text) -> list[int]:
    return [index for index, value in enumerate(text)
            if bin(value).count("1") % 2 == 0]


def repair(text: bytearray, crc: bytes) -> bool:
    """Flip one bit in each of at most two parity-marked bytes, as repair()."""
    bad = parity_errors(text)
    if not 1 <= len(bad) <= 2:
        return False
    for first in range(8):
        text[bad[0]] ^= 1 << first
        if len(bad) == 1:
            if frame_crc(text + crc) == 0:
                return True
        else:
            for second in range(8):
                text[bad[1]] ^= 1 << second
                if frame_crc(text + crc) == 0:
                    return True
                text[bad[1]] ^= 1 << second
        text[bad[0]] ^= 1 << first
    return False


class Demodulator:
    """Integer MSK receiver mirrored by acarsrx_app.c.

    `frames` holds the blocks that passed parity and BCS as received (the
    strict decoder); `repaired` holds those accepted only after repair().
    """

    PREKEY, SOH1, TEXT, CRC1, CRC2, END = range(6)
    PHASE_NOMINAL = 6144       # 1800 / 19200 of a 16-bit turn
    BIT_PHASE = 49152          # 3/4 turn = one 2400 bit/s symbol
    PLL_I_LIMIT = 192 * 16
    ACQ_GAIN = 24              # phase gain multiplier before the text
    ACQ_I = 3                  # integrator step per unit of phase step, acquiring
    TRACK_I = 8                # integrator step once in the text
    TIMING_ACQ = 1024          # bit clock step (1/6 sample) before the text
    TIMING_TRACK = 256
    PREKEY_LEAK = 3
    SYNC = 0x1616              # SYN SYN, last 16 bits
    SYNC_INVERTED = 0xE9E9

    def __init__(self):
        self.dc = 2048 << 4
        self.ring = [(0, 0)] * 16
        self.slot = 0
        self.phase = 0
        self.clock = 0
        self.pll_i = 0
        self.symbol = 0
        self.quadrature = 0
        self.cosine = [round(127 * math.cos(2 * math.pi * n / 64)) for n in range(64)]
        self.taps = [round(127 * math.sin(math.pi * n / 16)) for n in range(16)]
        self.frames: list[bytes] = []
        self.repaired: list[bytes] = []
        self.shift = 0
        self.invert = 0
        self._reset_decoder()

    def _reset_decoder(self):
        self.state = self.PREKEY
        self.nbits = 8
        self.prekey = 0
        self.text = bytearray()
        self.crc = bytearray()

    def busy(self) -> bool:
        """modemBusy in listen(): screen, battery and keys must wait."""
        return self.state != self.PREKEY or abs(self.prekey) >= 16

    def _decoded_byte(self, byte: int):
        if self.state == self.SOH1:
            if byte != SOH:
                self._reset_decoder()
                return
            self.state = self.TEXT
            self.text.clear()
            self.crc.clear()
            return
        if self.state == self.TEXT:
            if len(self.text) >= MAX_TEXT:
                self._reset_decoder()
                return
            self.text.append(byte)
            if byte in (odd_parity(ETX), odd_parity(ETB)):
                self.state = self.CRC1
            return
        if self.state == self.CRC1:
            self.crc[:] = bytes((byte,))
            self.state = self.CRC2
            return
        if self.state == self.CRC2:
            self.crc.append(byte)
            if len(self.text) >= 13:
                if frame_crc(self.text + self.crc) == 0 and not parity_errors(self.text):
                    self.frames.append(bytes(value & 0x7F for value in self.text[:-1]))
                elif repair(self.text, bytes(self.crc)):
                    self.repaired.append(bytes(value & 0x7F for value in self.text[:-1]))
            self.state = self.END
            return
        self._reset_decoder()

    def _prekey_bit(self, bit: bool):
        """Leaky run counter: noise decays to zero, the pre-key tone ramps."""
        step = 1 if bit else -1
        if (self.prekey ^ step) < 0:
            step *= self.PREKEY_LEAK
        if -127 <= self.prekey + step <= 127:
            self.prekey += step
        if self.shift in (self.SYNC, self.SYNC_INVERTED):
            self.invert = 0xFF if self.shift & 1 else 0
            self.state = self.SOH1
            self.nbits = 8

    def _pll_update(self, value: int, error: int):
        av, ae = abs(value) >> 8, abs(error) >> 8
        sign = -1 if error < 0 else 1 if error > 0 else 0
        if ae > av * 2:
            proportional = 40
        elif ae > av:
            proportional = 28
        elif ae * 2 > av:
            proportional = 16
        elif ae * 4 > av:
            proportional = 6
        else:
            proportional = 0
        integral = self.TRACK_I if proportional else 0
        if self.state < self.TEXT:
            integral = proportional * self.ACQ_I
            proportional *= self.ACQ_GAIN
        self.phase = (self.phase + sign * proportional) & 0xFFFF
        if abs(self.prekey) >= 8 or self.state != self.PREKEY:
            self.pll_i += sign * integral
            self.pll_i = max(-self.PLL_I_LIMIT, min(self.PLL_I_LIMIT, self.pll_i))
        else:
            self.pll_i = 0

    def _decision(self):
        i_value = q_value = 0
        for index, tap in enumerate(self.taps):
            i_part, q_part = self.ring[(self.slot + index) & 15]
            i_value += tap * i_part
            q_value += tap * q_part
        if self.symbol & 1:
            value, other = q_value, i_value
            error = -i_value if value >= 0 else i_value
        else:
            value, other = i_value, q_value
            error = q_value if value >= 0 else -q_value
        bit = value < 0 if self.symbol & 2 else value > 0
        # Symbol timing. The previous decision's quadrature rail sits between
        # the symbols before and after it: when those two differ (equal bits,
        # the rail sign alternates) its sign tells early from late.
        if ((self.shift >> 14) & 1) == int(bit):
            step = self.TIMING_ACQ if self.state < self.TEXT else self.TIMING_TRACK
            self.clock += -step if (self.quadrature ^ value) < 0 else step
        self.quadrature = other
        self._pll_update(value, error)
        self.symbol = (self.symbol + 1) & 0xFF
        self.shift = ((self.shift >> 1) | (int(bit) << 15)) & 0xFFFF
        if self.state == self.PREKEY:
            self._prekey_bit(bit)
            return
        self.nbits -= 1
        if self.nbits == 0:
            self.nbits = 8
            self._decoded_byte((self.shift >> 8) ^ self.invert)

    def sample(self, sample: int):
        self.dc += ((sample << 4) - self.dc) >> 6
        value = sample - (self.dc >> 4)
        step = self.PHASE_NOMINAL + (self.pll_i >> 4)
        self.clock += step
        if self.clock >= self.BIT_PHASE:
            self.clock -= self.BIT_PHASE
            self._decision()
        self.phase = (self.phase + step) & 0xFFFF
        phase = self.phase >> 10
        self.ring[self.slot] = (
            value * self.cosine[phase],
            value * self.cosine[(phase + 16) & 63],
        )
        self.slot = (self.slot + 1) & 15

    def run(self, samples: list[int]) -> list[bytes]:
        for sample in samples:
            self.sample(sample)
        return self.frames


def test_clean_frame():
    decoded = Demodulator().run(modulate(make_frame()))
    assert decoded, "clean synthetic frame was not decoded"
    assert b"AF1234PARIS TEST" in decoded[0]
    return 1


def test_timing_and_phase():
    """Every sample offset of the bit clock against every carrier phase."""
    frame = make_frame(address=".FGKXY", label="Q0", text="HELLO FROM PARIS")
    cases = 0
    for offset in range(SPB):
        for step in range(8):
            samples = modulate(
                frame,
                noise=150.0,
                amplitude=750.0,
                phase=2.0 * math.pi * step / 8,
                lead_samples=32 + offset,
                seed=offset * 8 + step,
            )
            decoded = Demodulator().run(samples)
            assert decoded, f"offset {offset}, phase step {step} was not decoded"
            assert b"HELLO FROM PARIS" in decoded[0]
            cases += 1
    return cases


def test_sample_clock():
    """The MCU runs from its internal RC oscillator: +/-1.5 % must decode."""
    long = make_frame(text="LONG " + "ABCDEFGHIJ0123456789" * 10) + b"\x7f" * 8
    cases = 0
    for rate in (-0.015, -0.01, -0.005, 0.005, 0.01, 0.015):
        for seed in range(4):
            samples = modulate(
                long,
                noise=150.0,
                amplitude=750.0,
                phase=seed * 0.9,
                lead_samples=32 + 2 * seed,
                seed=seed,
                rate_error=rate,
            )
            assert Demodulator().run(samples), f"clock {rate:+.1%} seed {seed} failed"
            cases += 1
    return cases


def test_weak_and_after_noise():
    frame = make_frame(text="WEAK SIGNAL")
    rng = random.Random(5)
    noise = [round(2048 + rng.gauss(0.0, 300.0)) for _ in range(FS)]
    cases = 0
    for seed in range(8):
        weak = modulate(frame, noise=6.0, amplitude=40.0, seed=seed,
                        phase=seed * 0.7, lead_samples=30 + seed)
        assert Demodulator().run(weak), f"weak frame {seed} was not decoded"
        after = modulate(frame, noise=150.0, seed=seed, phase=seed * 0.7,
                         lead_samples=30 + seed)
        assert Demodulator().run(noise + after), f"frame {seed} after noise failed"
        cases += 2
    return cases


def test_crc_rejects_corruption():
    frame = bytearray(make_frame())
    frame[14] ^= 0x06          # two flips in one byte keep its parity valid
    demodulator = Demodulator()
    assert not demodulator.run(modulate(bytes(frame)))
    assert not demodulator.repaired, "a parity-clean BCS failure must not be repaired"
    return 1


def test_repair():
    cases = 0
    for flips in ((10, 0x80), (20, 0x02), (9, 0x01, 30, 0x40)):
        frame = bytearray(make_frame(text="PARITY REPAIR"))
        for index in range(0, len(flips), 2):
            frame[flips[index]] ^= flips[index + 1]
        demodulator = Demodulator()
        assert not demodulator.run(modulate(bytes(frame)))
        assert demodulator.repaired, f"flips {flips} were not repaired"
        assert b"PARITY REPAIR" in demodulator.repaired[0]
        cases += 1
    return cases


def test_idle_stays_responsive():
    """Without a signal the modem must not claim the slot (keys, screen)."""
    cases = 0
    for sigma in (0.6, 20.0, 300.0):
        rng = random.Random(7)
        demodulator = Demodulator()
        busy = slots = 0
        for index in range(20 * FS):
            demodulator.sample(round(2048 + rng.gauss(0.0, sigma)))
            if index % 960 == 959:
                slots += 1
                busy += demodulator.busy()
        assert busy * 50 <= slots, f"idle busy {busy}/{slots} at sigma {sigma}"
        assert not demodulator.frames and not demodulator.repaired
        cases += 1
    return cases


if __name__ == "__main__":
    total = 0
    for test in (test_clean_frame, test_timing_and_phase, test_sample_clock,
                 test_weak_and_after_noise, test_crc_rejects_corruption,
                 test_repair, test_idle_stays_responsive):
        total += test()
    print(f"ACARS RX model: {total}/{total} synthetic cases passed")
