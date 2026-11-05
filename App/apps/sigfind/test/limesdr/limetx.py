#!/usr/local/bin/python3
"""Transmit an IQ file with a LimeSDR (Mini) through SoapySDR.

The LO is tuned --lo-offset below the target frequency and the IQ samples
are shifted up digitally by the same amount, so the LO leakage spur lands
away from the wanted signal.

Importable: LimeTx opens and calibrates the radio once, then play()/silence()
can be called any number of times (see txall.py).
"""
import argparse
import sys
import time

import numpy as np
import SoapySDR
from SoapySDR import SOAPY_SDR_TX, SOAPY_SDR_CF32, SOAPY_SDR_END_BURST

FORMATS = {"cf32", "cs16", "cs8", "cu8"}


# Distress frequencies, with margin. Hard block, no override on purpose:
#  - 406.0-406.1 MHz: Cospas-Sarsat, satellites relay anything there as a real alert
#  - 121.5 MHz: aeronautical emergency, monitored by aircraft, ATC and SAR homers
#  - 243 MHz: military distress (and the 121.5 MHz second harmonic)
FORBIDDEN = [(405.9e6, 406.2e6, "Cospas-Sarsat distress band"),
             (121.3e6, 121.7e6, "121.5 MHz aeronautical distress"),
             (242.8e6, 243.2e6, "243 MHz military distress")]


def check_freq(freq):
    for lo, hi, name in FORBIDDEN:
        if lo <= freq <= hi:
            sys.exit(f"refused: {lo / 1e6:g}-{hi / 1e6:g} MHz ({name}), never transmit there")


def load_iq(path, fmt):
    if fmt == "cf32":
        return np.fromfile(path, dtype=np.complex64)
    raw = {"cs16": np.int16, "cs8": np.int8, "cu8": np.uint8}[fmt]
    x = np.fromfile(path, dtype=raw).astype(np.float32)
    if fmt == "cs16":
        x /= 32768.0
    elif fmt == "cs8":
        x /= 128.0
    else:
        x = (x - 127.5) / 128.0
    return (x[0::2] + 1j * x[1::2]).astype(np.complex64)


class LimeTx:
    def __init__(self, freq, rate, gain=30, lo_offset=250e3, antenna=None):
        check_freq(freq)
        self.freq, self.rate, self.lo_offset = freq, rate, lo_offset
        self.n = 0      # samples sent so far: keeps the digital LO shift phase-continuous
        # string args on purpose: with SWIG 4.x a dict can hit the make(list) overload -> "no match"
        self.sdr = sdr = SoapySDR.Device("driver=lime")
        sdr.setSampleRate(SOAPY_SDR_TX, 0, rate)
        if antenna:
            sdr.setAntenna(SOAPY_SDR_TX, 0, antenna)
        sdr.setFrequency(SOAPY_SDR_TX, 0, freq - lo_offset)
        sdr.setGain(SOAPY_SDR_TX, 0, gain)
        self.st = sdr.setupStream(SOAPY_SDR_TX, SOAPY_SDR_CF32, [0])
        self.mtu = sdr.getStreamMTU(self.st)
        sdr.activateStream(self.st)
        self.t0 = time.time()

    def describe(self):
        s = self.sdr
        return "\n".join([
            f"device   : {s.getHardwareKey()}",
            f"antenna  : {s.getAntenna(SOAPY_SDR_TX, 0)}",
            f"LO       : {s.getFrequency(SOAPY_SDR_TX, 0) / 1e6:.6f} MHz",
            f"signal   : {self.freq / 1e6:.6f} MHz",
            f"rate     : {s.getSampleRate(SOAPY_SDR_TX, 0):.0f} S/s",
            f"gain     : {s.getGain(SOAPY_SDR_TX, 0):.1f} dB",
        ])

    def play(self, iq, end_burst=False):
        """Stream samples; blocks only as long as the driver buffers are full."""
        if self.lo_offset:
            t = (self.n + np.arange(len(iq), dtype=np.float64)) / self.rate
            iq = (iq * np.exp(2j * np.pi * self.lo_offset * t)).astype(np.complex64)
        peak = np.max(np.abs(iq)) if len(iq) else 0
        if peak > 1.0:
            print(f"warning: peak amplitude {peak:.2f} > 1, normalising", file=sys.stderr)
            iq /= peak
        i = 0
        while i < len(iq):
            chunk = iq[i:i + self.mtu]
            last = end_burst and i + len(chunk) >= len(iq)
            res = self.sdr.writeStream(self.st, [chunk], len(chunk),
                                       SOAPY_SDR_END_BURST if last else 0, timeoutUs=1_000_000)
            if res.ret <= 0:
                raise RuntimeError(f"writeStream error {res.ret}")
            i += res.ret
        self.n += len(iq)

    def silence(self, seconds, end_burst=False):
        """No carrier for this long (zeros keep the stream timing exact)."""
        self.play(np.zeros(int(seconds * self.rate), dtype=np.complex64), end_burst)

    def wait(self):
        """Block until every sample written so far has left the radio."""
        time.sleep(max(self.n / self.rate - (time.time() - self.t0), 0) + 0.2)

    def close(self):
        self.sdr.deactivateStream(self.st)
        self.sdr.closeStream(self.st)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("file", help="IQ file")
    p.add_argument("--freq", type=float, required=True, help="RF frequency in Hz, e.g. 433.6e6")
    p.add_argument("--rate", type=float, required=True, help="file sample rate in S/s")
    p.add_argument("--format", choices=sorted(FORMATS), default="cf32")
    p.add_argument("--gain", type=float, default=30, help="TX gain in dB (default 30, low)")
    p.add_argument("--lo-offset", type=float, default=250e3, help="LO offset in Hz (default 250e3, 0 to disable)")
    p.add_argument("--repeat", type=int, default=1, help="number of plays (default 1)")
    p.add_argument("--antenna", default=None, help="TX antenna/path override (BAND1, BAND2)")
    p.add_argument("--gap", type=float, default=0, help="seconds of no carrier between repeats (default 0)")
    a = p.parse_args()

    check_freq(a.freq)
    iq = load_iq(a.file, a.format)
    tx = LimeTx(a.freq, a.rate, a.gain, a.lo_offset, a.antenna)
    print(tx.describe())
    print(f"duration : {(len(iq) / a.rate + a.gap) * a.repeat - a.gap:.2f} s")
    try:
        for r in range(a.repeat):
            last = r == a.repeat - 1
            tx.play(iq, end_burst=last)
            if a.gap and not last:
                tx.silence(a.gap)
        tx.wait()
    finally:
        tx.close()
    print("done")


if __name__ == "__main__":
    main()
