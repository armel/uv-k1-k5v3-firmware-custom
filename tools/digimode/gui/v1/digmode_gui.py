#!/usr/bin/env python3
"""
UV-K1/K5V3 Digital Mode Control — Windows GUI

Features:
    - Manual carrier TX (TURN button) with 30s cap
    - Bridge Mode: speaker loopback → FFT peak detection → UART SET_FREQ
    - Software VOX: auto TX on audio, auto STOP when silent
    - Heartbeat watchdog (NOOP every 80ms)

Requires: pyserial, numpy
Optional audio backends: soundcard (preferred on Windows), sounddevice
Target: Python 3.10+, Windows 10/11
"""

import inspect
import math
import statistics
import struct
import threading
import time
import tkinter as tk
from collections import deque
from tkinter import ttk, messagebox

import numpy as np
import serial
import serial.tools.list_ports

try:
    import sounddevice as sd
    _HAS_SD = True
except Exception:
    _HAS_SD = False

try:
    import soundcard as sc
    import soundcard.mediafoundation as sc_mf
    _HAS_SC = True
except Exception:
    _HAS_SC = False


def _patch_soundcard_numpy_compat():
    """soundcard 0.4.5 uses numpy.fromstring(binary), which breaks on NumPy 2."""
    if not _HAS_SC:
        return
    record_chunk = getattr(sc_mf._Recorder, "_record_chunk", None)
    if record_chunk is None:
        return
    if getattr(record_chunk, "_digmode_numpy2_patch", False):
        return

    def _record_chunk_numpy2(self):
        while self._capture_available_frames() == 0:
            if self._idle_start_time is None:
                self._idle_start_time = time.perf_counter_ns()

            default_block_length, minimum_block_length = self.deviceperiod
            time.sleep(minimum_block_length / 4)
            elapsed_time_ns = time.perf_counter_ns() - self._idle_start_time
            if elapsed_time_ns / 1_000_000_000 > default_block_length * 4:
                num_frames = int(self.samplerate * elapsed_time_ns / 1_000_000_000)
                num_channels = len(set(self.channelmap))
                self._idle_start_time += elapsed_time_ns
                return np.zeros([num_frames * num_channels], dtype="float32")

        self._idle_start_time = None
        data_ptr, nframes, flags = self._capture_buffer()
        if data_ptr == sc_mf._ffi.NULL:
            raise RuntimeError("Could not create capture buffer")

        nbytes = nframes * 4 * len(set(self.channelmap))
        chunk = np.frombuffer(sc_mf._ffi.buffer(data_ptr, nbytes), dtype="float32").copy()

        if flags & sc_mf._ole32.AUDCLNT_BUFFERFLAGS_SILENT:
            chunk[:] = 0
        if self._is_first_frame:
            flags &= ~sc_mf._ole32.AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY
            self._is_first_frame = False
        if flags & sc_mf._ole32.AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY:
            import warnings
            warnings.warn("data discontinuity in recording", sc_mf.SoundcardRuntimeWarning)
        if nframes > 0:
            self._capture_release(nframes)
            return chunk
        return np.zeros([0], dtype="float32")

    _record_chunk_numpy2._digmode_numpy2_patch = True
    sc_mf._Recorder._record_chunk = _record_chunk_numpy2


_patch_soundcard_numpy_compat()

# ---------------------------------------------------------------------------
#  UART protocol (mirrored from firmware digmode.h)
# ---------------------------------------------------------------------------

SYNC          = 0xAB
CMD_START_TX  = 0x01
CMD_STOP_TX   = 0x02
CMD_SET_FREQ  = 0x03
CMD_ACK       = 0x05
CMD_SYNC_REQ  = 0x06
CMD_SYNC_RESP = 0x07
CMD_NOOP      = 0x08

POWER_LABELS  = ["LOW1", "LOW2", "LOW3", "LOW4", "LOW5", "MID", "HIGH"]

HEARTBEAT_S   = 0.08            # 80 ms between NOOPs
MAX_TX_S      = 30              # hard TX cap (TURN mode only)

# Audio monitor constants
SAMPLE_RATE   = 48000
ANALYSIS_HOP_MS = 10
ANALYSIS_HOP_SIZE = SAMPLE_RATE * ANALYSIS_HOP_MS // 1000
ANALYSIS_WINDOW_RATIO = 0.25
ANALYSIS_WINDOW_MIN_MS = 10
VOX_THRESHOLD = 0.005           # RMS threshold for VOX
VOX_HANG_S    = 0.05            # keep TX briefly between short tone gaps
BRIDGE_POLL_MS = 10             # UI/control loop cadence while bridge is active
FREQ_APPLY_AHEAD_US = 15_000    # small scheduling lead to keep radio updates responsive
FREQ_COPIES   = 5               # redundancy in SET_FREQ payload
BRIDGE_ACTIVATE_FRAMES = 2      # require consecutive active frames before TX starts
BRIDGE_SYMBOL_MS_DEFAULT = 160
BRIDGE_GENERIC_HOLD_MS = 1000
BRIDGE_STABLE_FRAMES = 3
BRIDGE_STABLE_TOL_HZ = 4.0
BRIDGE_DECISION_RATIO = 0.50
BRIDGE_CHANGE_CONFIRM_RATIO = 0.20
BRIDGE_MIN_SEND_RATIO = 0.75
BRIDGE_TONE_STEP_HZ_DEFAULT = 0.0
BRIDGE_SYMBOL_EST_SMOOTHING = 0.35
BRIDGE_TRACE_ENABLED = False

BRIDGE_MODE_PRESETS = {
    "Auto": {"symbol_ms": None, "tone_step_hz": 0.0},
    "FST4": {"symbol_ms": 324, "tone_step_hz": 3.09},
    "FT4": {"symbol_ms": 48, "tone_step_hz": 20.8333},
    "FT8": {"symbol_ms": 160, "tone_step_hz": 6.25},
    "JT4": {"symbol_ms": 229, "tone_step_hz": 4.375},
    "JT9": {"symbol_ms": 576, "tone_step_hz": 1.736},
    "JT65": {"symbol_ms": 372, "tone_step_hz": 2.692},
    "Q65": {"symbol_ms": 600, "tone_step_hz": 1.667},
    "MSK144": {"symbol_ms": 72, "tone_step_hz": 0.0},
    "FST4W": {"symbol_ms": 685, "tone_step_hz": 1.46},
    "WSPR": {"symbol_ms": 683, "tone_step_hz": 1.465},
    "Echo": {"symbol_ms": BRIDGE_GENERIC_HOLD_MS, "tone_step_hz": 0.0},
    "FreqCal": {"symbol_ms": BRIDGE_GENERIC_HOLD_MS, "tone_step_hz": 0.0},
}


