#!/usr/local/bin/python3
"""121.5 / 243 MHz ELT homing signal (swept-tone AM), generated live and
transmitted continuously with the LimeSDR, by default on 433.65 MHz.

Signal (RTCA DO-183 sec. 2, called up by TSO-C91a; same as ETSI EN 300 152):
  - A3X AM, full carrier, modulation index 0.85..1.0
  - audio tone sweeping DOWNWARD, >= 700 Hz span inside 1600..300 Hz
  - sweep repetition rate 2..4 Hz
  - audio duty cycle 33..55 % (rectangular tone)
  - TSO-C91a: stable carrier, >= 30 % of the energy within +/-30 Hz of it
The baseband is identical for 121.5 and 243 MHz, only the RF changes.

Defaults match a real beacon recording (RadioBeacon.mp3): 2.42 sweeps/s,
1570 -> 820 Hz, tone PERIOD growing linearly (RC-ramp oscillator: fast drop
at the top, slower near the bottom), near-sine tone. --law linear and
--wave square give the textbook linear-Hz rectangular variant instead.

One sweep is built once (each sweep restarts at audio phase 0, so it loops
seamlessly) and replayed until Ctrl-C or --seconds.

  homer121.py [--freq 433.65e6] [--gain 30] [--sweep-rate 2.42] [--law period|linear] [--wave sine|square]
  homer121.py --save homer.cs16 --seconds 10 --no-tx     (file only)

Keys while transmitting (to walk a detector through signal levels):
  up/down or +/-   gain +/-1 dB        PgUp/PgDn or ]/[   gain +/-10 dB
  digits + Enter   set exact gain      m                  mute / unmute
  q or Ctrl-C      stop
A change is applied at the next sweep boundary (<= 0.5 s).

Station ID: --id CALL is sent in Morse at start-up and then every --id-every
seconds (default N0CALL, 300 s) as MCW: an 800 Hz keyed tone on the same AM
carrier, so any AM receiver hears it. It replaces the sweep for its duration
(~4 s at 20 wpm); skipped while muted, sent as soon as unmuted. --id '' turns it off.
  homer121.py --wav homer.wav --no-tx                      (listen to it)
"""
import argparse
import os
import sys
import termios
import threading
import time
import tty
import wave

import numpy as np

RATE = 1e6

# real homing frequencies (+ margin): never put this signal there
FORBIDDEN = [(121.3e6, 121.7e6, "121.5 MHz aeronautical distress"),
             (242.8e6, 243.2e6, "243 MHz military distress")]


def check_homer_freq(freq):
    for lo, hi, name in FORBIDDEN:
        if lo <= freq <= hi:
            sys.exit(f"refused: {lo / 1e6:g}-{hi / 1e6:g} MHz ({name}), never transmit there")


def audio_sweep(rate, f_hi, f_lo, sweep_rate, duty, cutoff, law="period", wave="sine"):
    """One downward sweep of the tone, low-passed, values -1..+1."""
    n = int(round(rate / sweep_rate))
    t = np.arange(n) / rate
    T = n / rate
    if law == "linear":
        # f(t) = f_hi - (f_hi - f_lo) t / T, phase in cycles
        cycles = f_hi * t - (f_hi - f_lo) * t * t / (2 * T)
    else:
        # period p(t) = p_hi + (p_lo - p_hi) t / T, cycles = integral of dt / p(t)
        p_hi, p_lo = 1 / f_hi, 1 / f_lo
        k = (p_lo - p_hi) / T
        cycles = np.log1p(k * t / p_hi) / k
    if wave == "sine":
        a = np.sin(2 * np.pi * cycles)
    else:
        a = np.where(np.mod(cycles, 1.0) < duty, 1.0, -1.0)
    # circular low-pass (raised-cosine edge from cutoff to 2*cutoff): keeps the
    # RF spectrum narrow and the buffer still loops without a click
    A = np.fft.rfft(a)
    f = np.fft.rfftfreq(n, 1 / rate)
    edge = np.clip((f - cutoff) / cutoff, 0, 1)
    A *= 0.5 * (1 + np.cos(np.pi * edge))
    a = np.fft.irfft(A, n)
    return a / np.abs(a).max()                  # filter ringing must not push m past --mod-index


