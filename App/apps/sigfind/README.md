# Sig Finder: direction finding and ELT detection

Sig Finder (the Signal Finder, formerly EPIRB Finder) is an overlay app for
direction finding with a directional antenna on the UV-K1 / UV-K5 v3. It
finds any carrier: a 121.5 / 243 MHz homing beacon, a fox, an interferer. It
also tells you whether the signal is a real distress beacon: the ELT detector
recognises the homer's swept tone and shows `ELT` in the status bar.

Forked from Armel F4HWN's FoxHunt app. Needs the Labs firmware v6.1.0 or later
(app API level 2). 3,276 bytes of the 4 KiB overlay, version 1.8.

| Step | State |
|---|---|
| Direction finding (relative feedback, FOLLOW / SWEEP, auto attenuator) | **Done** (v1.1, as EPIRB Finder) |
| ELT swept-tone detector (`sweep.c`), host-tested (`test/sweep_test.c`) | **Done**: all legal sweeps found within about 2 s, no false detection on noise, steady tones, voice-like tone hops or upward sweeps |
| Detector on the radio with the Flipper Zero files (`test/flipper/`) | **Done** (v1.7) |
| Detector on the radio with LimeSDR IQ files (`test/limesdr/`) | **Done** (v1.7) |
| Detector on a real beacon | To do |

## Using the app

1. Tune the VFO to the signal: **AM** for a 121.5 / 243 MHz homer (or the
   Flipper / LimeSDR test signal on 433.650 MHz), FM or USB for anything else.
2. Launch **Sig Finder** from the app menu.
3. Hold the antenna in front of you and turn slowly. The feedback is
   **relative to the strongest level seen** (the peak), not an absolute level,
   so it keeps working when the signal gets strong close to the source.
4. Watch the status bar: `ELT` appears when the signal carries a beacon's
   swept tone (audio off or listen mode, see below).

The app never retunes the radio: it works on whatever the VFO receives, and
the signal level is measured before demodulation, so direction finding works
on any modulation.

### FOLLOW and SWEEP modes (key 1)

- **FOLLOW** (default): the peak falls back slowly (0.5, 1, 2 or 4 dB/s, key 5),
  so the app always answers "is this the best direction of the last few
  seconds". Good for walking towards the source.
- **SWEEP**: the peak never falls. Press MENU to start a new sweep, turn a full
  360 degrees, then turn back until the feedback says you are on the maximum.
  Good for a careful bearing from a fixed point.

Changing mode resets the peak.

### Audio modes (key 2)

| Mode | Speaker | ELT detection |
|---|---|---|
| **off** | Silent | Yes |
| **beep** | Beeps that speed up and rise in pitch as you approach the peak | No: the audio is muted between beeps |
| **listen** | The demodulated signal, AM or FM as the VFO is set | Yes, above -122 dBm |
| **listen USB** | The USB demodulated signal | Yes, above -122 dBm |

Beep feedback: 20 dB or more under the peak, a low 500 Hz beep every 800 ms;
closer to the peak, higher and faster (up to 2000 Hz every 100 ms); within
1.5 dB of the peak, a fast 2800 Hz "on peak" chirp. Below -122 dBm there are no
beeps, and in listen mode the audio is muted.

listen USB is picked automatically at launch when the VFO is in USB. The
label in the status bar shows `AM`, `FM` or `USB`.

A practical sequence for a beacon: audio **off** until `ELT` confirms it is a
real homer, then **beep** to find the direction.

### Attenuator (keys 3, 4, UP / DOWN)

Six steps of front-end attenuation: `0dB`, `6dB`, `15dB`, `27dB`, `BYP`,
`BYP+`. The tag at the bottom shows `A` (automatic) or `M` (manual) and the
step.

- **Automatic** (default, key 4 toggles it): one step more as soon as the
  signal reaches -60 dBm, before the receiver saturates; one step less below
  -105 dBm (FOLLOW mode only); at most one step per second.
