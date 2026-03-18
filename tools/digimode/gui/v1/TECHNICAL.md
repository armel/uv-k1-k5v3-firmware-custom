# v1 Audio Bridge — Technical Reference

This document describes the signal processing pipeline, debouncing strategy,
and timing design used by the v1 audio bridge GUI.

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Audio Capture Layer](#audio-capture-layer)
- [Frequency Estimation Pipeline](#frequency-estimation-pipeline)
  - [Stage 1: Windowed FFT](#stage-1-windowed-fft)
  - [Stage 2: Parabolic Peak Interpolation](#stage-2-parabolic-peak-interpolation)
  - [Stage 3: Phase Vocoder Refinement](#stage-3-phase-vocoder-refinement)
  - [Accuracy Summary](#accuracy-summary)
- [Frequency Stabilization and Debouncing](#frequency-stabilization-and-debouncing)
  - [Sliding Window History](#sliding-window-history)
  - [Frequency Clustering](#frequency-clustering)
  - [Stability Gate](#stability-gate)
  - [Change Candidate Confirmation](#change-candidate-confirmation)
  - [Minimum Send Interval](#minimum-send-interval)
- [Adaptive Symbol Timing Estimation](#adaptive-symbol-timing-estimation)
- [Software VOX](#software-vox)
- [UART Frequency Scheduling](#uart-frequency-scheduling)
- [Threading Model](#threading-model)
- [Timing Parameter Reference](#timing-parameter-reference)
- [Mode Presets](#mode-presets)
- [NumPy 2 Compatibility Patch](#numpy-2-compatibility-patch)

---

## Architecture Overview

The v1 bridge captures audio from a Windows speaker loopback device, detects
the dominant tone frequency in real time, and streams frequency updates to the
radio over UART. The radio synthesizes the corresponding RF tone on the
configured dial frequency.

```
┌──────────────┐    WASAPI     ┌────────────┐   FFT + Phase   ┌─────────────┐
│ Digital Mode │──loopback────>│   Audio    │───Vocoder──────>│  Frequency  │
│   Software   │   capture     │  Monitor   │   estimation    │ Stabilizer  │
└──────────────┘               └────────────┘                 └──────┬──────┘
                                                                     │
                                                              debounced freq
                                                                     │
                                                                     v
┌──────────────┐    38400 bps  ┌────────────┐  SET_FREQ cmd   ┌─────────────┐
│    Radio     │<──UART───────│   Bridge   │<────────────────│   Decision  │
│  (BK4819)   │               │    Loop    │                  │    Engine   │
└──────────────┘               └────────────┘                 └─────────────┘
```

The pipeline runs at sub-symbol cadence: for FT8 (160 ms symbols), the
analysis window is 40 ms and the bridge loop polls every 10 ms, producing a
new frequency estimate roughly every 10 ms.

---

## Audio Capture Layer

Two audio backends are supported, selected automatically at startup:

| Priority | Backend       | Module        | Capture Method                  |
|----------|---------------|---------------|---------------------------------|
| 1        | `soundcard`   | `soundcard`   | WASAPI loopback via COM/CFFI    |
| 2        | `sounddevice` | `sounddevice` | PortAudio WASAPI shared session |

The `soundcard` backend is preferred because it provides direct WASAPI
loopback capture without PortAudio overhead. If `soundcard` is not available
or fails to enumerate devices, the bridge falls back to `sounddevice` with
`WasapiSettings(loopback=True)`.

Both backends feed mono float32 audio at 48 kHz into the `_AudioMonitor`
class. Audio arrives in small chunks (hop size, typically 480 samples = 10 ms)
and is accumulated in a ring buffer until one full analysis block is available.

### Ring Buffer Accumulation

```
_ingest_audio(chunk):
    buffer = concat(buffer, chunk)
    if len(buffer) > block_size:
        buffer = buffer[-block_size:]     # keep only the latest block_size samples
    if len(buffer) == block_size:
        _process_audio(buffer)            # run FFT on the full block
```

This design ensures that every analysis runs on exactly `block_size` samples,
even if the audio backend delivers chunks of varying length. The overlap
between consecutive analyses is `block_size - hop_size`, which provides
temporal smoothing without extra latency.

---

## Frequency Estimation Pipeline

Each analysis block passes through three stages to produce a single frequency
estimate with sub-bin accuracy.

### Stage 1: Windowed FFT

The raw audio block is multiplied by a Hanning window to reduce spectral
leakage, then transformed with `numpy.fft.rfft`:

```
windowed = audio * hanning_window
fft_bins = rfft(windowed)
magnitudes = abs(fft_bins)
phases = angle(fft_bins)
```

The Hanning window suppresses sidelobe artifacts that would otherwise produce
false peaks when a tone frequency falls between two FFT bins.

The dominant bin is found by `argmax` over `magnitudes[1:]` (excluding the DC
bin at index 0). At 48 kHz sample rate with a 1920-sample block (40 ms), the
coarse FFT bin spacing is:

```
bin_spacing = 48000 / 1920 = 25.0 Hz
```

This coarse resolution is insufficient for digital modes (FT8 tone step is
6.25 Hz), so the following refinement stages are applied.

### Stage 2: Parabolic Peak Interpolation

Given the peak bin `k` and its two neighbors, a parabola is fitted to the
three magnitude values to estimate the true peak location:

```
y0 = magnitudes[k - 1]
y1 = magnitudes[k]          # the peak bin
y2 = magnitudes[k + 1]

delta = 0.5 * (y0 - y2) / (y0 - 2*y1 + y2)
refined_bin = k + delta
coarse_hz = refined_bin * sample_rate / block_size
```

This step typically reduces the frequency error from ±12.5 Hz (half a bin) to
roughly ±2–4 Hz, depending on the signal-to-noise ratio.

### Stage 3: Phase Vocoder Refinement

When phase history from the previous analysis block is available, the
instantaneous frequency is computed from the phase difference between
consecutive blocks at the peak bin:

```
expected_phase_advance = 2π * peak_bin * hop_size / block_size
actual_phase_advance   = phase[peak_bin] - prev_phase[peak_bin]
deviation              = wrap_to_±π(actual_phase_advance - expected_phase_advance)
instantaneous_bin      = peak_bin + deviation * block_size / (2π * hop_size)
instantaneous_hz       = instantaneous_bin * sample_rate / block_size
```

The phase difference directly encodes the sub-bin frequency offset. Because
the hop size (10 ms) is shorter than the analysis window (40 ms), the phase
measurement has high temporal resolution while the frequency measurement
retains the spectral resolution of the longer window.

**Safety guards:**

- If the peak bin jumped by more than ±1 bin since the last frame, the phase
  vocoder is bypassed and the parabolic estimate is used instead. This
  prevents the phase unwrapping from producing erroneous results during rapid
  frequency transitions.
- If the instantaneous frequency is negative or deviates from the parabolic
  estimate by more than one full bin width, the parabolic estimate is used.
- Phase history is cleared when audio drops below the VOX threshold.

### Accuracy Summary

| Stage              | Typical Error (48 kHz, 40 ms window) |
|--------------------|--------------------------------------|
| Raw FFT bin        | ±12.5 Hz                             |
| Parabolic interp.  | ±2–4 Hz                              |
| Phase vocoder      | ±0.1–0.5 Hz                          |

The phase vocoder accuracy is sufficient to distinguish individual FT8 tones
(6.25 Hz spacing) within a single 40 ms analysis window.

---

## Frequency Stabilization and Debouncing

The raw per-frame frequency estimates are noisy. The stabilization pipeline
suppresses jitter and only emits a UART frequency update when the detected
tone has genuinely changed. This pipeline operates in the bridge loop thread.

### Sliding Window History

Every frame where audio is present (`rms > VOX_THRESHOLD` and `peak_hz > 0`),
the `(timestamp, frequency)` pair is appended to a deque:

```
_append_bridge_freq(timestamp, peak_hz)
```

Entries older than `symbol_ms` (e.g. 160 ms for FT8) are continuously pruned
from the front. This window contains the most recent one-symbol-duration of
frequency observations.

### Frequency Clustering

The sliding window entries are clustered to find the dominant frequency. Two
clustering strategies are used depending on whether a tone step is configured:

**With known tone step (e.g. FT8 = 6.25 Hz):**

Each observation is snapped to the nearest tone grid point. The grid point
with the highest count (tie-broken by most recent timestamp) is selected as
the dominant frequency. This approach is noise-immune because all observations
within ±3.125 Hz of the true tone collapse to the same grid point.

**Without known tone step (Auto mode, tone_step = 0):**

A greedy nearest-neighbor clustering algorithm groups observations within
`cluster_tol_hz` of each other. The cluster center is maintained as the
running median of its members. The cluster with the most members (tie-broken
by most recent timestamp) is selected. The tolerance is:

```
cluster_tol_hz = max(4.0 Hz, fft_bin_spacing * 0.15)
```

### Stability Gate

A frequency is considered "stable" only when all of the following hold:

1. At least `BRIDGE_STABLE_FRAMES` (3) observations exist in the sliding
   window.
2. The dominant cluster from the most recent `decision_ms` window
   (= `symbol_ms * 0.50`) contains at least `BRIDGE_STABLE_FRAMES` members.
3. The most recent observation, after quantization, falls within tolerance of
   the dominant cluster center.

If any condition fails, `_stable_bridge_freq()` returns `None` and no
frequency update is considered for this frame.

### Change Candidate Confirmation

When a stable frequency differs from the last frequency sent over UART, it is
not sent immediately. Instead, it enters a two-stage confirmation process:

```
1. The new frequency becomes the "candidate" and a timestamp is recorded.

2. If the stable frequency changes away from the candidate before
   confirmation, the candidate is replaced (restarting the timer).

3. The candidate is confirmed (and sent) only after it has persisted for at
   least change_confirm_ms = symbol_ms * 0.20.

   For FT8: 160 * 0.20 = 32 ms confirmation hold.
```

This prevents momentary FFT artifacts or inter-symbol transients from
triggering a spurious frequency update.

### Minimum Send Interval

Even after confirmation, a frequency update is suppressed if the time since
the last UART send is less than `min_send_ms`:

```
min_send_ms = max(change_confirm_ms, symbol_ms * 0.75)
```

For FT8: `max(32, 120) = 120 ms`.

This enforces a rate limit that prevents flooding the UART during rapid
frequency sweeps, and naturally aligns updates with symbol boundaries.

### Debouncing Pipeline Summary (FT8 example)

```
Audio frame (every ~10 ms)
    │
    ├─ RMS < threshold? ──> reset everything, no update
    │
    ├─ Phase vocoder estimate ──> ±0.5 Hz accuracy
    │
    ├─ Append to sliding window (160 ms depth)
    │
    ├─ Cluster within decision window (80 ms)
    │       └─ Snap to 6.25 Hz grid
    │       └─ Require ≥3 consistent observations
    │
    ├─ Stable? ──> No: wait for more frames
    │
    ├─ Same as last sent? ──> Yes: do nothing
    │
    ├─ New candidate: held for ≥32 ms confirmation
    │
    └─ Confirmed + min 120 ms since last send ──> UART SET_FREQ
```

---

## Adaptive Symbol Timing Estimation

When the mode preset is set to "Auto", the bridge does not know the symbol
duration in advance. It estimates it by observing frequency transitions:

1. **Track the current stable frequency** and the timestamp when it first
   appeared (`observed_symbol_hz`, `observed_symbol_since`).

2. **When the stable frequency changes**, compute the duration the previous
   frequency was held:
   ```
   duration_ms = (now - observed_symbol_since) * 1000
   ```

3. **If the duration exceeds the minimum** (analysis window length), feed it
   into an exponential moving average:
   ```
   symbol_est = (1 - alpha) * symbol_est + alpha * duration_ms
   alpha = 0.35
   ```

4. **All timing parameters** (analysis window, decision window, confirmation
   hold, minimum send interval) are dynamically recomputed from the updated
   symbol estimate.

This allows the bridge to adapt to any FSK-based digital mode without manual
configuration. The EMA smoothing prevents a single anomalous transition from
destabilizing the timing.

---

## Software VOX

The bridge includes a software voice-operated exchange (VOX) that
automatically starts and stops radio transmission based on audio presence:

| Parameter                  | Value | Purpose                                  |
|----------------------------|-------|------------------------------------------|
| `VOX_THRESHOLD`            | 0.005 | RMS amplitude below which audio is "silent" |
| `VOX_HANG_S`               | 50 ms | Keep TX active briefly after audio drops  |
| `BRIDGE_ACTIVATE_FRAMES`   | 2     | Consecutive active frames required to start TX |

**TX activation:**

TX is started only after `BRIDGE_ACTIVATE_FRAMES` (2) consecutive bridge loop
iterations where `rms > VOX_THRESHOLD` and `peak_hz > 0`. This prevents a
single noise spike from triggering transmission.

**TX deactivation:**

TX is stopped when the time since the last frame with audio exceeds
`VOX_HANG_S` (50 ms). The short hang time bridges momentary gaps between
symbols (e.g. during inter-symbol amplitude dips) without holding TX
unnecessarily long after the source software stops.

**State reset on silence:**

When audio drops below threshold, all stabilization state is cleared:
- Sliding window history is flushed.
- The change candidate is discarded.
- Phase vocoder history is cleared.
- Symbol timing observation is reset.

This ensures that when audio resumes, the pipeline starts fresh without stale
frequency data influencing the first estimate.

---

## UART Frequency Scheduling

When the decision engine commits to a frequency update, it sends a `SET_FREQ`
command to the radio:

```
freq_dhz = round(audio_hz * 10)            # frequency in 0.1 Hz units
apply_at = clock.to_radio(clock.now() + 15000)  # 15 ms scheduling lead

payload = freq_dhz (2B, big-endian) × 5    # 5 redundant copies
        + apply_at (4B, big-endian)         # scheduled application time

frame = [0xAB, 0x03, len, payload..., CRC]
```

**Redundancy:** The frequency value is repeated 5 times in the payload. The
firmware uses majority voting across the copies to reject bit errors on the
UART link.

**Scheduling lead:** The `apply_at` timestamp is set 15 ms into the future
(in the radio's clock domain). This compensates for UART transmission latency
and firmware processing time, so the radio applies the new frequency closer to
the intended symbol boundary.

**Clock synchronization:** An NTP-like protocol synchronizes the PC clock with
the radio's microsecond timer at connection time. The median offset from 5
round-trip measurements is used to translate PC timestamps to radio timestamps.

---

## Threading Model

```
┌─────────────────────────────┐
│       Tkinter Main Thread   │  UI updates, user interaction
│                             │  _bridge_update_ui() via root.after()
└─────────────┬───────────────┘
              │ root.after(0, ...)
              │
┌─────────────┴───────────────┐
│      Bridge Loop Thread     │  _bridge_loop()
│                             │  Polls AudioMonitor.snapshot()
│                             │  Runs stabilization + decision engine
│                             │  Writes UART (SET_FREQ, NOOP)
│                             │  Sleeps 10 ms between iterations
└─────────────┬───────────────┘
              │ reads snapshot
              │
┌─────────────┴───────────────┐
│   Audio Capture Thread(s)   │  _AudioMonitor
│                             │  soundcard: _soundcard_loop() in thread
│                             │  sounddevice: callback in PortAudio thread
│                             │  Runs FFT, writes snapshot under lock
└─────────────────────────────┘
```

**Thread safety:** The `_AudioMonitor` exposes a single `snapshot()` method
that returns `(seq, timestamp, rms, peak_hz)` under a lock. The bridge loop
reads this snapshot; it never accesses raw audio buffers or FFT state directly.
The lock is held only for the duration of copying four scalar values, so
contention is negligible.

**UI updates:** The bridge loop posts UI updates via `root.after(0, callback)`
to the Tkinter main thread. This avoids cross-thread Tkinter access, which is
not thread-safe on all platforms.

---

## Timing Parameter Reference

All timing parameters are derived from the symbol duration. The following
table shows concrete values for common modes:

| Parameter            | Formula                      | FT8 (160 ms) | FT4 (48 ms) | JT65 (372 ms) | WSPR (683 ms) |
|----------------------|------------------------------|---------------|--------------|----------------|---------------|
| Analysis window      | symbol × 0.25, min 10 ms     | 40 ms         | 12 ms        | 93 ms          | 170 ms        |
| Analysis block size  | window × 48000 / 1000        | 1920 samples  | 576 samples  | 4464 samples   | 8160 samples  |
| FFT bin spacing      | 48000 / block_size            | 25.0 Hz       | 83.3 Hz      | 10.8 Hz        | 5.9 Hz        |
| Decision window      | symbol × 0.50, min 20 ms     | 80 ms         | 24 ms        | 186 ms         | 341 ms        |
| Change confirm hold  | symbol × 0.20, min 10 ms     | 32 ms         | 10 ms        | 74 ms          | 136 ms        |
| Minimum send interval| max(confirm, symbol × 0.75)  | 120 ms        | 36 ms        | 279 ms         | 512 ms        |
| History depth        | symbol duration               | 160 ms        | 48 ms        | 372 ms         | 683 ms        |
| Hop size             | 10 ms (480 samples)           | 480           | 480          | 480            | 480           |
| Bridge poll interval | 10 ms fixed                   | 10 ms         | 10 ms        | 10 ms          | 10 ms         |

---

## Mode Presets

The following presets configure the symbol duration and tone step for each
supported digital mode:

| Preset   | Symbol (ms) | Tone Step (Hz) | Notes                           |
|----------|-------------|----------------|---------------------------------|
| Auto     | adaptive    | 0 (free)       | Symbol estimated from transitions |
| FST4     | 324         | 3.09           |                                 |
| FT4      | 48          | 20.8333        | Very fast symbols               |
| FT8      | 160         | 6.25           | Most common mode                |
| JT4      | 229         | 4.375          |                                 |
| JT9      | 576         | 1.736          |                                 |
| JT65     | 372         | 2.692          |                                 |
| Q65      | 600         | 1.667          |                                 |
| MSK144   | 72          | 0 (free)       | Continuous phase, no tone grid  |
| FST4W    | 685         | 1.46           |                                 |
| WSPR     | 683         | 1.465          |                                 |
| Echo     | 1000        | 0 (free)       | Generic hold for echo testing   |
| FreqCal  | 1000        | 0 (free)       | Generic hold for calibration    |

When a tone step is configured (> 0), frequency observations are snapped to
the grid before clustering, which dramatically improves noise rejection for
modes with known tone spacing.

---

## NumPy 2 Compatibility Patch

The `soundcard` library version 0.4.5 internally calls
`numpy.fromstring(binary_data)`, which was deprecated in NumPy 1.x and
removed in NumPy 2.0. The bridge includes a runtime monkey-patch that
replaces the internal `_record_chunk` method of
`soundcard.mediafoundation._Recorder` with a version using
`numpy.frombuffer(...).copy()`.

The patch is applied once at import time and is guarded by a flag attribute
(`_digmode_numpy2_patch`) to prevent double-patching. It is a no-op if
`soundcard` is not installed or if the internal API has changed.