MORSE = dict(zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/", (
    ".- -... -.-. -.. . ..-. --. .... .. .--- -.- .-.. -- -. --- .--. --.- .-. ... - ..- ...- .-- -..- -.-- --.. "
    "----- .---- ..--- ...-- ....- ..... -.... --... ---.. ----. -..-.").split()))


def morse_iq(text, rate=RATE, wpm=20, tone=800, m=0.9, amp=0.9, ramp=0.005):
    """MCW: keyed tone AM-modulating a steady carrier, same level scale as homer_iq.
    PARIS timing (dit = 1.2 / wpm s), raised-cosine key edges against clicks."""
    dit = 1.2 / wpm
    key = []                                    # (on, length in dits)
    for i, word in enumerate(text.upper().split()):
        if i:
            key.append((0, 4))                  # 7-dit word gap (3 already added after the last letter)
        for ch in word:
            for sym in MORSE[ch]:
                key += [(1, 1 if sym == "." else 3), (0, 1)]
            key[-1] = (0, 3)                    # letter gap
    key.append((0, 4))                          # trailing silence before the sweep resumes
    k = np.concatenate([np.full(int(round(d * dit * rate)), float(on)) for on, d in key])
    r = int(ramp * rate)
    edge = 0.5 - 0.5 * np.cos(np.pi * np.arange(r) / r)
    k = np.convolve(k, edge / edge.sum(), mode="same")
    t = np.arange(len(k)) / rate
    env = 1 + m * k * np.sin(2 * np.pi * tone * t)
    return (env * amp / (1 + m)).astype(np.complex64)


def homer_iq(rate=RATE, f_hi=1570, f_lo=820, sweep_rate=2.42, duty=0.5, m=0.9, cutoff=5000, amp=0.9,
             law="period", wave="sine"):
    a = audio_sweep(rate, f_hi, f_lo, sweep_rate, duty, cutoff, law, wave)
    env = np.clip(1 + m * a, 0, None)          # AM, carrier at 0 Hz in baseband
    env *= amp / env.max()
    return env.astype(np.complex64)


def save_cs16(path, iq):
    x = np.empty(2 * len(iq), dtype=np.int16)
    x[0::2] = np.round(iq.real * 32767)
    x[1::2] = np.round(iq.imag * 32767)
    x.tofile(path)


def save_wav(path, iq, rate, seconds, audio_rate=48000):
    """What an AM receiver hears: envelope minus DC, decimated."""
    reps = int(np.ceil(seconds * rate / len(iq)))
    env = np.tile(np.abs(iq), reps)[:int(seconds * rate)]
    env -= env.mean()
    step = int(rate // audio_rate)
    k = np.ones(step) / step                    # boxcar anti-alias, fine for a 5 kHz signal
    au = np.convolve(env, k, mode="same")[::step]
    au = np.int16(au / np.abs(au).max() * 0.8 * 32767)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(rate / step))
        w.writeframes(au.tobytes())