def _crc(data: bytes) -> int:
    c = 0
    for b in data:
        c ^= b
    return c


def _frame(cmd: int, payload: bytes = b"") -> bytes:
    hdr = bytes([SYNC, cmd, len(payload)])
    body = hdr + payload
    return body + bytes([_crc(body)])


NOOP_FRAME = _frame(CMD_NOOP)


def _parse(data: bytes):
    """Return (cmd, payload, rest) or None."""
    while len(data) >= 4:
        if data[0] != SYNC:
            idx = data.find(bytes([SYNC]), 1)
            if idx < 0:
                return None
            data = data[idx:]
            continue
        length = data[2]
        fs = 3 + length + 1
        if len(data) < fs:
            return None
        if _crc(data[: fs - 1]) != data[fs - 1]:
            data = data[1:]
            continue
        return data[1], data[3 : 3 + length], data[fs:]
    return None


# ---------------------------------------------------------------------------
#  NTP-like clock sync
# ---------------------------------------------------------------------------

class _Clock:
    def __init__(self):
        self.offset = 0
        self.rtt = 0
        self._t0 = time.monotonic()

    def now(self) -> int:
        return int((time.monotonic() - self._t0) * 1_000_000) & 0xFFFFFFFF

    def to_radio(self, pc_us: int) -> int:
        return (pc_us + self.offset) & 0xFFFFFFFF

    def sync(self, ser: serial.Serial, rounds: int = 5) -> bool:
        offsets, rtts = [], []
        for _ in range(rounds):
            t1 = self.now()
            ser.write(_frame(CMD_SYNC_REQ, struct.pack(">I", t1)))
            buf = b""
            deadline = time.monotonic() + 0.5
            result = None
            while time.monotonic() < deadline:
                chunk = ser.read(32)
                if chunk:
                    buf += chunk
                    result = _parse(buf)
                    if result:
                        break
            if not result:
                continue
            t3 = self.now()
            cmd, payload, _ = result
            if cmd != CMD_SYNC_RESP or len(payload) < 8:
                continue
            echo = struct.unpack(">I", payload[:4])[0]
            t2 = struct.unpack(">I", payload[4:8])[0]
            if echo != t1:
                continue
            rtt = (t3 - t1) & 0xFFFFFFFF
            off = (t2 - t1 - rtt // 2) & 0xFFFFFFFF
            if off > 0x7FFFFFFF:
                off -= 0x100000000
            offsets.append(off)
            rtts.append(rtt)
            time.sleep(0.03)
        if len(offsets) < 2:
            return False
        self.offset = int(statistics.median(offsets))
        self.rtt = int(statistics.median(rtts))
        return True


# ---------------------------------------------------------------------------
#  Audio monitor — speaker loopback + FFT peak detection
# ---------------------------------------------------------------------------

class _AudioMonitor:
    """Captures speaker audio, runs FFT, and exposes peak frequency + RMS."""

    def __init__(self, device_info: dict, block_size: int):
        self._dev = device_info
        self._block_size = block_size
        self._hop_size = min(ANALYSIS_HOP_SIZE, block_size)
        self._stream = None
        self._recorder = None
        self._thread: threading.Thread | None = None
        self._stop_evt = threading.Event()
        self._lock = threading.Lock()
        self._rms: float = 0.0
        self._peak_hz: float = 0.0
        self._window_seq: int = 0
        self._window_ts: float = 0.0
        self._last_error: str | None = None
        self._hanning = np.hanning(self._block_size).astype(np.float32)
        self._prev_phase: np.ndarray | None = None
        self._prev_peak_bin: int | None = None
        self._buffer = np.zeros(0, dtype=np.float32)

    def start(self):
        backend = self._dev["backend"]
        if backend == "soundcard":
            self._start_soundcard()
            return
        if backend == "sounddevice":
            self._start_sounddevice()
            return
        raise RuntimeError(f"Unsupported audio backend: {backend}")

    def _start_soundcard(self):
        speaker = self._dev["device"]
        loopback = sc.get_microphone(speaker.id, include_loopback=True)
        self._recorder = loopback.recorder(
            samplerate=SAMPLE_RATE,
            channels=1,
            blocksize=self._hop_size,
        )
        self._recorder.__enter__()
        self._stop_evt.clear()
        self._thread = threading.Thread(target=self._soundcard_loop, daemon=True)
        self._thread.start()

    def _start_sounddevice(self):
        extra = None
        try:
            params = inspect.signature(sd.WasapiSettings).parameters
            if "loopback" in params:
                extra = sd.WasapiSettings(loopback=True)
            else:
                extra = sd.WasapiSettings()
        except Exception:
            extra = None
        self._stream = sd.InputStream(
            device=self._dev["device"],
            samplerate=SAMPLE_RATE,
            blocksize=self._hop_size,
            channels=1,
            dtype="float32",
            extra_settings=extra,
            callback=self._cb,
        )
        self._stream.start()

    def _soundcard_loop(self):
        while not self._stop_evt.is_set():
            try:
                chunk = self._recorder.record(numframes=self._hop_size)
            except Exception as exc:
                self._last_error = str(exc)
                break

            if chunk is None:
                continue

            audio = np.asarray(chunk, dtype=np.float32)
            if audio.ndim == 2:
                audio = audio[:, 0]
            self._ingest_audio(audio)

    def stop(self):
        self._stop_evt.set()
        if self._stream is not None:
            try:
                self._stream.stop()
                self._stream.close()
            except Exception:
                pass
            self._stream = None
        if self._recorder is not None:
            try:
                self._recorder.__exit__(None, None, None)
            except Exception:
                pass
            self._recorder = None
        if self._thread is not None:
            self._thread.join(timeout=1.0)
            self._thread = None

    @property
    def running(self) -> bool:
        return self._stream is not None or self._thread is not None

    @property
    def last_error(self) -> str | None:
        return self._last_error

    def snapshot(self) -> tuple[int, float, float, float]:
        """Return (window_seq, window_ts, rms, peak_hz). Thread-safe."""
        with self._lock:
            return self._window_seq, self._window_ts, self._rms, self._peak_hz

    def _cb(self, indata, _frames, _time_info, _status):
        self._ingest_audio(indata[:, 0])

    def _ingest_audio(self, audio):
        if audio.size == 0:
            return
        self._buffer = np.concatenate((self._buffer, np.asarray(audio, dtype=np.float32)))
        if self._buffer.size > self._block_size:
            self._buffer = self._buffer[-self._block_size :]
        if self._buffer.size == self._block_size:
            self._process_audio(self._buffer)

    def _process_audio(self, audio):
        rms = float(np.sqrt(np.mean(audio * audio)))
        peak = 0.0
        window_ts = time.monotonic()

        if rms > VOX_THRESHOLD:
            windowed = audio * self._hanning
            fft_bins = np.fft.rfft(windowed)
            mags = np.abs(fft_bins)
            phase = np.angle(fft_bins)

            if len(mags) > 2:
                search = mags[1:]
                pk = int(np.argmax(search)) + 1
                peak = self._estimate_peak_hz(pk, mags, phase)
            self._prev_phase = phase
            self._prev_peak_bin = pk if len(mags) > 2 else None
        else:
            self._prev_phase = None
            self._prev_peak_bin = None

        with self._lock:
            self._window_seq += 1
            self._window_ts = window_ts
            self._rms = rms
            self._peak_hz = peak

    def _estimate_peak_hz(self, peak_bin: int, mags: np.ndarray, phase: np.ndarray) -> float:
        coarse_hz = self._parabolic_peak_hz(peak_bin, mags)

        if self._prev_phase is None or peak_bin >= len(self._prev_phase):
            return coarse_hz

        # If the dominant bin moved abruptly, use the coarse estimate for this frame
        # and let phase tracking lock again on the next block.
        if self._prev_peak_bin is not None and abs(peak_bin - self._prev_peak_bin) > 1:
            return coarse_hz

        expected = 2.0 * math.pi * peak_bin * self._hop_size / self._block_size
        delta = phase[peak_bin] - self._prev_phase[peak_bin] - expected
        delta = (delta + math.pi) % (2.0 * math.pi) - math.pi
        inst_bin = peak_bin + delta * self._block_size / (2.0 * math.pi * self._hop_size)
        inst_hz = inst_bin * SAMPLE_RATE / self._block_size

        if inst_hz <= 0:
            return coarse_hz

        coarse_step_hz = SAMPLE_RATE / self._block_size
        if abs(inst_hz - coarse_hz) > coarse_step_hz:
            return coarse_hz
        return inst_hz

    def _parabolic_peak_hz(self, peak_bin: int, mags: np.ndarray) -> float:
        if 1 <= peak_bin < len(mags) - 1:
            y0 = float(mags[peak_bin - 1])
            y1 = float(mags[peak_bin])
            y2 = float(mags[peak_bin + 1])
            denom = y0 - 2.0 * y1 + y2
            if abs(denom) > 1e-12:
                delta = 0.5 * (y0 - y2) / denom
            else:
                delta = 0.0
            return (peak_bin + delta) * SAMPLE_RATE / self._block_size
        return peak_bin * SAMPLE_RATE / self._block_size


# ---------------------------------------------------------------------------
#  Application
# ---------------------------------------------------------------------------

class App:

    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("Digmode Control")
        root.resizable(False, False)

        self._ser: serial.Serial | None = None
        self._clock: _Clock | None = None

        # Manual TX state
        self._tx_active = False
        self._stop_evt = threading.Event()
        self._tx_t0 = 0.0

        # Bridge state
        self._bridge_on = False
        self._audio_mon: _AudioMonitor | None = None
        self._bridge_thread: threading.Thread | None = None
        self._bridge_stop_evt = threading.Event()
        self._vox_last_audio = 0.0
        self._bridge_tx = False
        self._bridge_active_streak = 0
        self._bridge_last_freq_dhz: int | None = None
        self._bridge_last_send_ts: float | None = None
        self._bridge_freq_history = deque()
        self._bridge_candidate_hz: float | None = None
        self._bridge_candidate_since: float | None = None
        self._bridge_symbol_est_ms: float | None = None
        self._bridge_observed_symbol_hz: float | None = None
        self._bridge_observed_symbol_since: float | None = None
        self._tone_step_var = tk.DoubleVar(value=BRIDGE_TONE_STEP_HZ_DEFAULT)
        self._symbol_preset_var = tk.StringVar(value="Auto")

        self._build_ui()
        self._refresh_ports()
        self._refresh_speakers()

    # ================================================================
    #  UI construction
    # ================================================================

    def _build_ui(self):
        pad = dict(padx=6, pady=4)
        fr = ttk.Frame(self.root, padding=12)
        fr.grid(sticky="nsew")
        fr.columnconfigure(4, weight=1)
        r = 0

        # ---- COM port ----
        ttk.Label(fr, text="COM Port:").grid(row=r, column=0, sticky="w", **pad)
        self._port_var = tk.StringVar()
        self._port_cb = ttk.Combobox(
            fr, textvariable=self._port_var, width=38, state="readonly")
        self._port_cb.grid(row=r, column=1, columnspan=2, sticky="ew", **pad)
        ttk.Button(fr, text="\u21bb", width=3,
                   command=self._refresh_ports).grid(row=r, column=3, **pad)
        r += 1

        # ---- Speaker ----
        ttk.Label(fr, text="Speaker:").grid(row=r, column=0, sticky="w", **pad)
        self._spk_var = tk.StringVar()
        self._spk_cb = ttk.Combobox(
            fr, textvariable=self._spk_var, width=38, state="readonly")
        self._spk_cb.grid(row=r, column=1, columnspan=2, sticky="ew", **pad)
        ttk.Button(fr, text="\u21bb", width=3,
                   command=self._refresh_speakers).grid(row=r, column=3, **pad)
        r += 1

        # ---- Connect ----
        self._conn_btn = ttk.Button(fr, text="Connect", command=self._toggle_conn)
        self._conn_btn.grid(row=r, column=0, columnspan=4, sticky="ew",
                            padx=6, pady=8)
        r += 1

        ttk.Separator(fr, orient="horizontal").grid(
            row=r, column=0, columnspan=4, sticky="ew", pady=4)
        r += 1

        # ---- Frequency ----
        ttk.Label(fr, text="Frequency:").grid(row=r, column=0, sticky="w", **pad)
        ff = ttk.Frame(fr)
        ff.grid(row=r, column=1, sticky="w", **pad)
        self._freq_var = tk.StringVar(value="144.174000")
        vcmd = (self.root.register(self._validate_freq), "%P")
        self._freq_ent = ttk.Entry(ff, textvariable=self._freq_var, width=14,
                                   validate="key", validatecommand=vcmd)
        self._freq_ent.pack(side="left")
        ttk.Label(ff, text=" MHz").pack(side="left")
        self._apply_btn = ttk.Button(fr, text="Set", width=6,
                                     command=self._apply_freq, state="disabled")
        self._apply_btn.grid(row=r, column=2, columnspan=2, sticky="e", **pad)
        r += 1

        # ---- Power slider ----
        ttk.Label(fr, text="Power:").grid(row=r, column=0, sticky="w", **pad)
        pf = ttk.Frame(fr)
        pf.grid(row=r, column=1, columnspan=3, sticky="ew", **pad)
        ttk.Label(pf, text="LOW").pack(side="left")
        self._pwr_var = tk.IntVar(value=1)
        self._pwr_scale = tk.Scale(
            pf, from_=1, to=7, orient="horizontal", showvalue=False,
            variable=self._pwr_var, resolution=1, command=self._on_power)
        self._pwr_scale.pack(side="left", fill="x", expand=True, padx=4)
        ttk.Label(pf, text="HIGH").pack(side="left")
        self._pwr_lbl = ttk.Label(pf, text="LOW1", width=5, anchor="center")
        self._pwr_lbl.pack(side="left", padx=(4, 0))
        r += 1

        ttk.Separator(fr, orient="horizontal").grid(
            row=r, column=0, columnspan=4, sticky="ew", pady=4)
        r += 1

        # ---- Bridge mode ----
        bf = ttk.LabelFrame(fr, text="Bridge Mode (FFT \u2192 TX)", padding=6)
        bf.grid(row=r, column=0, columnspan=4, sticky="ew", padx=6, pady=4)

        self._bridge_var = tk.BooleanVar(value=False)
        self._bridge_chk = ttk.Checkbutton(
            bf, text="Enable audio bridge", variable=self._bridge_var,
            command=self._toggle_bridge, state="disabled")
        self._bridge_chk.grid(row=0, column=0, columnspan=4, sticky="w")

        ttk.Label(bf, text="Analysis:").grid(row=1, column=0, sticky="w", pady=2)
        self._analysis_lbl = ttk.Label(bf, text="40 ms  (1/4 symbol)")
        self._analysis_lbl.grid(row=1, column=1, columnspan=3, sticky="w", pady=2)

        ttk.Label(bf, text="Tone Step:").grid(row=2, column=0, sticky="w", pady=2)
        self._tone_step_spin = ttk.Spinbox(
            bf, from_=0.0, to=200.0, increment=0.05, width=10,
            textvariable=self._tone_step_var)
        self._tone_step_spin.grid(row=2, column=1, sticky="w", pady=2)
        ttk.Label(bf, text="Hz  (0 = auto)").grid(row=2, column=2, columnspan=2, sticky="w", pady=2)

        ttk.Label(bf, text="Symbol:").grid(row=3, column=0, sticky="w", pady=2)
        self._symbol_preset_cb = ttk.Combobox(
            bf, textvariable=self._symbol_preset_var, width=12, state="readonly",
            values=list(BRIDGE_MODE_PRESETS.keys()))
        self._symbol_preset_cb.grid(row=3, column=1, sticky="w", pady=2)
        self._symbol_preset_cb.bind("<<ComboboxSelected>>", self._on_symbol_preset_changed)
        self._symbol_lbl = ttk.Label(bf, text="160 ms")
        self._symbol_lbl.grid(row=3, column=2, columnspan=2, sticky="w", pady=2)

        # Audio level bar
        ttk.Label(bf, text="Level:").grid(row=4, column=0, sticky="w", pady=2)
        self._level_canvas = tk.Canvas(bf, width=220, height=16,
                                       bg="#222222", highlightthickness=0)
        self._level_canvas.grid(row=4, column=1, columnspan=2, sticky="w", pady=2)
        self._level_lbl = ttk.Label(bf, text="- dB", width=8)
        self._level_lbl.grid(row=4, column=3, sticky="w", pady=2)

        # Detected frequency
        ttk.Label(bf, text="Peak:").grid(row=5, column=0, sticky="w", pady=2)
        self._peak_lbl = ttk.Label(bf, text="- Hz", font=("Consolas", 11))
        self._peak_lbl.grid(row=5, column=1, columnspan=3, sticky="w", pady=2)

        # VOX state
        ttk.Label(bf, text="VOX:").grid(row=6, column=0, sticky="w", pady=2)
        self._vox_lbl = ttk.Label(bf, text="Idle")
        self._vox_lbl.grid(row=6, column=1, columnspan=3, sticky="w", pady=2)

        r += 1

        ttk.Separator(fr, orient="horizontal").grid(
            row=r, column=0, columnspan=4, sticky="ew", pady=4)
        r += 1

        # ---- TURN button ----
        self._turn_btn = tk.Button(
            fr, text="TURN  (TX)", font=("Segoe UI", 12, "bold"),
            bg="#cc3333", fg="white", activebackground="#aa1111",
            activeforeground="white", relief="raised", bd=2,
            command=self._toggle_tx, state="disabled")
        self._turn_btn.grid(row=r, column=0, columnspan=4, sticky="ew",
                            padx=6, pady=6, ipady=6)
        r += 1

        # Timer
        self._timer_var = tk.StringVar()
        ttk.Label(fr, textvariable=self._timer_var, anchor="center").grid(
            row=r, column=0, columnspan=4)
        r += 1

        # Status bar
        self._status_var = tk.StringVar(value="Disconnected")
        ttk.Label(fr, textvariable=self._status_var, relief="sunken",
                  anchor="w").grid(row=r, column=0, columnspan=4,
                                   sticky="ew", pady=(8, 0), padx=6)

        logf = ttk.LabelFrame(fr, text="Sent Frequencies", padding=6)
        logf.grid(row=0, column=4, rowspan=r + 1, sticky="nsew", padx=(12, 0), pady=4)
        logf.rowconfigure(0, weight=1)
        logf.columnconfigure(0, weight=1)
        self._send_log = tk.Text(logf, width=34, height=24, wrap="none", state="disabled")
        self._send_log.grid(row=0, column=0, sticky="nsew")
        self._send_log_scroll = ttk.Scrollbar(logf, orient="vertical", command=self._send_log.yview)
        self._send_log_scroll.grid(row=0, column=1, sticky="ns")
        self._send_log.config(yscrollcommand=self._send_log_scroll.set)
        self._update_symbol_label()
        self._update_analysis_label()

    @staticmethod
    def _validate_freq(val: str) -> bool:
        if val == "":
            return True
        return val.count(".") <= 1 and all(c in "0123456789." for c in val)

    # ================================================================
    #  Device enumeration
    # ================================================================

    def _refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        self._ports = ports
        items = [f"{p.device} \u2013 {p.description}" for p in ports]
        self._port_cb["values"] = items
        aioc = 0
        for i, p in enumerate(ports):
            txt = (p.description or "") + (p.manufacturer or "")
            if "AIOC" in txt.upper():
                aioc = i
                break
        if items:
            self._port_cb.current(aioc)

    def _refresh_speakers(self):
        items: list[str] = []
        self._spk_devices: list[dict] = []
        aioc_idx = 0

        if _HAS_SC:
            try:
                for speaker in sc.all_speakers():
                    items.append(f"{speaker.name}  [loopback]")
                    self._spk_devices.append({
                        "backend": "soundcard",
                        "device": speaker,
                    })
                    if "AIOC" in speaker.name.upper():
                        aioc_idx = len(items) - 1
            except Exception:
                items = []
                self._spk_devices = []

        if not items and _HAS_SD:
            try:
                devs = sd.query_devices()
                for i, d in enumerate(devs):
                    if d["max_output_channels"] > 0:
                        items.append(f"[{i}] {d['name']}  [sounddevice]")
                        self._spk_devices.append({
                            "backend": "sounddevice",
                            "device": i,
                        })
                        if "AIOC" in d["name"].upper():
                            aioc_idx = len(items) - 1
            except Exception:
                items = []
                self._spk_devices = []

        if items:
            self._spk_cb["values"] = items
            self._spk_cb.current(aioc_idx)
            return

        if _HAS_SC or _HAS_SD:
            self._spk_cb["values"] = ["(error listing devices)"]
        else:
            self._spk_cb["values"] = ["(no audio backend available)"]

    def _selected_speaker(self) -> dict | None:
        """Get the selected audio device descriptor."""
        idx = self._spk_cb.current()
        if idx < 0 or idx >= len(getattr(self, "_spk_devices", [])):
            return None
        return self._spk_devices[idx]

    def _on_power(self, _val):
        v = self._pwr_var.get()
        self._pwr_lbl.config(text=POWER_LABELS[v - 1])

    def _on_symbol_preset_changed(self, _event=None):
        preset = BRIDGE_MODE_PRESETS.get(self._symbol_preset_var.get(), BRIDGE_MODE_PRESETS["Auto"])
        self._tone_step_var.set(float(preset["tone_step_hz"]))
        self._update_symbol_label()
        self._update_analysis_label()

    def _update_symbol_label(self):
        preset_name = self._symbol_preset_var.get()
        if BRIDGE_MODE_PRESETS.get(preset_name, {}).get("symbol_ms") is None:
            self._symbol_lbl.config(text=f"Auto  {self._bridge_symbol_ms()} ms")
        else:
            self._symbol_lbl.config(text=f"{self._bridge_symbol_ms()} ms")

    def _update_analysis_label(self):
        self._analysis_lbl.config(text=f"{self._analysis_window_ms()} ms  (1/4 symbol)")

    def _selected_symbol_ms(self) -> int | None:
        preset_name = self._symbol_preset_var.get()
        preset_ms = BRIDGE_MODE_PRESETS.get(preset_name, {}).get("symbol_ms")
        if preset_ms is None:
            return None
        return max(20, int(round(preset_ms)))

    def _analysis_window_ms(self) -> int:
        symbol_ms = self._bridge_symbol_ms()
        return max(ANALYSIS_WINDOW_MIN_MS, int(round(symbol_ms * ANALYSIS_WINDOW_RATIO)))

    def _analysis_block_size(self) -> int:
        window_ms = self._analysis_window_ms()
        return max(1, round(SAMPLE_RATE * window_ms / 1000))

    def _bridge_symbol_seed_ms(self) -> int:
        preset_ms = self._selected_symbol_ms()
        if preset_ms is not None:
            return preset_ms
        return BRIDGE_SYMBOL_MS_DEFAULT

    def _update_symbol_estimate(self, duration_ms: float):
        min_ms = 20
        max_ms = 2000
        duration_ms = max(min_ms, min(max_ms, duration_ms))
        if self._bridge_symbol_est_ms is None:
            self._bridge_symbol_est_ms = duration_ms
        else:
            alpha = BRIDGE_SYMBOL_EST_SMOOTHING
            self._bridge_symbol_est_ms = (1.0 - alpha) * self._bridge_symbol_est_ms + alpha * duration_ms
        if self._selected_symbol_ms() is None:
            self._update_symbol_label()
            self._update_analysis_label()

    def _bridge_symbol_ms(self) -> int:
        preset_ms = self._selected_symbol_ms()
        if preset_ms is not None:
            return preset_ms
        if self._bridge_symbol_est_ms is None:
            return self._bridge_symbol_seed_ms()
        return int(round(max(20, min(2000, self._bridge_symbol_est_ms))))

    def _bridge_decision_ms(self) -> int:
        symbol_ms = self._bridge_symbol_ms()
        return max(20, min(symbol_ms, round(symbol_ms * BRIDGE_DECISION_RATIO)))

    def _bridge_change_confirm_ms(self) -> int:
        symbol_ms = self._bridge_symbol_ms()
        return max(10, round(symbol_ms * BRIDGE_CHANGE_CONFIRM_RATIO))

    def _bridge_min_send_ms(self) -> int:
        symbol_ms = self._bridge_symbol_ms()
        return max(self._bridge_change_confirm_ms(), round(symbol_ms * BRIDGE_MIN_SEND_RATIO))

    def _bridge_tone_step_hz(self) -> float:
        try:
            step_hz = float(self._tone_step_var.get())
        except (ValueError, tk.TclError):
            step_hz = BRIDGE_TONE_STEP_HZ_DEFAULT
        return max(0.0, step_hz)

    def _bridge_cluster_tol_hz(self) -> float:
        fft_bin_hz = SAMPLE_RATE / self._analysis_block_size()
        return max(BRIDGE_STABLE_TOL_HZ, fft_bin_hz * 0.15)

    def _reset_bridge_history(self):
        self._bridge_freq_history.clear()

    def _reset_bridge_symbol_tracking(self):
        self._bridge_observed_symbol_hz = None
        self._bridge_observed_symbol_since = None
        if self._selected_symbol_ms() is None:
            self._bridge_symbol_est_ms = None
            self._update_symbol_label()
            self._update_analysis_label()

    def _quantize_bridge_freq(self, freq_hz: float) -> float:
        step_hz = self._bridge_tone_step_hz()
        if step_hz <= 0.0:
            return freq_hz
        return round(freq_hz / step_hz) * step_hz

    def _cluster_bridge_freqs(self, recent: list[tuple[float, float]]) -> tuple[float, int] | None:
        if not recent:
            return None

        step_hz = self._bridge_tone_step_hz()
        if step_hz > 0.0:
            counts: dict[float, int] = {}
            last_seen: dict[float, float] = {}
            for when, freq in recent:
                snapped = self._quantize_bridge_freq(freq)
                counts[snapped] = counts.get(snapped, 0) + 1
                last_seen[snapped] = when
            dominant = max(counts, key=lambda freq: (counts[freq], last_seen[freq]))
            return dominant, counts[dominant]

        tol_hz = self._bridge_cluster_tol_hz()
        clusters: list[dict] = []
        for when, freq in recent:
            best_cluster = None
            best_distance = None
            for cluster in clusters:
                distance = abs(freq - cluster["center"])
                if distance <= tol_hz and (best_distance is None or distance < best_distance):
                    best_cluster = cluster
                    best_distance = distance
            if best_cluster is None:
                clusters.append({
                    "center": freq,
                    "freqs": [freq],
                    "count": 1,
                    "last_when": when,
                })
                continue
            best_cluster["freqs"].append(freq)
            best_cluster["count"] += 1
            best_cluster["last_when"] = when
            best_cluster["center"] = float(statistics.median(best_cluster["freqs"]))

        if not clusters:
            return None

        dominant_cluster = max(
            clusters,
            key=lambda cluster: (cluster["count"], cluster["last_when"]),
        )
        return float(dominant_cluster["center"]), int(dominant_cluster["count"])

    def _reset_bridge_candidate(self):
        self._bridge_candidate_hz = None
        self._bridge_candidate_since = None

    def _append_bridge_freq(self, when: float, peak_hz: float):
        self._bridge_freq_history.append((when, peak_hz))
        cutoff = when - self._bridge_symbol_ms() / 1000.0
        while self._bridge_freq_history and self._bridge_freq_history[0][0] < cutoff:
            self._bridge_freq_history.popleft()

    def _stable_bridge_freq(self) -> float | None:
        if len(self._bridge_freq_history) < BRIDGE_STABLE_FRAMES:
            return None

        latest_when = self._bridge_freq_history[-1][0]
        cutoff = latest_when - self._bridge_decision_ms() / 1000.0
        recent = [
            (when, freq)
            for when, freq in self._bridge_freq_history
            if when >= cutoff
        ]

        if len(recent) < BRIDGE_STABLE_FRAMES:
            return None

        clustered = self._cluster_bridge_freqs(recent)
        if clustered is None:
            return None
        dominant, dominant_count = clustered
        if dominant_count < BRIDGE_STABLE_FRAMES:
            return None

        latest_freq = self._quantize_bridge_freq(recent[-1][1])
        latest_tol_hz = self._bridge_tone_step_hz() / 2.0 if self._bridge_tone_step_hz() > 0.0 else self._bridge_cluster_tol_hz()
        if abs(latest_freq - dominant) > latest_tol_hz:
            return None

        return float(dominant)

    def _update_bridge_symbol(self, stable_peak_hz: float, now: float):
        if self._bridge_observed_symbol_hz is None:
            self._bridge_observed_symbol_hz = stable_peak_hz
            self._bridge_observed_symbol_since = now
        elif abs(stable_peak_hz - self._bridge_observed_symbol_hz) >= 0.01:
            if self._bridge_observed_symbol_since is not None:
                duration_ms = (now - self._bridge_observed_symbol_since) * 1000.0
                if duration_ms >= max(20, self._analysis_window_ms()):
                    self._update_symbol_estimate(duration_ms)
            self._bridge_observed_symbol_hz = stable_peak_hz
            self._bridge_observed_symbol_since = now

        if self._bridge_last_freq_dhz is None and self._bridge_last_send_ts is None:
            self._bridge_send_freq(stable_peak_hz, now)
            self._reset_bridge_candidate()
            return

        if self._bridge_last_freq_dhz is not None:
            last_sent_hz = self._bridge_last_freq_dhz / 10.0
            if abs(stable_peak_hz - last_sent_hz) < 0.01:
                self._reset_bridge_candidate()
                return

        if self._bridge_candidate_hz is None or abs(stable_peak_hz - self._bridge_candidate_hz) >= 0.01:
            self._bridge_candidate_hz = stable_peak_hz
            self._bridge_candidate_since = now
            return

        if self._bridge_candidate_since is None:
            self._bridge_candidate_since = now
            return

        if now - self._bridge_candidate_since < self._bridge_change_confirm_ms() / 1000.0:
            return

        if self._bridge_last_send_ts is not None and now - self._bridge_last_send_ts < self._bridge_min_send_ms() / 1000.0:
            return

        self._bridge_send_freq(self._bridge_candidate_hz, now)
        self._reset_bridge_candidate()

    def _clear_send_log(self):
        self._send_log.config(state="normal")
        self._send_log.delete("1.0", "end")
        self._send_log.config(state="disabled")

    def _append_send_log(self, freq_dhz: int, delta_ms: float | None):
        delta_text = "start" if delta_ms is None else f"+{delta_ms:.1f} ms"
        line = f"{freq_dhz / 10:.1f} Hz   {delta_text}\n"
        self._send_log.config(state="normal")
        self._send_log.insert("end", line)
        self._send_log.see("end")
        self._send_log.config(state="disabled")

    def _start_bridge_trace(self):
        if not BRIDGE_TRACE_ENABLED:
            return

    def _stop_bridge_trace(self):
        if not BRIDGE_TRACE_ENABLED:
            return

    def _trace_bridge_window(
        self,
        audio_seq: int,
        audio_ts: float,
        loop_ts: float,
        rms: float,
        raw_peak_hz: float,
        stable_peak_hz: float | None,
        sent_freq_dhz: int | None,
    ):
        if not BRIDGE_TRACE_ENABLED:
            return

    # ================================================================
    #  Connect / disconnect
    # ================================================================

    def _toggle_conn(self):
        if self._ser and self._ser.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        idx = self._port_cb.current()
        if idx < 0 or idx >= len(self._ports):
            messagebox.showerror("Error", "Please select a COM port.")
            return
        port = self._ports[idx].device
        try:
            self._ser = serial.Serial(port, 38400, timeout=0.1)
            time.sleep(0.3)
            self._ser.reset_input_buffer()
            self._ser.write(b"\x00" * 8)
            time.sleep(0.1)
            self._ser.reset_input_buffer()
        except Exception as exc:
            messagebox.showerror("Error", f"Cannot open {port}:\n{exc}")
            return

        self._status_var.set(f"Connected to {port}, syncing clock\u2026")
        self.root.update()

        self._clock = _Clock()
        if self._clock.sync(self._ser):
            self._status_var.set(
                f"Connected to {port}  |  RTT {self._clock.rtt}\u00b5s")
        else:
            self._status_var.set(
                f"Connected to {port}  |  sync failed (zero offset)")

        self._conn_btn.config(text="Disconnect")
        self._apply_btn.config(state="normal")
        self._turn_btn.config(state="normal")
        self._bridge_chk.config(state="normal")
        self._port_cb.config(state="disabled")
        self._spk_cb.config(state="disabled")

    def _disconnect(self):
        if self._bridge_on:
            self._bridge_var.set(False)
            self._toggle_bridge()
        if self._tx_active:
            self._stop_tx()
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None
        self._clock = None
        self._conn_btn.config(text="Connect")
        self._apply_btn.config(state="disabled")
        self._turn_btn.config(state="disabled")
        self._bridge_chk.config(state="disabled")
        self._port_cb.config(state="readonly")
        self._spk_cb.config(state="readonly")
        self._status_var.set("Disconnected")

    # ================================================================
    #  Frequency
    # ================================================================

    def _freq_10hz(self) -> int | None:
        try:
            mhz = float(self._freq_var.get())
            if mhz < 18.0 or mhz > 1400.0:
                raise ValueError
            return round(mhz * 100_000)
        except (ValueError, TypeError):
            return None

    def _apply_freq(self):
        f = self._freq_10hz()
        if f is None:
            messagebox.showerror("Error",
                                 "Invalid frequency (18\u20131400 MHz).")
            return
        if self._tx_active:
            self._stop_tx()
        mhz = f / 100_000
        self._status_var.set(f"Frequency: {mhz:.6f} MHz  |  ready")

    # ================================================================
    #  Manual TURN TX
    # ================================================================

    def _toggle_tx(self):
        if self._tx_active:
            self._stop_tx()
        else:
            self._start_tx()

    def _start_tx(self):
        if not self._ser or not self._ser.is_open:
            messagebox.showerror("Error", "Not connected.")
            return
        if self._bridge_on:
            messagebox.showinfo("Info", "Disable bridge mode first.")
            return
        freq = self._freq_10hz()
        if freq is None:
            messagebox.showerror("Error",
                                 "Invalid frequency (18\u20131400 MHz).")
            return

        power = self._pwr_var.get()
        payload = struct.pack(">I", freq) + struct.pack("B", power)
        try:
            self._ser.write(_frame(CMD_START_TX, payload))
            time.sleep(0.05)
            self._ser.read(64)
        except Exception as exc:
            messagebox.showerror("Error", f"Serial write failed:\n{exc}")
            return

        self._tx_active = True
        self._stop_evt.clear()
        self._tx_t0 = time.monotonic()

        self._turn_btn.config(text="STOP  TX", bg="#226622",
                              activebackground="#114411")
        self._apply_btn.config(state="disabled")
        self._conn_btn.config(state="disabled")
        self._freq_ent.config(state="disabled")
        self._pwr_scale.config(state="disabled")
        self._bridge_chk.config(state="disabled")

        self._update_timer()
        threading.Thread(target=self._heartbeat_loop, daemon=True).start()

    def _heartbeat_loop(self):
        """Background heartbeat for manual TURN mode."""
        while not self._stop_evt.is_set():
            if time.monotonic() - self._tx_t0 >= MAX_TX_S:
                self.root.after(0, self._stop_tx)
                return
            try:
                self._ser.write(NOOP_FRAME)
            except Exception:
                self.root.after(0, self._stop_tx)
                return
            self._stop_evt.wait(HEARTBEAT_S)

    def _update_timer(self):
        if not self._tx_active:
            self._timer_var.set("")
            return
        elapsed = time.monotonic() - self._tx_t0
        remain = max(0.0, MAX_TX_S - elapsed)
        self._timer_var.set(
            f"TX  {elapsed:.1f}s / {MAX_TX_S}s   (remaining {remain:.1f}s)")
        self.root.after(200, self._update_timer)

    def _stop_tx(self):
        if not self._tx_active:
            return
        self._stop_evt.set()
        self._tx_active = False

        self._send_stop_with_retry()

        self._turn_btn.config(text="TURN  (TX)", bg="#cc3333",
                              activebackground="#aa1111")
        self._apply_btn.config(state="normal")
        self._conn_btn.config(state="normal")
        self._freq_ent.config(state="normal")
        self._pwr_scale.config(state="normal")
        if self._ser and self._ser.is_open:
            self._bridge_chk.config(state="normal")
        self._timer_var.set("")

        base = self._status_var.get().split("|")[0].strip()
        self._status_var.set(f"{base}  |  TX stopped")

    def _send_stop_with_retry(self):
        if not self._ser or not self._ser.is_open:
            return
        for _ in range(5):
            try:
                self._ser.write(_frame(CMD_STOP_TX))
                time.sleep(0.1)
                resp = self._ser.read(64)
                if resp:
                    r = _parse(resp)
                    if r and r[0] == CMD_ACK:
                        return
            except Exception:
                return

    # ================================================================
    #  Bridge mode — FFT peak detection + software VOX
    # ================================================================

    def _toggle_bridge(self):
        if self._bridge_var.get():
            self._start_bridge()
        else:
            self._stop_bridge()

    def _start_bridge(self):
        if not _HAS_SC and not _HAS_SD:
            messagebox.showerror("Error", "No audio capture backend available.")
            self._bridge_var.set(False)
            return
        dev = self._selected_speaker()
        if dev is None:
            messagebox.showerror("Error", "No speaker device selected.")
            self._bridge_var.set(False)
            return
        if not self._ser or not self._ser.is_open:
            messagebox.showerror("Error", "Not connected.")
            self._bridge_var.set(False)
            return

        try:
            self._audio_mon = _AudioMonitor(dev, self._analysis_block_size())
            self._audio_mon.start()
        except Exception as exc:
            messagebox.showerror("Error",
                                 f"Cannot open audio loopback:\n{exc}")
            self._bridge_var.set(False)
            return

        self._bridge_on = True
        self._bridge_tx = False
        self._vox_last_audio = 0.0
        self._bridge_active_streak = 0
        self._bridge_last_freq_dhz = None
        self._bridge_last_send_ts = None
        self._reset_bridge_history()
        self._reset_bridge_candidate()
        self._reset_bridge_symbol_tracking()
        self._clear_send_log()
        self._start_bridge_trace()
        self._turn_btn.config(state="disabled")
        self._spk_cb.config(state="disabled")
        self._tone_step_spin.config(state="disabled")
        self._symbol_preset_cb.config(state="disabled")
        self._bridge_stop_evt.clear()
        self._bridge_thread = threading.Thread(target=self._bridge_loop, daemon=True)
        self._bridge_thread.start()

    def _stop_bridge(self):
        self._bridge_on = False
        self._bridge_stop_evt.set()
        if self._bridge_thread is not None and self._bridge_thread.is_alive() and threading.current_thread() is not self._bridge_thread:
            self._bridge_thread.join(timeout=1.0)
        self._bridge_thread = None
        if self._bridge_tx:
            self._bridge_stop_tx()
        if self._audio_mon is not None:
            self._audio_mon.stop()
            self._audio_mon = None
        self._stop_bridge_trace()
        self._bridge_active_streak = 0
        self._bridge_last_freq_dhz = None
        self._bridge_last_send_ts = None
        self._reset_bridge_history()
        self._reset_bridge_candidate()
        self._reset_bridge_symbol_tracking()

        self._level_canvas.delete("all")
        self._level_lbl.config(text="- dB")
        self._peak_lbl.config(text="- Hz")
        self._vox_lbl.config(text="Idle")

        if self._ser and self._ser.is_open:
            self._turn_btn.config(state="normal")
            self._spk_cb.config(state="disabled")  # still connected
        self._tone_step_spin.config(state="normal")
        self._symbol_preset_cb.config(state="readonly")

    def _bridge_loop(self):
        while self._bridge_on and not self._bridge_stop_evt.is_set():
            if self._audio_mon is None:
                return

            if self._audio_mon.last_error:
                err = self._audio_mon.last_error
                self.root.after(0, self._handle_bridge_error, err)
                return

            audio_seq, audio_ts, rms, peak_hz = self._audio_mon.snapshot()
            loop_now = time.monotonic()
            decision_ts = audio_ts if audio_ts > 0.0 else loop_now

            audio_present = rms > VOX_THRESHOLD and peak_hz > 0
            stable_peak_hz = None
            sent_freq_dhz = None

            if audio_present:
                self._vox_last_audio = decision_ts
                self._bridge_active_streak += 1
                self._append_bridge_freq(decision_ts, peak_hz)
                stable_peak_hz = self._stable_bridge_freq()
            else:
                self._bridge_active_streak = 0
                self._reset_bridge_history()
                self._reset_bridge_candidate()
                self._bridge_observed_symbol_hz = None
                self._bridge_observed_symbol_since = None

            tx_activate = self._bridge_active_streak >= BRIDGE_ACTIVATE_FRAMES

            if tx_activate and not self._bridge_tx:
                self._bridge_start_tx()
            elif self._bridge_tx and (decision_ts - self._vox_last_audio > VOX_HANG_S):
                self._bridge_stop_tx()

            if self._bridge_tx and stable_peak_hz is not None:
                prev_freq_dhz = self._bridge_last_freq_dhz
                self._update_bridge_symbol(stable_peak_hz, decision_ts)
                if self._bridge_last_freq_dhz != prev_freq_dhz:
                    sent_freq_dhz = self._bridge_last_freq_dhz

            if self._bridge_tx or self._bridge_on:
                try:
                    self._ser.write(NOOP_FRAME)
                except Exception:
                    pass

            self._trace_bridge_window(
                audio_seq=audio_seq,
                audio_ts=audio_ts,
                loop_ts=loop_now,
                rms=rms,
                raw_peak_hz=peak_hz,
                stable_peak_hz=stable_peak_hz,
                sent_freq_dhz=sent_freq_dhz,
            )

            display_peak_hz = None
            if self._bridge_last_freq_dhz is not None:
                display_peak_hz = self._bridge_last_freq_dhz / 10.0
            elif peak_hz > 0:
                display_peak_hz = round(peak_hz * 10) / 10.0
            self.root.after(0, self._bridge_update_ui, rms, display_peak_hz)

            if self._bridge_stop_evt.wait(BRIDGE_POLL_MS / 1000.0):
                return

    def _handle_bridge_error(self, err: str):
        self._bridge_var.set(False)
        self._stop_bridge()
        messagebox.showerror("Error", f"Audio loopback failed:\n{err}")

    def _bridge_update_ui(self, rms: float, peak_hz: float | None):
        if not self._bridge_on:
            return
        self._draw_level(rms)
        if peak_hz is None:
            self._peak_lbl.config(text="- Hz")
        else:
            self._peak_lbl.config(text=f"{peak_hz:.1f} Hz")

    def _draw_level(self, rms: float):
        self._level_canvas.delete("all")
        if rms <= 0:
            db = -60.0
        else:
            db = 20.0 * math.log10(max(rms, 1e-10))
        db_clamped = max(-60.0, min(0.0, db))
        frac = (db_clamped + 60.0) / 60.0
        w = int(220 * frac)
        color = "#44cc44" if db_clamped < -12 else ("#cccc22" if db_clamped < -3 else "#cc3333")
        if w > 0:
            self._level_canvas.create_rectangle(0, 0, w, 16, fill=color, outline="")
        self._level_lbl.config(text=f"{db_clamped:.0f} dB")

    def _bridge_start_tx(self):
        freq = self._freq_10hz()
        if freq is None or not self._ser:
            return
        power = self._pwr_var.get()
        payload = struct.pack(">I", freq) + struct.pack("B", power)
        try:
            self._ser.write(_frame(CMD_START_TX, payload))
            time.sleep(0.02)
            self._ser.read(64)
        except Exception:
            return
        self._bridge_tx = True
        self.root.after(0, self._set_bridge_tx_ui, True)

    def _bridge_stop_tx(self):
        self._bridge_tx = False
        self._bridge_last_freq_dhz = None
        self._bridge_last_send_ts = None
        self._reset_bridge_history()
        self._reset_bridge_candidate()
        self._bridge_observed_symbol_hz = None
        self._bridge_observed_symbol_since = None
        self._send_stop_with_retry()
        self.root.after(0, self._set_bridge_tx_ui, False)

    def _set_bridge_tx_ui(self, active: bool):
        if active:
            self._vox_lbl.config(text="\u25cf TX", foreground="red")
            self._apply_btn.config(state="disabled")
            self._conn_btn.config(state="disabled")
            self._freq_ent.config(state="disabled")
            self._pwr_scale.config(state="disabled")
            return
        self._vox_lbl.config(text="Idle (RX)", foreground="")
        self._apply_btn.config(state="normal")
        if self._ser and self._ser.is_open:
            self._conn_btn.config(state="normal")
        self._freq_ent.config(state="normal")
        self._pwr_scale.config(state="normal")

    def _bridge_send_freq(self, audio_hz: float, now: float | None = None):
        """Send SET_FREQ with detected audio peak frequency."""
        if not self._ser or not self._clock:
            return
        freq_dhz = max(0, min(65535, round(audio_hz * 10)))
        if freq_dhz == self._bridge_last_freq_dhz:
            return
        apply_at = self._clock.to_radio(self._clock.now() + FREQ_APPLY_AHEAD_US)
        payload = struct.pack(">H", freq_dhz) * FREQ_COPIES + struct.pack(">I", apply_at)
        try:
            self._ser.write(_frame(CMD_SET_FREQ, payload))
            now = time.monotonic() if now is None else now
            delta_ms = None if self._bridge_last_send_ts is None else (now - self._bridge_last_send_ts) * 1000.0
            self._bridge_last_freq_dhz = freq_dhz
            self._bridge_last_send_ts = now
            self.root.after(0, self._on_bridge_freq_sent, freq_dhz, delta_ms)
        except Exception:
            pass

    def _on_bridge_freq_sent(self, freq_dhz: int, delta_ms: float | None):
        self._peak_lbl.config(text=f"{freq_dhz / 10:.1f} Hz")
        self._append_send_log(freq_dhz, delta_ms)

    # ================================================================
    #  Lifecycle
    # ================================================================

    def on_close(self):
        if self._bridge_on:
            self._bridge_var.set(False)
            self._stop_bridge()
        if self._tx_active:
            self._stop_tx()
        if self._ser and self._ser.is_open:
            self._ser.close()
        self.root.destroy()


def main():
    root = tk.Tk()
    app = App(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()


if __name__ == "__main__":
    main()