- **Manual**: key 3, UP or DOWN changes the step and turns the automatic mode
  off.

Every step change re-measures the level and shifts the peak and the history by
the measured difference, so the readings stay comparable across steps.

## Keys

Identical on the UV-K5 and the UV-K1.

| Key | Action |
|---|---|
| **1** | Mode FOLLOW / SWEEP (resets the peak) |
| **2** | Audio: off, beep, listen, listen USB |
| **3** | Attenuator step (manual; turns the automatic mode off) |
| **4** | Automatic attenuator on / off |
| **5** | FOLLOW decay rate: 0.5, 1, 2, 4 dB/s |
| **6** | Keep the backlight on / normal backlight timeout |
| **UP / DOWN** | Attenuator step (manual; turns the automatic mode off) |
| **MENU** | Reset the peak; in SWEEP mode, start a new sweep |
| **F** (short press) | The next 2, 3 or 5 cycles backwards (F icon in the status bar) |
| **F** (held 0.5 s) | Keypad lock on / off (lock icon in the status bar) |
| **EXIT** | Quit |

While the keypad is locked, only UP / DOWN and MENU work; hold F to unlock
before quitting. Mode, audio mode, attenuator step, automatic attenuator,
decay rate and backlight setting are saved when you quit.

The backlight hold (key 6) re-arms the backlight once a second, which also
keeps the radio's sleep timer from running out during a long search. It has no
icon: the backlight staying on is the sign.

## Screen

| Area | Content |
|---|---|
| Status bar | `SIG DF`; lock or F icon; beep or speaker icon (audio on); `AM` / `FM` / `USB` (listen modes); `ELT` when a beacon sweep is confirmed; battery |
| Large number (left) | dB under the peak: `0` means you are on the peak |
| Right, line 1 | Current level in dBm |
| Right, line 2 | RX frequency in MHz |
| Closeness bar | Fills up as you approach the peak; the marker near the right end is the 1.5 dB "on peak" zone |
| Sweep line (small text) | `ELT 1560>820Hz 3.1Hz` once a beacon is confirmed: the range and rate of its last sweep. Otherwise `AF 1240Hz A35 S1`: the audio frequency, its amplitude (ADC units) and the detector score, or `AF --` when no tone is heard |
| Graph | The last 18 s, relative to the peak (dotted line): 2 dB per row, 30 dB deep |
| Bottom tags | Mode (`FOL 1dB/s` or `SWEEP`), attenuator (`A 0dB`, `M 15dB`...), peak level (`PK -85`) |

## The ELT detector

A 121.5 / 243 MHz homing signal (an ELT, or the homer of a 406 MHz beacon) is
AM modulated by an audio tone that sweeps **downward** over at least 700 Hz
between 1600 and 300 Hz, 2 to 4 times a second (RTCA DO-183, ICAO Annex 10).
A stuck microphone, an interferer or another carrier on the same frequency
does not, so the detector tells you whether the signal is worth the hunt.

How it works (`sweep.c`):

- During each 50 ms pause of the app's loop, the receiver audio is sampled on
  PA4 at 9.6 kHz, as in the POCSAG and EPIRB 406 apps. PA4 receives the audio
  before the speaker amplifier, so this works with the speaker off.
- In each 25 ms window, the tone's zero crossings give its frequency (20 Hz
  steps). The threshold follows the tone's amplitude, so weak and strong
  signals read the same.
- A sweep shows as windows going down, then a jump back up. A cycle counts as
  valid when its period is 200 to 600 ms, it spans at least 400 Hz, reaches
  below 1050 Hz and goes down in most of its windows.
- Three valid cycles in a row, with the same period (within 100 ms) and the
  same top frequency (within 200 Hz), show `ELT`. It goes away after 0.8 s
  without a valid sweep.

Typical time to `ELT`: 1 to 2 s of clean signal.

Limits:

- No detection in beep mode (the audio is muted between beeps), nor in listen
  mode below -122 dBm (muted too).
- Only downward sweeps are recognised, as the standard specifies.
- The `A` value on the sweep line is the tone amplitude reaching PA4. Below 16
  the detector treats the window as silence.
- The detector only says whether the signal is a homer; other signal types
  (CW fox, CTCSS, data) just show their audio frequency.

## Tests

All in `test/`.

### Host test (`sweep_test.c`)

```
clang -O2 -Wall -Wextra -o /tmp/sweep_test sweep_test.c -lm
/tmp/sweep_test                              # the synthetic cases
/tmp/sweep_test flipper/homer_default.sub    # a Flipper file through the detector
```

It synthesizes what PA4 sees (the swept tone, noise, the PA4 high-pass around
1 kHz measured on the K1, the 12-bit ADC) and samples it the way the app does,
with the screen drawing taking 5, 15 or 30 ms between ticks. Every legal sweep
(2 to 4 Hz, 700 to 1300 Hz wide, weak, 6 dB SNR, with and without the
high-pass) must be found within 4 s; white and low-passed noise, a steady
tone, voice-like tone hops and an upward sweep must never be.

On macOS, `cc` may not be the system compiler in some shells: use
`/usr/bin/clang`.

### Flipper Zero (`flipper_homer.py`, `flipper/`)

The Flipper cannot amplitude-modulate, so it keys the carrier on and off at
the tone rate: AM at 100 % with a rectangular tone, which DO-183 allows. Set
the radio to **AM on 433.650 MHz** and press Send; each file lasts about 20 s.

| File | Sweep | Expected |
|---|---|---|
| `homer_default.sub` | 1570 to 820 Hz, 2.42 sweeps/s (like a real beacon recording) | `ELT` |
| `homer_textbook.sub` | 1600 to 300 Hz, 3 sweeps/s | `ELT` |
| `homer_fast_min.sub` | 1000 to 300 Hz, 4 sweeps/s | `ELT` |
| `homer_slow_min.sub` | 1600 to 900 Hz, 2 sweeps/s | `ELT` |
| `neg_upward.sub` | 300 to 1600 Hz, upward | never `ELT` |
| `neg_tone_1k.sub` | steady 1 kHz | never `ELT` |

`flipper_homer.py [out_dir]` regenerates them; it refuses 121.5, 243 and
406 MHz.

### LimeSDR (`limesdr/homer121.py`)

A live swept-tone AM homer, by default on 433.65 MHz, with keys to change the
TX gain while it runs (walk the app through signal levels) and a Morse ID.
Run it from `test/limesdr/` so it finds `limetx.py`:

```
cd test/limesdr
./homer121.py --id YOURCALL
./homer121.py --law linear --wave square --f-hi 1600 --f-lo 300 --sweep-rate 3
```

It can also write the signal as an IQ file and send it with `limetx.py`; the
detector shows `ELT` on the radio with these files:

```
./homer121.py --save homer.cs16 --seconds 30 --no-tx
./limetx.py homer.cs16 --format cs16 --rate 1e6 --freq 433.65e6 --repeat 3
```

`limetx.py` refuses 121.3-121.7 MHz, 242.8-243.2 MHz and 405.9-406.2 MHz,
for every script that transmits through it.

**Never transmit a test homer on 121.5, 243 or 406 MHz**: these are
monitored distress frequencies, and a test signal there is a false alert.

## Files

| File | Content |
|---|---|
| `sigfind_app.c` | The app: radio, feedback, drawing, keys, PA4 sampling |
| `sweep.c` | The ELT swept-tone detector (also built by the host test) |
| `gen_assets.py` | Read-only assets: texts, attenuator ladder, icons |
| `build.sh` | Build (via `../../../compile-app.sh sigfind`), `APP_VER` |
| `test/` | Host test, Flipper files, LimeSDR transmitter |