class Keys:
    """Reads keys in a background thread; the TX loop polls .gain and .muted
    between sweeps, so the radio is only ever driven from one thread."""

    def __init__(self, gain, lo, hi):
        self.gain, self.lo, self.hi = gain, lo, hi
        self.muted = self.quit = False
        self.typed = ""
        self.fd = sys.stdin.fileno()
        self.saved = termios.tcgetattr(self.fd)
        tty.setcbreak(self.fd)                  # keeps Ctrl-C working
        threading.Thread(target=self._run, daemon=True).start()

    def restore(self):
        termios.tcsetattr(self.fd, termios.TCSADRAIN, self.saved)

    def _set(self, g):
        self.gain = min(max(g, self.lo), self.hi)

    def _run(self):
        seq = ""
        while not self.quit:
            c = os.read(self.fd, 1).decode(errors="ignore")
            if seq or c == "\x1b":              # arrow / PgUp / PgDn escape sequences
                seq += c
                if len(seq) < 3 or (seq[2].isdigit() and not seq.endswith("~")):
                    continue
                c = {"\x1b[A": "+", "\x1b[B": "-", "\x1b[5~": "]", "\x1b[6~": "["}.get(seq, "")
                seq = ""
            step = {"+": 1, "=": 1, "-": -1, "]": 10, "[": -10}.get(c)
            if step:
                self._set(self.gain + step)
            elif c.isdigit() or (c == "." and self.typed):   # typed values are >= 0; arrows reach below
                self.typed += c
            elif c in "\r\n" and self.typed:
                try:
                    self._set(float(self.typed))
                except ValueError:
                    pass
                self.typed = ""
            elif c in ("\x7f", "\b"):
                self.typed = self.typed[:-1]
            elif c == "m":
                self.muted = not self.muted
            elif c == "q":
                self.quit = True


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--freq", type=float, default=433.65e6, help="RF frequency in Hz (default 433.65e6)")
    p.add_argument("--gain", type=float, default=30, help="TX gain in dB (default 30, range -12..64)")
    p.add_argument("--sweep-rate", type=float, default=2.42, help="sweeps per second (spec 2..4, default 2.42)")
    p.add_argument("--f-hi", type=float, default=1570, help="sweep start in Hz (default 1570)")
    p.add_argument("--f-lo", type=float, default=820, help="sweep end in Hz (default 820)")
    p.add_argument("--law", choices=["period", "linear"], default="period",
                   help="period: tone period grows linearly, like the recording (default); linear: Hz linear in time")
    p.add_argument("--wave", choices=["sine", "square"], default="sine", help="tone shape (default sine)")
    p.add_argument("--duty", type=float, default=0.5, help="square-wave duty cycle (spec 0.33..0.55, default 0.5)")
    p.add_argument("--mod-index", type=float, default=0.9, help="AM index (spec 0.85..1.0, default 0.9)")
    p.add_argument("--seconds", type=float, default=0, help="stop after this long (default 0 = until Ctrl-C)")
    p.add_argument("--save", help="also write the IQ (cs16 @ 1 MS/s, --seconds long, default 10 s)")
    p.add_argument("--wav", help="also write the demodulated audio (--seconds long, default 10 s)")
    p.add_argument("--id", default="N0CALL", help="callsign sent in Morse (default N0CALL, '' = off)")
    p.add_argument("--id-every", type=float, default=300, help="seconds between IDs (default 300)")
    p.add_argument("--wpm", type=float, default=20, help="Morse speed in words/min (default 20)")
    p.add_argument("--no-tx", action="store_true", help="only generate files, no radio")
    a = p.parse_args()

    for name, v, lo, hi in [("sweep-rate", a.sweep_rate, 2, 4), ("duty", a.duty, 0.33, 0.55),
                            ("mod-index", a.mod_index, 0.85, 1.0)]:
        if not lo <= v <= hi:
            print(f"note: {name} {v:g} is outside the DO-183 range {lo:g}..{hi:g}", file=sys.stderr)
    if a.f_hi <= a.f_lo:
        sys.exit("the sweep goes downward: --f-hi must be above --f-lo")

    if a.f_hi - a.f_lo < 700:
        print(f"note: sweep span {a.f_hi - a.f_lo:g} Hz is below the DO-183 minimum of 700 Hz", file=sys.stderr)
    iq = homer_iq(RATE, a.f_hi, a.f_lo, a.sweep_rate, a.duty, a.mod_index, law=a.law, wave=a.wave)
    bad = set(a.id.upper()) - set(MORSE) - {" "}
    if bad:
        sys.exit(f"--id: no Morse for {''.join(sorted(bad))}")
    ident = morse_iq(a.id, RATE, a.wpm, m=a.mod_index) if a.id.strip() else np.zeros(0, np.complex64)
    file_s = a.seconds or 10
    if a.save or a.wav:
        # files start with the ID (if any), then sweeps
        reps = int(np.ceil(file_s * RATE / len(iq)))
        seq = np.concatenate([ident, np.tile(iq, reps)])
    if a.save:
        save_cs16(a.save, seq)
        print(f"{a.save}: ID {len(ident) / RATE:.2f} s + {reps} sweeps, {len(seq) / RATE:.2f} s, cs16 @ {RATE:.0f} S/s")
    if a.wav:
        save_wav(a.wav, seq, RATE, len(seq) / RATE)
        print(f"{a.wav}: {len(seq) / RATE:.2f} s of demodulated audio")
    if a.no_tx:
        return

    from SoapySDR import SOAPY_SDR_TX
    from limetx import LimeTx, check_freq
    check_freq(a.freq)
    check_homer_freq(a.freq)
    tx = LimeTx(a.freq, RATE, a.gain)
    print(tx.describe())
    print(f"homer    : {a.f_hi:g} -> {a.f_lo:g} Hz ({a.law}, {a.wave}), {a.sweep_rate:g} sweeps/s, "
          f"m {a.mod_index:g}, " + (f"{a.seconds:g} s" if a.seconds else "until Ctrl-C"))
    if len(ident):
        print(f"ID       : {a.id.upper()} in MCW, {a.wpm:g} wpm, {len(ident) / RATE:.1f} s, every {a.id_every:g} s")
    rng = tx.sdr.getGainRange(SOAPY_SDR_TX, 0)
    keys = Keys(a.gain, rng.minimum(), rng.maximum()) if sys.stdin.isatty() else None
    if keys:
        print(f"keys     : up/down +/-1 dB, PgUp/PgDn +/-10 dB, number+Enter, m mute, q quit"
              f" (gain {rng.minimum():g}..{rng.maximum():g})")
    silence = np.zeros_like(iq)
    gain, muted, shown = a.gain, False, None
    next_id = 0.0                               # air time (s) of the next ID: first one at start-up
    t0 = time.time()
    try:
        while not a.seconds or tx.n / RATE < a.seconds:
            if keys:
                if keys.quit:
                    break
                if keys.gain != gain:
                    gain = keys.gain
                    tx.sdr.setGain(SOAPY_SDR_TX, 0, gain)
                muted = keys.muted
            send_id = len(ident) and not muted and tx.n / RATE >= next_id
            if keys:
                state = (tx.sdr.getGain(SOAPY_SDR_TX, 0), muted, keys.typed, send_id)
                if state != shown:
                    shown = state
                    typed = f"   set: {keys.typed}_" if keys.typed else ""
                    print(f"\r\x1b[K{time.strftime('%H:%M:%S')}  gain {state[0]:5.1f} dB"
                          + ("  MUTED (no carrier)" if muted else "")
                          + (f"  ID {a.id.upper()}" if send_id else "") + typed, end="", flush=True)
            elif send_id:
                print(f"{time.strftime('%H:%M:%S')}  ID {a.id.upper()}", flush=True)
            if send_id:
                next_id = tx.n / RATE + a.id_every
                tx.play(ident)
            else:
                tx.play(silence if muted else iq)
        tx.play(np.zeros(1000, np.complex64), end_burst=True)
        tx.wait()
        print(f"\ndone: {tx.n / RATE:.1f} s on air")
    except KeyboardInterrupt:
        print(f"\nstopped after {time.time() - t0:.1f} s")
    finally:
        if keys:
            keys.quit = True
            keys.restore()
        tx.close()


if __name__ == "__main__":
    main()
