#!/usr/bin/env python3
"""Synthesize what the K1 ADC sees on PA4 while a 406 beacon burst is received.

Chain: biphase-L frame -> +/-1.1 rad phase with raised-cosine transitions ->
carrier with frequency offset -> AWGN in a 25 kHz channel -> FM discriminator
(RAW RX: no de-emphasis, no 300 Hz / 3 kHz filters) -> audio-path low-pass and AC
coupling -> ADC at 9.6 kHz (optional clock error) -> 12-bit around the 2048
bias the app sets on PA4. Output: little-endian uint16 samples.

Before, between and after bursts there is no carrier, so the discriminator
outputs full-scale noise, as a real receiver does.
"""
import argparse
import numpy as np

FS_SIM = 192000
FS_ADC = 9600
BIAS = 2048         # PA4 held at mid-scale by the MCU DAC (unbuffered), as in the app
LSB_PER_HZ = 0.065  # ~2.3 kHz pulse peak -> ~150 LSB: measured 1896-2194 at 9.6 kHz (406 Lab v1.5)


def frame_bits(hexstr, nbits):
    v = int(hexstr, 16)
    return [(v >> (nbits - 1 - i)) & 1 for i in range(nbits)]


def lowpass(x, fc, fs, taps=255):
    n = np.arange(taps) - (taps - 1) / 2
    h = np.sinc(2 * fc / fs * n) * np.hanning(taps)
    return np.convolve(x, h / h.sum(), mode="same")


def burst_phase(bits, rise_us, lead_ms):
    half = FS_SIM // 800                              # samples per half-bit
    levels = []
    for b in bits:
        levels += [1.1, -1.1] if b else [-1.1, 1.1]
    ph = np.repeat(np.array(levels), half)
    ph = np.concatenate([np.zeros(int(FS_SIM * lead_ms / 1000)), ph])
    w = max(3, int(FS_SIM * rise_us * 1e-6))
    win = np.hanning(w)
    return np.convolve(ph, win / win.sum(), mode="same")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame", required=True, help="frame hex (36 = long, 28 = short)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--bursts", type=int, default=1)
    ap.add_argument("--gap-ms", type=float, default=300)
    ap.add_argument("--lead-ms", type=float, default=160)
    ap.add_argument("--cnr", type=float, default=30, help="carrier/noise in 25 kHz, dB")
    ap.add_argument("--foff", type=float, default=0, help="carrier offset, Hz")
    ap.add_argument("--rise-us", type=float, default=150)
    ap.add_argument("--audio-lpf", type=float, default=5000)
    ap.add_argument("--hpf", type=float, default=30, help="AC coupling corner, Hz")
    ap.add_argument("--deemph-us", type=float, default=0,
                    help="1-pole de-emphasis time constant in the audio circuit, us (0 = none)")
    ap.add_argument("--gain", type=float, default=LSB_PER_HZ, help="ADC LSB per Hz of deviation")
    ap.add_argument("--clock-ppm", type=float, default=0)
    ap.add_argument("--invert", action="store_true")
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    rng = np.random.default_rng(a.seed)
    nbits = len(a.frame) * 4
    bits = frame_bits(a.frame, nbits)

    gap = np.zeros(int(FS_SIM * a.gap_ms / 1000))
    parts, amps = [gap], [np.zeros_like(gap)]
    for _ in range(a.bursts):
        ph = burst_phase(bits, a.rise_us, a.lead_ms)
        parts += [ph, gap]
        amps += [np.ones_like(ph), np.zeros_like(gap)]
    phase = np.concatenate(parts)
    amp = np.concatenate(amps)
    t = np.arange(len(phase)) / FS_SIM
    z = amp * np.exp(1j * (phase + 2 * np.pi * a.foff * t))

    # AWGN, carrier/noise measured in a 25 kHz channel
    nstd = np.sqrt((10 ** (-a.cnr / 10)) * FS_SIM / 25000 / 2)
    z = z + nstd * (rng.standard_normal(len(z)) + 1j * rng.standard_normal(len(z)))
    z = lowpass(z.real, 12500, FS_SIM) + 1j * lowpass(z.imag, 12500, FS_SIM)

    f = np.angle(z[1:] * np.conj(z[:-1])) * FS_SIM / (2 * np.pi)   # Hz
    f = lowpass(f, a.audio_lpf, FS_SIM)
    if a.deemph_us > 0:                                           # 1-pole RC low-pass
        k = 1 - np.exp(-1 / (FS_SIM * a.deemph_us * 1e-6))
        y = np.empty_like(f)
        acc = 0.0
        for i, v in enumerate(f):
            acc += k * (v - acc)
            y[i] = acc
        f = y * (a.deemph_us * 1e-6) * 2 * np.pi * 2300 / 1.1     # keep a comparable swing
    if a.hpf > 0:                                                 # 1-pole AC coupling
        alpha = np.exp(-2 * np.pi * a.hpf / FS_SIM)
        y = np.empty_like(f)
        acc = prev = 0.0
        for i, v in enumerate(f):
            acc = alpha * (acc + v - prev)
            prev = v
            y[i] = acc
        f = y
    if a.invert:
        f = -f

    step = FS_SIM / FS_ADC * (1 + a.clock_ppm * 1e-6)
    idx = np.arange(0, len(f) - 1, step)
    s = np.interp(idx, np.arange(len(f)), f)
    adc = np.clip(np.round(BIAS + a.gain * s), 0, 4095).astype("<u2")
    adc.tofile(a.out)


if __name__ == "__main__":
    main()
