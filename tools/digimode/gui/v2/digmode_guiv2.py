#!/usr/bin/env python3
"""
FT8 UART symbol scheduler GUI.

This version does not capture or generate audio. It encodes a text message into
FT8 symbols with PyFT8 and sends the remaining symbols in the current 15-second
cycle over the existing UART protocol.
"""

import math
import statistics
import struct
import time
import tkinter as tk
from dataclasses import dataclass
from tkinter import messagebox, ttk

import serial
import serial.tools.list_ports
from PyFT8.transmitter import pack_message


SYNC = 0xAB
CMD_START_TX = 0x01
CMD_STOP_TX = 0x02
CMD_SET_FREQ = 0x03
CMD_ACK = 0x05
CMD_SYNC_REQ = 0x06
CMD_SYNC_RESP = 0x07
CMD_NOOP = 0x08

POWER_LABELS = ["LOW1", "LOW2", "LOW3", "LOW4", "LOW5", "MID", "HIGH"]

FT8_CYCLE_SECONDS = 15.0
FT8_SYMBOL_COUNT = 79
FT8_SYMBOL_SECONDS = 0.160
FT8_TX_SECONDS = FT8_SYMBOL_COUNT * FT8_SYMBOL_SECONDS
FT8_TONE_STEP_HZ = 6.25
DEFAULT_TONE_BASE_HZ = 1000.0
HEARTBEAT_S = 0.08
SET_FREQ_LEAD_S = 0.03
FREQ_COPIES = 5
UI_TICK_MS = 50


def _crc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
    return crc


def _frame(cmd: int, payload: bytes = b"") -> bytes:
    header = bytes([SYNC, cmd, len(payload)])
    body = header + payload
    return body + bytes([_crc(body)])


NOOP_FRAME = _frame(CMD_NOOP)


