#!/usr/bin/env python3
"""Measure the tones of a selcall burst: frequency and duration of each tone.

Tells CCIR-1 (100 ms tones) from CCIR-2 (70 ms), which multimon-ng cannot, and
shows the repeat tone that tells ZVEI-1 (2600 Hz) from ZVEI-2 (970 Hz).

Input: the FM-demodulated audio of the burst, either a WAV file or raw signed
16-bit mono (the rtl_fm output), e.g.

    rtl_fm -f 433.650M -M fm -s 22050 - | tee burst.raw | multimon-ng -t raw -a CCIR -a ZVEI1 -a ZVEI2 -
    measure_tones.py burst.raw --rate 22050

    measure_tones.py burst.wav

Method: the audio is first limited to 300-3000 Hz (removes the CTCSS and the
noise of an unfiltered FM discriminator), then a 20 ms analysis window is moved
in 1 ms steps; in each window, the strongest
of the known selcall frequencies (ZVEI and CCIR digits and repeat tones) is kept
if it clearly dominates; consecutive windows with the same tone form one tone.
The detected length of a tone is a few ms short (the window straddles two tones
at each transition); the spacing between the starts of consecutive tones is not
affected, and since selcall tones follow each other without a gap it is the tone
duration: it is what decides between 70 ms and 100 ms.
Needs numpy.
"""
import argparse
import sys
import wave

import numpy as np

ZVEI = [2400, 1060, 1160, 1270, 1400, 1530, 1670, 1830, 2000, 2200]
CCIR = [1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860]
REPEAT = {2600: "R (ZVEI-1)", 970: "R (ZVEI-2)", 2110: "R (CCIR)"}


def label(f):
    """what a frequency means in each standard"""
    names = []
    if f in ZVEI:
        names.append("ZVEI %d" % ZVEI.index(f))
    if f in CCIR:
        names.append("CCIR %d" % CCIR.index(f))
    if f in REPEAT:
        names.append(REPEAT[f])
    return ", ".join(names)


def load(path, rate):
    if path.lower().endswith(".wav"):
        with wave.open(path, "rb") as w:
            rate = w.getframerate()
            ch, width = w.getnchannels(), w.getsampwidth()
            data = w.readframes(w.getnframes())
        if width != 2:
            sys.exit("WAV must be 16-bit")
        x = np.frombuffer(data, "<i2").astype(float)
        if ch > 1:
            x = x.reshape(-1, ch)[:, 0]
        return x, rate
    return np.fromfile(path, "<i2").astype(float), rate


def bandpass(x, rate, lo=300.0, hi=3000.0):
    """keep the selcall band: removes the CTCSS/DCS (< 300 Hz) and the noise that
    an unfiltered FM discriminator (rtl_fm) puts above 3 kHz"""
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / rate)
    spec[(f < lo) | (f > hi)] = 0
    return np.fft.irfft(spec, len(x))


def measure(x, rate, win_ms=20.0, step_ms=1.0, dominance=0.6, min_ms=15.0):
    x = bandpass(x, rate)
    freqs = sorted(set(ZVEI) | set(CCIR) | set(REPEAT))
    n = int(rate * win_ms / 1000)
    step = max(1, int(rate * step_ms / 1000))
    t = np.arange(n) / rate
    win = np.hanning(n)
    basis = np.array([np.exp(-2j * np.pi * f * t) * win for f in freqs])   # one row per tone

    labels = []
    for start in range(0, len(x) - n, step):
        seg = x[start:start + n]
        seg = seg - seg.mean()
        # share of the window's power carried by each known tone (1.0 = pure tone)
        amp = 2 * np.abs(basis @ seg) / win.sum()                        # tone amplitude
        power = np.sum((seg * win) ** 2) / np.sum(win ** 2) + 1e-9        # window power
        share = (amp ** 2 / 2) / power
        k = int(np.argmax(share))
        labels.append(freqs[k] if share[k] > dominance else None)

    # consecutive windows with the same tone -> one tone; window centre = time
    tones, cur, first = [], None, 0
    for i, f in enumerate(labels + [None]):
        if f != cur:
            if cur is not None:
                dur = (i - first) * step_ms
                if dur >= min_ms:
                    tones.append((first * step_ms + win_ms / 2, dur, cur))
            cur, first = f, i
    return tones


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", help="WAV or raw s16le mono audio of the burst")
    ap.add_argument("--rate", type=int, default=22050, help="sample rate of a raw file (default 22050)")
    a = ap.parse_args()

    x, rate = load(a.file, a.rate)
    tones = measure(x, rate)
    if not tones:
        sys.exit("no selcall tone found")

    print("  start      spacing  detected  frequency  meaning")
    for i, (start, dur, f) in enumerate(tones):
        nxt = tones[i + 1][0] if i + 1 < len(tones) else None
        gap = "%5.0f ms" % (nxt - start) if nxt is not None and nxt - start < dur + 60 else "      -"
        print("  %7.0f ms  %s  %5.0f ms  %5d Hz   %s" % (start, gap, dur, f, label(f)))

    # group tones into bursts (gap > 150 ms) and classify each one
    bursts, cur = [], [tones[0]]
    for tone in tones[1:]:
        prev = cur[-1]
        if tone[0] - (prev[0] + prev[1]) > 150:
            bursts.append(cur)
            cur = []
        cur.append(tone)
    bursts.append(cur)

    for b in bursts:
        # tone duration = spacing between consecutive tone starts (no gap in selcall)
        spacings = [b[i + 1][0] - b[i][0] for i in range(len(b) - 1)]
        med = float(np.median(spacings)) if spacings else b[0][1]
        fs = {f for _, _, f in b}
        family = "CCIR" if fs & (set(CCIR) | {2110}) and not fs & {2600, 970} else "ZVEI"
        if family == "CCIR":
            std = "CCIR-1 (100 ms)" if med > 85 else "CCIR-2 (70 ms)"
        else:
            std = "ZVEI-1" if 2600 in fs else "ZVEI-2" if 970 in fs else "ZVEI (no repeat tone: 1 or 2)"
        print("burst at %.0f ms: %d tones, tone duration %.0f ms -> %s"
              % (b[0][0], len(b), med, std))


if __name__ == "__main__":
    main()