def _parse(data: bytes):
    while len(data) >= 4:
        if data[0] != SYNC:
            index = data.find(bytes([SYNC]), 1)
            if index < 0:
                return None
            data = data[index:]
            continue
        length = data[2]
        frame_size = 3 + length + 1
        if len(data) < frame_size:
            return None
        if _crc(data[: frame_size - 1]) != data[frame_size - 1]:
            data = data[1:]
            continue
        return data[1], data[3 : 3 + length], data[frame_size:]
    return None


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
            offset = (t2 - t1 - rtt // 2) & 0xFFFFFFFF
            if offset > 0x7FFFFFFF:
                offset -= 0x100000000
            offsets.append(offset)
            rtts.append(rtt)
            time.sleep(0.03)
        if len(offsets) < 2:
            return False
        self.offset = int(statistics.median(offsets))
        self.rtt = int(statistics.median(rtts))
        return True


@dataclass
class ActiveTransmission:
    token: int
    message: str
    symbols: list[int]
    cycle_start_wall: float
    cycle_start_mono: float
    start_symbol_index: int
    next_symbol_index: int
    emitted_count: int = 0


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("Digmode Control v2")
        self.root.resizable(False, False)

        self._ser: serial.Serial | None = None
        self._clock: _Clock | None = None
        self._ports = []
        self._active_tx: ActiveTransmission | None = None
        self._tx_token = 0
        self._radio_tx_active = False
        self._last_noop_ts = 0.0

        self._port_var = tk.StringVar()
        self._freq_var = tk.StringVar(value="144.174000")
        self._tone_base_var = tk.DoubleVar(value=DEFAULT_TONE_BASE_HZ)
        self._message_var = tk.StringVar(value="CQ TEST AA00")
        self._pwr_var = tk.IntVar(value=1)
        self._cycle_var = tk.StringVar(value="Cycle: -")
        self._window_var = tk.StringVar(value="TX window: -")
        self._active_var = tk.StringVar(value="Idle")
        self._status_var = tk.StringVar(value="Disconnected")

        self._build_ui()
        self._refresh_ports()
        self._tick()

    def _build_ui(self):
        pad = dict(padx=6, pady=4)
        frame = ttk.Frame(self.root, padding=12)
        frame.grid(sticky="nsew")
        frame.columnconfigure(1, weight=1)

        row = 0
        ttk.Label(frame, text="COM Port:").grid(row=row, column=0, sticky="w", **pad)
        self._port_cb = ttk.Combobox(frame, textvariable=self._port_var, width=36, state="readonly")
        self._port_cb.grid(row=row, column=1, columnspan=2, sticky="ew", **pad)
        ttk.Button(frame, text="↻", width=3, command=self._refresh_ports).grid(row=row, column=3, **pad)
        row += 1

        self._conn_btn = ttk.Button(frame, text="Connect", command=self._toggle_conn)
        self._conn_btn.grid(row=row, column=0, columnspan=4, sticky="ew", padx=6, pady=8)
        row += 1

        ttk.Separator(frame, orient="horizontal").grid(row=row, column=0, columnspan=4, sticky="ew", pady=4)
        row += 1

        ttk.Label(frame, text="Dial Freq:").grid(row=row, column=0, sticky="w", **pad)
        freq_frame = ttk.Frame(frame)
        freq_frame.grid(row=row, column=1, sticky="w", **pad)
        self._freq_entry = ttk.Entry(freq_frame, textvariable=self._freq_var, width=14)
        self._freq_entry.pack(side="left")
        ttk.Label(freq_frame, text=" MHz").pack(side="left")
        row += 1

        ttk.Label(frame, text="Power:").grid(row=row, column=0, sticky="w", **pad)
        power_frame = ttk.Frame(frame)
        power_frame.grid(row=row, column=1, columnspan=3, sticky="ew", **pad)
        ttk.Label(power_frame, text="LOW").pack(side="left")
        self._pwr_scale = tk.Scale(
            power_frame,
            from_=1,
            to=7,
            orient="horizontal",
            showvalue=False,
            variable=self._pwr_var,
            resolution=1,
            command=self._on_power,
        )
        self._pwr_scale.pack(side="left", fill="x", expand=True, padx=4)
        ttk.Label(power_frame, text="HIGH").pack(side="left")
        self._pwr_lbl = ttk.Label(power_frame, text=POWER_LABELS[0], width=5, anchor="center")
        self._pwr_lbl.pack(side="left", padx=(4, 0))
        row += 1

        ttk.Label(frame, text="Tone Base:").grid(row=row, column=0, sticky="w", **pad)
        tone_frame = ttk.Frame(frame)
        tone_frame.grid(row=row, column=1, columnspan=3, sticky="w", **pad)
        self._tone_base_spin = ttk.Spinbox(
            tone_frame,
            from_=100.0,
            to=4000.0,
            increment=1.0,
            width=10,
            textvariable=self._tone_base_var,
        )
        self._tone_base_spin.pack(side="left")
        ttk.Label(tone_frame, text=f" Hz  |  step {FT8_TONE_STEP_HZ:.2f} Hz").pack(side="left")
        row += 1

        ttk.Separator(frame, orient="horizontal").grid(row=row, column=0, columnspan=4, sticky="ew", pady=4)
        row += 1

        ttk.Label(frame, text="Message:").grid(row=row, column=0, sticky="w", **pad)
        self._message_entry = ttk.Entry(frame, textvariable=self._message_var, width=36)
        self._message_entry.grid(row=row, column=1, columnspan=3, sticky="ew", **pad)
        row += 1

        ttk.Button(frame, text="Send", command=self._send_clicked).grid(row=row, column=1, sticky="ew", padx=6, pady=6)
        ttk.Button(frame, text="Stop", command=self._stop_clicked).grid(row=row, column=2, sticky="ew", padx=6, pady=6)
        row += 1

        ttk.Label(frame, textvariable=self._cycle_var).grid(row=row, column=0, columnspan=4, sticky="w", **pad)
        row += 1
        ttk.Label(frame, textvariable=self._window_var).grid(row=row, column=0, columnspan=4, sticky="w", **pad)
        row += 1
        ttk.Label(frame, textvariable=self._active_var).grid(row=row, column=0, columnspan=4, sticky="w", **pad)
        row += 1

        ttk.Label(frame, text="Remaining symbols:").grid(row=row, column=0, sticky="nw", padx=6, pady=(10, 4))
        self._remaining_text = tk.Text(frame, width=54, height=5, wrap="word", state="disabled")
        self._remaining_text.grid(row=row, column=1, columnspan=3, sticky="ew", padx=6, pady=(10, 4))
        row += 1

        ttk.Label(frame, text="UART log:").grid(row=row, column=0, sticky="nw", padx=6, pady=4)
        self._log_text = tk.Text(frame, width=54, height=16, wrap="none", state="disabled")
        self._log_text.grid(row=row, column=1, columnspan=3, sticky="ew", padx=6, pady=4)
        row += 1

        ttk.Label(frame, textvariable=self._status_var, relief="sunken", anchor="w").grid(
            row=row, column=0, columnspan=4, sticky="ew", padx=6, pady=(8, 0)
        )

    def _append_log(self, line: str):
        self._log_text.config(state="normal")
        self._log_text.insert("end", line + "\n")
        self._log_text.see("end")
        self._log_text.config(state="disabled")

    def _set_remaining_text(self, text: str):
        self._remaining_text.config(state="normal")
        self._remaining_text.delete("1.0", "end")
        self._remaining_text.insert("1.0", text)
        self._remaining_text.config(state="disabled")

    def _ts(self) -> str:
        return time.strftime("%H:%M:%S", time.localtime())

    def _refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        self._ports = ports
        items = [f"{port.device} – {port.description}" for port in ports]
        self._port_cb["values"] = items
        if items:
            self._port_cb.current(0)

    def _toggle_conn(self):
        if self._ser and self._ser.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        index = self._port_cb.current()
        if index < 0 or index >= len(self._ports):
            messagebox.showerror("Error", "Please select a COM port.")
            return
        port = self._ports[index].device
        try:
            self._ser = serial.Serial(port, 38400, timeout=0.1)
            time.sleep(0.3)
            self._ser.reset_input_buffer()
            self._ser.write(b"\x00" * 8)
            time.sleep(0.1)
            self._ser.reset_input_buffer()
        except Exception as exc:
            messagebox.showerror("Error", f"Cannot open {port}:\n{exc}")
            self._ser = None
            return

        self._status_var.set(f"Connected to {port}, syncing clock…")
        self.root.update()

        self._clock = _Clock()
        if self._clock.sync(self._ser):
            self._status_var.set(f"Connected to {port}  |  RTT {self._clock.rtt}µs")
        else:
            self._status_var.set(f"Connected to {port}  |  sync failed (zero offset)")

        self._conn_btn.config(text="Disconnect")
        self._port_cb.config(state="disabled")

    def _disconnect(self):
        self._stop_clicked()
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None
        self._clock = None
        self._conn_btn.config(text="Connect")
        self._port_cb.config(state="readonly")
        self._status_var.set("Disconnected")

    def _on_power(self, _value):
        self._pwr_lbl.config(text=POWER_LABELS[self._pwr_var.get() - 1])

    def _freq_10hz(self) -> int | None:
        try:
            mhz = float(self._freq_var.get())
            if mhz < 18.0 or mhz > 1400.0:
                raise ValueError
            return round(mhz * 100_000)
        except (ValueError, TypeError):
            return None

    def _tone_hz_for_symbol(self, symbol: int) -> float:
        try:
            base_hz = float(self._tone_base_var.get())
        except (ValueError, tk.TclError):
            base_hz = DEFAULT_TONE_BASE_HZ
        return max(0.0, base_hz + symbol * FT8_TONE_STEP_HZ)

    def _encode_message(self, message: str) -> list[int]:
        parts = message.upper().split()
        if len(parts) != 3:
            raise ValueError("PyFT8 message must contain exactly 3 fields, e.g. 'CQ TEST AA00'.")
        symbols = pack_message(parts[0], parts[1], parts[2])
        if not isinstance(symbols, list) or len(symbols) != FT8_SYMBOL_COUNT:
            raise ValueError("PyFT8 did not return a valid 79-symbol FT8 frame.")
        return [int(symbol) for symbol in symbols]

    def _start_radio_tx(self) -> bool:
        if self._radio_tx_active:
            return True
        if not self._ser or not self._ser.is_open:
            messagebox.showerror("Error", "Not connected.")
            return False
        freq = self._freq_10hz()
        if freq is None:
            messagebox.showerror("Error", "Invalid dial frequency (18–1400 MHz).")
            return False

        payload = struct.pack(">I", freq) + struct.pack("B", self._pwr_var.get())
        try:
            self._ser.write(_frame(CMD_START_TX, payload))
            time.sleep(0.02)
            self._ser.read(64)
        except Exception as exc:
            messagebox.showerror("Error", f"Serial write failed:\n{exc}")
            return False

        self._radio_tx_active = True
        self._last_noop_ts = time.monotonic()
        self._append_log(f"{self._ts()} START_TX")
        return True

    def _send_stop_with_retry(self):
        if not self._ser or not self._ser.is_open:
            return
        for _ in range(5):
            try:
                self._ser.write(_frame(CMD_STOP_TX))
                time.sleep(0.05)
                resp = self._ser.read(64)
                if resp:
                    result = _parse(resp)
                    if result and result[0] == CMD_ACK:
                        return
            except Exception:
                return

    def _stop_radio_tx(self):
        if not self._radio_tx_active:
            return
        self._send_stop_with_retry()
        self._radio_tx_active = False
        self._append_log(f"{self._ts()} STOP_TX")

    def _send_symbol_freq(self, symbol_index: int, symbol: int, target_mono: float):
        if not self._ser or not self._clock:
            return
        tone_hz = self._tone_hz_for_symbol(symbol)
        freq_dhz = max(0, min(65535, round(tone_hz * 10)))
        now_us = self._clock.now()
        delay_us = max(0, int((target_mono - time.monotonic()) * 1_000_000))
        apply_at = self._clock.to_radio(now_us + delay_us)
        payload = struct.pack(">H", freq_dhz) * FREQ_COPIES + struct.pack(">I", apply_at)
        self._ser.write(_frame(CMD_SET_FREQ, payload))
        self._append_log(
            f"{self._ts()} symbol {symbol_index + 1:02d}/{FT8_SYMBOL_COUNT}: {symbol} -> {tone_hz:.2f} Hz"
        )

    def _send_clicked(self):
        if not self._ser or not self._ser.is_open or not self._clock:
            messagebox.showerror("Error", "Connect the UART first.")
            return

        message = self._message_var.get().strip()
        if not message:
            messagebox.showerror("Error", "Message cannot be empty.")
            return

        try:
            symbols = self._encode_message(message)
        except Exception as exc:
            messagebox.showerror("Error", str(exc))
            return

        now_wall = time.time()
        now_mono = time.monotonic()
        cycle_start_wall = math.floor(now_wall / FT8_CYCLE_SECONDS) * FT8_CYCLE_SECONDS
        cycle_start_mono = now_mono - (now_wall - cycle_start_wall)
        elapsed = now_wall - cycle_start_wall

        if elapsed >= FT8_TX_SECONDS:
            self._status_var.set("Ignored send request: current FT8 TX window has already ended.")
            return

        start_symbol_index = max(0, min(FT8_SYMBOL_COUNT, int(math.ceil(elapsed / FT8_SYMBOL_SECONDS - 1e-9))))
        if start_symbol_index >= FT8_SYMBOL_COUNT:
            self._status_var.set("Ignored send request: no symbols remain in this cycle.")
            return

        if not self._start_radio_tx():
            return

        if self._active_tx is not None:
            self._append_log(f"{self._ts()} interrupted: {self._active_tx.message}")

        self._tx_token += 1
        self._active_tx = ActiveTransmission(
            token=self._tx_token,
            message=message.upper(),
            symbols=symbols,
            cycle_start_wall=cycle_start_wall,
            cycle_start_mono=cycle_start_mono,
            start_symbol_index=start_symbol_index,
            next_symbol_index=start_symbol_index,
        )

        remaining = symbols[start_symbol_index:]
        self._set_remaining_text(
            f"Start at symbol {start_symbol_index + 1}/{FT8_SYMBOL_COUNT}\n"
            + " ".join(str(symbol) for symbol in remaining)
        )
        self._active_var.set(
            f"Active: {message.upper()}  |  remaining {len(remaining)} symbols from #{start_symbol_index + 1}"
        )
        self._status_var.set("Transmission started for the remaining portion of the current FT8 cycle.")
        self._append_log(
            f"{self._ts()} scheduled: {message.upper()} from symbol {start_symbol_index + 1}/{FT8_SYMBOL_COUNT}"
        )
        self._drive_transmission(self._tx_token)

    def _stop_clicked(self):
        if self._active_tx is None and not self._radio_tx_active:
            self._status_var.set("No active transmission.")
            return
        if self._active_tx is not None:
            self._append_log(f"{self._ts()} stopped: {self._active_tx.message}")
        self._active_tx = None
        self._active_var.set("Idle")
        self._set_remaining_text("")
        self._stop_radio_tx()
        self._status_var.set("Transmission stopped.")

    def _drive_transmission(self, token: int):
        tx = self._active_tx
        if tx is None or tx.token != token:
            return

        now_mono = time.monotonic()
        cycle_elapsed = now_mono - tx.cycle_start_mono
        if cycle_elapsed >= FT8_TX_SECONDS:
            self._append_log(f"{self._ts()} cycle ended: {tx.message}")
            self._active_tx = None
            self._active_var.set("Idle")
            self._set_remaining_text("")
            self._stop_radio_tx()
            self._status_var.set("Transmission ended at FT8 cycle boundary.")
            return

        while tx.next_symbol_index < FT8_SYMBOL_COUNT:
            symbol_time = tx.cycle_start_mono + tx.next_symbol_index * FT8_SYMBOL_SECONDS
            if symbol_time < now_mono - 0.02:
                self._append_log(f"{self._ts()} skipped late symbol {tx.next_symbol_index + 1:02d}")
                tx.next_symbol_index += 1
                continue
            if now_mono + SET_FREQ_LEAD_S < symbol_time:
                break
            symbol = tx.symbols[tx.next_symbol_index]
            try:
                self._send_symbol_freq(tx.next_symbol_index, symbol, symbol_time)
            except Exception as exc:
                self._append_log(f"{self._ts()} UART error: {exc}")
                self._active_tx = None
                self._active_var.set("Idle")
                self._set_remaining_text("")
                self._stop_radio_tx()
                self._status_var.set("UART error while sending symbol.")
                return
            tx.next_symbol_index += 1
            tx.emitted_count += 1
            now_mono = time.monotonic()

        if tx.next_symbol_index >= FT8_SYMBOL_COUNT:
            self._set_remaining_text("")
            self._active_var.set(f"Active: {tx.message}  |  final symbol scheduled")
            self._status_var.set("Final FT8 symbol scheduled; waiting for the TX window to end.")
            cycle_end_time = tx.cycle_start_mono + FT8_TX_SECONDS
            delay_ms = max(1, int(round((cycle_end_time - time.monotonic()) * 1000.0)))
            self.root.after(delay_ms, self._drive_transmission, token)
            return

        remaining = tx.symbols[tx.next_symbol_index:]
        self._set_remaining_text(
            f"Next symbol {tx.next_symbol_index + 1}/{FT8_SYMBOL_COUNT}\n"
            + " ".join(str(symbol) for symbol in remaining)
        )
        self._active_var.set(
            f"Active: {tx.message}  |  emitted {tx.emitted_count}, remaining {len(remaining)}"
        )

        next_symbol_time = tx.cycle_start_mono + tx.next_symbol_index * FT8_SYMBOL_SECONDS
        delay_ms = max(1, int(round((next_symbol_time - SET_FREQ_LEAD_S - time.monotonic()) * 1000.0)))
        self.root.after(delay_ms, self._drive_transmission, token)

    def _tick(self):
        now_wall = time.time()
        cycle_start = math.floor(now_wall / FT8_CYCLE_SECONDS) * FT8_CYCLE_SECONDS
        cycle_elapsed = now_wall - cycle_start
        cycle_remaining = FT8_CYCLE_SECONDS - cycle_elapsed
        tx_remaining = max(0.0, FT8_TX_SECONDS - cycle_elapsed)
        self._cycle_var.set(f"Cycle: elapsed {cycle_elapsed:5.2f}s  |  remaining {cycle_remaining:5.2f}s")
        self._window_var.set(f"TX window: remaining {tx_remaining:5.2f}s of {FT8_TX_SECONDS:.2f}s")

        if self._radio_tx_active and self._ser and self._ser.is_open:
            now_mono = time.monotonic()
            if now_mono - self._last_noop_ts >= HEARTBEAT_S:
                try:
                    self._ser.write(NOOP_FRAME)
                    self._last_noop_ts = now_mono
                except Exception:
                    self._status_var.set("UART heartbeat failed.")

        self.root.after(UI_TICK_MS, self._tick)

    def on_close(self):
        self._stop_clicked()
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