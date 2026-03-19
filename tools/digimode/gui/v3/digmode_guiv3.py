#!/usr/bin/env python3
"""
WSJT-X/JTDX UDP driven FT8 batch scheduler GUI.

This version listens for WSJT-X/JTDX UDP status packets, extracts the current
transmitted FT8 text from the Status packet, and batch-schedules only the
remaining part of the current FT8 slot over the existing UART protocol.
"""

from __future__ import annotations

import base64
import ipaddress
import json
import math
import queue
import socket
import statistics
import struct
import sys
import threading
import time
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from tkinter import messagebox, ttk

import serial
import serial.tools.list_ports
from PyFT8.transmitter import pack_message


WSJT_MAGIC = 0xADBCCBDA

SYNC = 0xAB
CMD_ACK = 0x05
CMD_SYNC_REQ = 0x06
CMD_SYNC_RESP = 0x07
CMD_SCHED_TX = 0x09

POWER_LABELS = ["LOW1", "LOW2", "LOW3", "LOW4", "LOW5", "MID", "HIGH"]

FT8_CYCLE_SECONDS = 15.0
FT8_SYMBOL_COUNT = 79
FT8_SYMBOL_SECONDS = 0.160
FT8_SYMBOL_US = 160_000
FT8_TX_SECONDS = FT8_SYMBOL_COUNT * FT8_SYMBOL_SECONDS
FT8_TONE_STEP_HZ = 6.25

UI_TICK_MS = 100
MAX_ACK_RETRIES = 5
BACKGROUND_SYNC_INTERVAL_S = 5.0
BACKGROUND_SYNC_ROUNDS = 3
SYNC_TIMEOUT_S = 1.0
SYNC_READ_POLL_S = 0.05
DEFAULT_UNICAST_IP = "127.0.0.1"
DEFAULT_MULTICAST_IP = "224.0.0.73"


def _get_capture_path() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().with_name("udp_packets.jsonl")
    return Path(__file__).resolve().with_name("udp_packets.jsonl")


UDP_CAPTURE_PATH = _get_capture_path()


def _crc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
    return crc


def _frame(cmd: int, payload: bytes = b"") -> bytes:
    header = bytes([SYNC, cmd, len(payload)])
    body = header + payload
    return body + bytes([_crc(body)])


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


def _parse_ack(data: bytes) -> tuple[int, bool] | None:
    result = _parse(data)
    if not result or result[0] != CMD_ACK or len(result[1]) < 2:
        return None
    orig_cmd = result[1][0]
    votes = result[1][1:6]
    ok_count = sum(1 for value in votes if value == 0x00)
    return orig_cmd, ok_count >= 3


def _read_qt_utf8(data: bytes, offset: int) -> tuple[str, int]:
    if offset + 4 > len(data):
        raise ValueError("truncated qt string length")
    (length,) = struct.unpack_from(">I", data, offset)
    offset += 4
    if length in (0, 0xFFFFFFFF):
        return "", offset
    if offset + length > len(data):
        raise ValueError("truncated qt string payload")
    raw = data[offset : offset + length]
    offset += length
    return raw.decode("utf-8", errors="replace"), offset


def _maybe_read_qt_utf8(data: bytes, offset: int) -> tuple[str, int, bool]:
    if offset + 4 > len(data):
        return "", offset, False
    value, offset = _read_qt_utf8(data, offset)
    return value, offset, True


def _inspect_udp_packet(data: bytes) -> dict:
    info = {
        "size": len(data),
        "magic": None,
        "schema": None,
        "msg_type": None,
        "instance_id": "",
        "status": None,
    }
    try:
        if len(data) < 12:
            return info
        (magic,) = struct.unpack_from(">I", data, 0)
        info["magic"] = f"0x{magic:08X}"
        if magic != WSJT_MAGIC:
            return info
        (schema,) = struct.unpack_from(">I", data, 4)
        (msg_type,) = struct.unpack_from(">I", data, 8)
        info["schema"] = schema
        info["msg_type"] = msg_type
        instance_id, _ = _read_qt_utf8(data, 12)
        info["instance_id"] = (instance_id or "").strip()
        status = _parse_status_packet(data)
        if status is not None:
            info["status"] = {
                "mode": status.mode,
                "tx_mode": status.tx_mode,
                "sub_mode": status.sub_mode,
                "tx_enabled": status.tx_enabled,
                "transmitting": status.transmitting,
                "decoding": status.decoding,
                "tx_df_hz": status.tx_df_hz,
                "frequency_hz": status.frequency_hz,
                "tx_message": status.tx_message,
                "has_tx_message": bool(status.tx_message),
            }
    except Exception as exc:
        info["inspect_error"] = str(exc)
    return info


@dataclass
class StatusMessage:
    id: str
    schema: int
    frequency_hz: int
    mode: str
    dx_call: str
    report: str
    tx_mode: str
    tx_enabled: bool
    transmitting: bool
    decoding: bool
    rx_df_hz: int
    tx_df_hz: int
    de_call: str
    de_grid: str
    dx_grid: str
    tx_watchdog: bool = False
    sub_mode: str = ""
    fast_mode: bool = False
    special_operation_mode: int = 0
    frequency_tolerance_hz: int | None = None
    tr_period_s: int | None = None
    configuration_name: str = ""
    tx_message: str = ""


def _parse_status_packet(data: bytes) -> StatusMessage | None:
    try:
        if len(data) < 12:
            return None
        (magic,) = struct.unpack_from(">I", data, 0)
        if magic != WSJT_MAGIC:
            return None
        offset = 4
        (schema,) = struct.unpack_from(">I", data, offset)
        offset += 4
        (msg_type,) = struct.unpack_from(">I", data, offset)
        offset += 4
        if msg_type != 1:
            return None

        instance_id, offset = _read_qt_utf8(data, offset)
        (frequency_hz,) = struct.unpack_from(">Q", data, offset)
        offset += 8
        mode, offset = _read_qt_utf8(data, offset)
        dx_call, offset = _read_qt_utf8(data, offset)
        report, offset = _read_qt_utf8(data, offset)
        tx_mode, offset = _read_qt_utf8(data, offset)
        if offset + 3 > len(data):
            return None
        tx_enabled = data[offset] != 0
        transmitting = data[offset + 1] != 0
        decoding = data[offset + 2] != 0
        offset += 3
        if offset + 8 > len(data):
            return None
        (rx_df_hz,) = struct.unpack_from(">I", data, offset)
        offset += 4
        (tx_df_hz,) = struct.unpack_from(">I", data, offset)
        offset += 4
        de_call, offset = _read_qt_utf8(data, offset)
        de_grid, offset = _read_qt_utf8(data, offset)
        dx_grid, offset = _read_qt_utf8(data, offset)

        status = StatusMessage(
            id=(instance_id or "").strip(),
            schema=schema,
            frequency_hz=frequency_hz,
            mode=(mode or "").strip(),
            dx_call=(dx_call or "").strip(),
            report=(report or "").strip(),
            tx_mode=(tx_mode or "").strip(),
            tx_enabled=tx_enabled,
            transmitting=transmitting,
            decoding=decoding,
            rx_df_hz=rx_df_hz,
            tx_df_hz=tx_df_hz,
            de_call=(de_call or "").strip(),
            de_grid=(de_grid or "").strip(),
            dx_grid=(dx_grid or "").strip(),
        )

        if offset < len(data):
            status.tx_watchdog = data[offset] != 0
            offset += 1

        sub_mode, offset, ok = _maybe_read_qt_utf8(data, offset)
        if ok:
            status.sub_mode = (sub_mode or "").strip()
        else:
            return status

        if offset < len(data):
            status.fast_mode = data[offset] != 0
            offset += 1
        else:
            return status

        if offset < len(data):
            status.special_operation_mode = data[offset]
            offset += 1
        else:
            return status

        if offset + 4 <= len(data):
            (freq_tolerance,) = struct.unpack_from(">I", data, offset)
            status.frequency_tolerance_hz = None if freq_tolerance == 0xFFFFFFFF else freq_tolerance
            offset += 4
        else:
            return status

        if offset + 4 <= len(data):
            (tr_period,) = struct.unpack_from(">I", data, offset)
            status.tr_period_s = None if tr_period == 0xFFFFFFFF else tr_period
            offset += 4
        else:
            return status

        configuration_name, offset, ok = _maybe_read_qt_utf8(data, offset)
        if ok:
            status.configuration_name = (configuration_name or "").strip()
        else:
            return status

        tx_message, offset, ok = _maybe_read_qt_utf8(data, offset)
        if ok:
            status.tx_message = (tx_message or "").strip()
        return status
    except (ValueError, struct.error, UnicodeDecodeError):
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

    def sync(self, ser: serial.Serial, rounds: int = 5, max_duration_s: float | None = None) -> bool:
        offsets, rtts = [], []
        total_deadline = time.monotonic() + max_duration_s if max_duration_s else None
        old_timeout = ser.timeout
        ser.timeout = SYNC_READ_POLL_S
        try:
            for _ in range(rounds):
                if total_deadline and time.monotonic() >= total_deadline:
                    raise TimeoutError("clock sync timed out")
                t1 = self.now()
                ser.write(_frame(CMD_SYNC_REQ, struct.pack(">I", t1)))
                buf = b""
                round_deadline = time.monotonic() + 0.5
                if total_deadline:
                    round_deadline = min(round_deadline, total_deadline)
                result = None
                while time.monotonic() < round_deadline:
                    chunk = ser.read(32)
                    if chunk:
                        buf += chunk
                        result = _parse(buf)
                        if result:
                            break
                if total_deadline and time.monotonic() >= total_deadline and not result:
                    raise TimeoutError("clock sync timed out")
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
        finally:
            ser.timeout = old_timeout
        if len(offsets) < 2:
            return False
        self.offset = int(statistics.median(offsets))
        self.rtt = int(statistics.median(rtts))
        return True


class _UdpStatusListener(threading.Thread):
    def __init__(
        self,
        bind_ip: str,
        port: int,
        event_queue: queue.Queue,
        multicast: bool = False,
        capture_path: Path | None = None,
    ):
        super().__init__(daemon=True)
        self._bind_ip = bind_ip.strip()
        self._port = port
        self._event_queue = event_queue
        self._multicast = multicast
        self._capture_path = capture_path
        self._sock: socket.socket | None = None
        self._running = threading.Event()
        self._running.set()

    def _capture_packet(self, data: bytes, addr: tuple[str, int]):
        if not self._capture_path:
            return
        record = {
            "ts": time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime()) + f".{int((time.time() % 1) * 1000):03d}Z",
            "src_ip": addr[0],
            "src_port": addr[1],
            "packet": _inspect_udp_packet(data),
            "payload_b64": base64.b64encode(data).decode("ascii"),
        }
        try:
            with self._capture_path.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(record, ensure_ascii=True) + "\n")
        except OSError as exc:
            self._event_queue.put(("udp-log", f"UDP capture write failed: {exc}"))
            self._capture_path = None

    def _prepare_capture_file(self) -> bool:
        if not self._capture_path:
            return False
        try:
            self._capture_path.parent.mkdir(parents=True, exist_ok=True)
            self._capture_path.touch(exist_ok=True)
            return True
        except OSError as exc:
            self._event_queue.put(("udp-log", f"UDP capture setup failed: {exc}"))
            self._capture_path = None
            return False

    def run(self):
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if self._multicast:
                bind_host = ""
                membership = socket.inet_aton(self._bind_ip) + socket.inet_aton("0.0.0.0")
                self._sock.bind((bind_host, self._port))
                self._sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
                listen_label = f"multicast {self._bind_ip}:{self._port}"
            else:
                bind_host = self._bind_ip or ""
                self._sock.bind((bind_host, self._port))
                listen_label = f"{bind_host or '0.0.0.0'}:{self._port}"
            self._sock.settimeout(0.5)
            self._event_queue.put(("udp-log", f"UDP listening on {listen_label}"))
            if self._prepare_capture_file():
                self._event_queue.put(("udp-log", f"UDP capture enabled: {self._capture_path}"))
        except OSError as exc:
            self._event_queue.put(("udp-error", f"UDP bind failed: {exc}"))
            self._running.clear()
            if self._sock:
                self._sock.close()
                self._sock = None
            return

        while self._running.is_set() and self._sock:
            try:
                data, addr = self._sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            self._capture_packet(data, addr)
            status = _parse_status_packet(data)
            if status is not None:
                self._event_queue.put(("status", status))

        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
        self._event_queue.put(("udp-stopped", None))

    def stop(self):
        self._running.clear()
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("Digmode Control v3")
        self.root.resizable(False, False)

        self._ser: serial.Serial | None = None
        self._clock: _Clock | None = None
        self._uart_port_name = ""
        self._ports = []
        self._udp_listener: _UdpStatusListener | None = None
        self._event_queue: queue.Queue = queue.Queue()
        self._serial_lock = threading.Lock()
        self._sync_thread: threading.Thread | None = None
        self._sync_token = 0
        self._next_background_sync_ts = 0.0
        self._reconnect_in_progress = False

        self._transmitting_by_instance: dict[str, bool] = {}
        self._last_frequency_by_instance: dict[str, int] = {}
        self._last_schedule_key: tuple | None = None
        self._last_tuned_frequency_hz: int | None = None
        self._last_udp_message_ts = 0.0

        self._port_var = tk.StringVar()
        self._udp_ip_var = tk.StringVar(value=DEFAULT_UNICAST_IP)
        self._udp_port_var = tk.StringVar(value="2237")
        self._udp_multicast_var = tk.BooleanVar(value=False)
        self._udp_capture_var = tk.BooleanVar(value=False)
        self._uart_status_var = tk.StringVar(value="Disconnected")
        self._udp_status_var = tk.StringVar(value="UDP listener stopped")
        self._wsjt_status_var = tk.StringVar(value="WSJT-X/JTDX: idle")
        self._context_var = tk.StringVar(value="Dial: -  |  Tx DF: -  |  Instance: -")
        self._message_var = tk.StringVar(value="Last Tx Message: -")
        self._decision_var = tk.StringVar(value="Decision: waiting for TX edge")
        self._pwr_var = tk.IntVar(value=1)

        self._build_ui()
        self._refresh_ports()
        self._tick()

    def _build_ui(self):
        pad = dict(padx=6, pady=4)
        frame = ttk.Frame(self.root, padding=12)
        frame.grid(sticky="nsew")
        frame.columnconfigure(1, weight=1)
        frame.columnconfigure(2, weight=1)

        row = 0
        ttk.Label(frame, text="COM Port:").grid(row=row, column=0, sticky="w", **pad)
        self._port_cb = ttk.Combobox(frame, textvariable=self._port_var, width=36, state="readonly")
        self._port_cb.grid(row=row, column=1, columnspan=2, sticky="ew", **pad)
        ttk.Button(frame, text="↻", width=3, command=self._refresh_ports).grid(row=row, column=3, **pad)
        row += 1

        self._conn_btn = ttk.Button(frame, text="Connect UART", command=self._toggle_conn)
        self._conn_btn.grid(row=row, column=0, columnspan=4, sticky="ew", padx=6, pady=8)
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

        ttk.Separator(frame, orient="horizontal").grid(row=row, column=0, columnspan=4, sticky="ew", pady=6)
        row += 1

        ttk.Label(frame, text="UDP IP:").grid(row=row, column=0, sticky="w", **pad)
        ttk.Entry(frame, textvariable=self._udp_ip_var, width=18).grid(row=row, column=1, sticky="ew", **pad)
        ttk.Label(frame, text="UDP Port:").grid(row=row, column=2, sticky="e", **pad)
        ttk.Entry(frame, textvariable=self._udp_port_var, width=8).grid(row=row, column=3, sticky="w", **pad)
        row += 1

        ttk.Checkbutton(
            frame,
            text="Use Multicast",
            variable=self._udp_multicast_var,
            command=self._on_udp_mode_changed,
        ).grid(row=row, column=0, columnspan=4, sticky="w", padx=6, pady=(0, 6))
        row += 1

        ttk.Checkbutton(
            frame,
            text="Capture UDP to JSONL",
            variable=self._udp_capture_var,
        ).grid(row=row, column=0, columnspan=4, sticky="w", padx=6, pady=(0, 6))
        row += 1

        self._udp_btn = ttk.Button(frame, text="Start UDP Listener", command=self._toggle_udp)
        self._udp_btn.grid(row=row, column=0, columnspan=4, sticky="ew", padx=6, pady=8)
        row += 1

        ttk.Separator(frame, orient="horizontal").grid(row=row, column=0, columnspan=4, sticky="ew", pady=6)
        row += 1

        ttk.Label(frame, textvariable=self._uart_status_var, wraplength=520).grid(
            row=row, column=0, columnspan=4, sticky="w", **pad
        )
        row += 1
        ttk.Label(frame, textvariable=self._udp_status_var, wraplength=520).grid(
            row=row, column=0, columnspan=4, sticky="w", **pad
        )
        row += 1
        ttk.Label(frame, textvariable=self._wsjt_status_var, wraplength=520).grid(
            row=row, column=0, columnspan=4, sticky="w", **pad
        )
        row += 1
        ttk.Label(frame, textvariable=self._context_var, wraplength=520).grid(
            row=row, column=0, columnspan=4, sticky="w", **pad
        )
        row += 1
        ttk.Label(frame, textvariable=self._message_var, wraplength=520).grid(
            row=row, column=0, columnspan=4, sticky="w", **pad
        )
        row += 1
        ttk.Label(frame, textvariable=self._decision_var, wraplength=520).grid(
            row=row, column=0, columnspan=4, sticky="w", **pad
        )
        row += 1

        ttk.Label(frame, text="Scheduled symbols:").grid(row=row, column=0, sticky="nw", padx=6, pady=(10, 4))
        self._symbols_text = tk.Text(frame, width=54, height=6, wrap="word", state="disabled")
        self._symbols_text.grid(row=row, column=1, columnspan=3, sticky="ew", padx=6, pady=(10, 4))
        row += 1

        ttk.Label(frame, text="Log:").grid(row=row, column=0, sticky="nw", padx=6, pady=4)
        self._log_text = tk.Text(frame, width=54, height=16, wrap="none", state="disabled")
        self._log_text.grid(row=row, column=1, columnspan=3, sticky="ew", padx=6, pady=4)
        row += 1

        ttk.Label(frame, text="WSJT-X outgoing text is used only when Status.Tx Message is present.", anchor="w").grid(
            row=row, column=0, columnspan=4, sticky="ew", padx=6, pady=(8, 0)
        )

    def _ts(self) -> str:
        return time.strftime("%H:%M:%S", time.localtime())

    def _append_log(self, line: str):
        self._log_text.config(state="normal")
        self._log_text.insert("end", f"{self._ts()} {line}\n")
        self._log_text.see("end")
        self._log_text.config(state="disabled")

    def _set_symbols_text(self, text: str):
        self._symbols_text.config(state="normal")
        self._symbols_text.delete("1.0", "end")
        self._symbols_text.insert("1.0", text)
        self._symbols_text.config(state="disabled")

    def _set_decision(self, text: str, *, log: bool = True):
        self._decision_var.set(f"Decision: {text}")
        if log:
            self._append_log(text)

    def _refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        self._ports = ports
        items = [f"{port.device} - {port.description}" for port in ports]
        self._port_cb["values"] = items
        if items:
            self._port_cb.current(0)

    def _toggle_conn(self):
        if self._ser and self._ser.is_open:
            self._disconnect()
        else:
            self._connect()

    def _open_uart_port(self, port: str) -> serial.Serial:
        ser = serial.Serial(port, 38400, timeout=0.1)
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(b"\x00" * 8)
        time.sleep(0.1)
        ser.reset_input_buffer()
        return ser

    def _connect(self):
        index = self._port_cb.current()
        if index < 0 or index >= len(self._ports):
            messagebox.showerror("Error", "Please select a COM port.")
            return
        port = self._ports[index].device
        try:
            self._ser = self._open_uart_port(port)
        except Exception as exc:
            self._ser = None
            messagebox.showerror("Error", f"Cannot open {port}:\n{exc}")
            return

        self._uart_status_var.set(f"Connected to {port}; syncing clock...")
        self.root.update()
        self._clock = _Clock()
        self._uart_port_name = port
        self._conn_btn.config(text="Disconnect UART")
        self._port_cb.config(state="disabled")

        sync_state = self._sync_clock(rounds=5, max_duration_s=SYNC_TIMEOUT_S)
        if sync_state == "ok":
            self._uart_status_var.set(
                f"Connected to {port}  |  clock synced  |  RTT {self._clock.rtt}us  |  offset {self._clock.offset}us"
            )
            self._append_log(f"Clock sync OK on {port} (RTT {self._clock.rtt}us, offset {self._clock.offset}us)")
        elif sync_state == "timeout":
            self._append_log(f"Clock sync timed out on {port}; UART link lost, restarting")
            self._restart_uart_after_sync_timeout(port, reason="initial clock sync timeout")
        else:
            self._uart_status_var.set(f"Connected to {port}  |  clock sync failed; zero offset will be used")
            self._append_log(f"Clock sync failed on {port}; continuing with zero offset")
        self._next_background_sync_ts = time.monotonic() + BACKGROUND_SYNC_INTERVAL_S

    def _disconnect(self):
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None
        self._clock = None
        self._uart_port_name = ""
        self._sync_thread = None
        self._sync_token += 1
        self._next_background_sync_ts = 0.0
        self._reconnect_in_progress = False
        self._conn_btn.config(text="Connect UART")
        self._port_cb.config(state="readonly")
        self._uart_status_var.set("Disconnected")
        self._append_log("UART disconnected")

    def _on_power(self, _value):
        self._pwr_lbl.config(text=POWER_LABELS[self._pwr_var.get() - 1])

    def _toggle_udp(self):
        if self._udp_listener and self._udp_listener.is_alive():
            self._stop_udp()
        else:
            self._start_udp()

    def _on_udp_mode_changed(self):
        if self._udp_multicast_var.get():
            self._udp_ip_var.set(DEFAULT_MULTICAST_IP)
        else:
            self._udp_ip_var.set(DEFAULT_UNICAST_IP)

    def _start_udp(self):
        bind_ip = self._udp_ip_var.get().strip()
        port_text = self._udp_port_var.get().strip()
        use_multicast = self._udp_multicast_var.get()
        try:
            port = int(port_text)
            if port < 1 or port > 65535:
                raise ValueError
        except ValueError:
            messagebox.showerror("Error", "UDP port must be in 1..65535.")
            return

        try:
            bind_addr = ipaddress.ip_address(bind_ip)
        except ValueError:
            messagebox.showerror("Error", "UDP IP is not a valid IPv4 address.")
            return

        if bind_addr.version != 4:
            messagebox.showerror("Error", "Only IPv4 UDP addresses are supported.")
            return

        if use_multicast:
            if not bind_addr.is_multicast:
                messagebox.showerror("Error", "Multicast mode requires a multicast IPv4 address.")
                return
        elif bind_addr.is_multicast:
            messagebox.showerror("Error", "Unicast mode does not accept a multicast IP; enable multicast mode first.")
            return

        self._transmitting_by_instance.clear()
        self._last_frequency_by_instance.clear()
        self._last_schedule_key = None
        self._last_tuned_frequency_hz = None
        capture_path = UDP_CAPTURE_PATH if self._udp_capture_var.get() else None
        self._udp_listener = _UdpStatusListener(
            bind_ip,
            port,
            self._event_queue,
            multicast=use_multicast,
            capture_path=capture_path,
        )
        self._udp_listener.start()
        mode_label = "multicast" if use_multicast else "unicast"
        self._udp_status_var.set(f"UDP listener starting in {mode_label} mode on {bind_ip}:{port}...")
        self._udp_btn.config(text="Stop UDP Listener")

    def _stop_udp(self):
        if self._udp_listener:
            self._udp_listener.stop()
            self._udp_listener = None
        self._udp_status_var.set("UDP listener stopped")
        self._udp_btn.config(text="Start UDP Listener")
        self._append_log("UDP listener stopped")

    def _is_uart_ready(self) -> bool:
        return bool(self._ser and self._ser.is_open and self._clock)

    def _sync_clock(self, rounds: int, max_duration_s: float) -> str:
        if not self._ser or not self._clock or not self._ser.is_open:
            return "not-ready"
        with self._serial_lock:
            if not self._ser or not self._clock or not self._ser.is_open:
                return "not-ready"
            try:
                if self._clock.sync(self._ser, rounds=rounds, max_duration_s=max_duration_s):
                    return "ok"
                return "failed"
            except TimeoutError:
                return "timeout"
            except (serial.SerialException, OSError):
                return "lost"

    def _restart_uart_after_sync_timeout(self, port: str, reason: str) -> bool:
        if self._reconnect_in_progress:
            return False

        self._reconnect_in_progress = True
        self._sync_token += 1
        self._sync_thread = None
        self._uart_status_var.set(f"Connected to {port}  |  link lost; restarting...")
        self._append_log(f"UART link lost, restarting {port} after {reason}")

        if self._ser and self._ser.is_open:
            try:
                self._ser.close()
            except Exception:
                pass
        self._ser = None
        self._clock = None

        try:
            self._ser = self._open_uart_port(port)
            self._clock = _Clock()
            self._uart_port_name = port
            sync_state = self._sync_clock(rounds=5, max_duration_s=SYNC_TIMEOUT_S)
        except Exception as exc:
            self._ser = None
            self._clock = None
            self._uart_status_var.set(f"UART link lost; restart failed on {port}")
            self._append_log(f"UART link lost; restart failed on {port}: {exc}")
            self._conn_btn.config(text="Connect UART")
            self._port_cb.config(state="readonly")
            self._reconnect_in_progress = False
            return False

        self._next_background_sync_ts = time.monotonic() + BACKGROUND_SYNC_INTERVAL_S
        self._conn_btn.config(text="Disconnect UART")
        self._port_cb.config(state="disabled")
        self._reconnect_in_progress = False

        if sync_state == "ok":
            self._uart_status_var.set(
                f"Connected to {port}  |  link lost, restarted  |  RTT {self._clock.rtt}us  |  offset {self._clock.offset}us"
            )
            self._append_log(
                f"UART link lost, restarted on {port}; clock sync OK (RTT {self._clock.rtt}us, offset {self._clock.offset}us)"
            )
            return True

        if sync_state == "timeout":
            self._uart_status_var.set(f"Connected to {port}  |  link restarted but clock sync timed out")
            self._append_log(f"UART restarted on {port}, but clock sync still timed out")
        elif sync_state == "lost":
            self._uart_status_var.set(f"Connected to {port}  |  link restarted but device did not respond")
            self._append_log(f"UART restarted on {port}, but device still did not respond")
        else:
            self._uart_status_var.set(f"Connected to {port}  |  link restarted but clock sync failed")
            self._append_log(f"UART restarted on {port}, but clock sync failed")
        return False

    def _start_background_sync(self):
        if self._sync_thread and self._sync_thread.is_alive():
            return
        if not self._is_uart_ready():
            return
        if self._reconnect_in_progress:
            return
        if any(self._transmitting_by_instance.values()):
            self._next_background_sync_ts = time.monotonic() + BACKGROUND_SYNC_INTERVAL_S
            return

        self._next_background_sync_ts = time.monotonic() + BACKGROUND_SYNC_INTERVAL_S
        self._sync_token += 1
        sync_token = self._sync_token

        def worker():
            sync_state = self._sync_clock(rounds=BACKGROUND_SYNC_ROUNDS, max_duration_s=SYNC_TIMEOUT_S)
            clock = self._clock
            port = self._uart_port_name
            self._event_queue.put(
                ("clock-sync", (sync_token, sync_state, port, clock.rtt if clock else 0, clock.offset if clock else 0))
            )

        self._sync_thread = threading.Thread(target=worker, daemon=True)
        self._sync_thread.start()

    def _encode_message(self, message: str) -> list[int]:
        parts = message.upper().split()
        if not 1 <= len(parts) <= 3:
            raise ValueError("Unsupported FT8 text shape for PyFT8; expected 1 to 3 fields.")
        while len(parts) < 3:
            parts.append("")
        symbols = pack_message(parts[0], parts[1], parts[2])
        if not isinstance(symbols, list) or len(symbols) != FT8_SYMBOL_COUNT:
            raise ValueError("PyFT8 did not return a valid 79-symbol FT8 frame.")
        return [int(symbol) for symbol in symbols]

    def _send_sched_tx(self, status: StatusMessage, symbols: list[int], start_at_radio: int) -> int:
        if not self._ser or not self._clock:
            raise RuntimeError("UART is not connected.")

        base_freq_10hz = round(status.frequency_hz / 10)
        tone_list = []
        for symbol in symbols:
            tone_hz = status.tx_df_hz + symbol * FT8_TONE_STEP_HZ
            tone_dhz = max(0, min(65535, round(tone_hz * 10)))
            tone_list.append(tone_dhz)

        payload = struct.pack(">I", base_freq_10hz)
        payload += struct.pack(">I", FT8_SYMBOL_US)
        payload += struct.pack("B", self._pwr_var.get())
        payload += struct.pack(">I", start_at_radio)
        for tone in tone_list:
            payload += struct.pack(">H", tone)

        frame = _frame(CMD_SCHED_TX, payload)
        with self._serial_lock:
            if not self._ser or not self._ser.is_open:
                raise RuntimeError("UART is not connected.")
            for attempt in range(1, MAX_ACK_RETRIES + 1):
                self._ser.reset_input_buffer()
                self._ser.write(frame)
                time.sleep(0.08)
                response = self._ser.read(128)
                ack = _parse_ack(response) if response else None
                if ack and ack[0] == CMD_SCHED_TX and ack[1]:
                    return len(payload)
                if attempt < MAX_ACK_RETRIES:
                    self._append_log(f"SCHED_TX retry {attempt + 1}/{MAX_ACK_RETRIES}")
        raise RuntimeError("CMD_SCHED_TX did not receive an OK ACK.")

    def _is_ft8_status(self, status: StatusMessage) -> bool:
        mode_tokens = [status.mode.upper(), status.tx_mode.upper(), status.sub_mode.upper()]
        return any(token.startswith("FT8") for token in mode_tokens if token)

    def _maybe_retune_rx(self, status: StatusMessage, previous_frequency_hz: int | None):
        if not self._is_uart_ready():
            return
        if status.transmitting or any(self._transmitting_by_instance.values()):
            return
        if previous_frequency_hz == status.frequency_hz and self._last_tuned_frequency_hz == status.frequency_hz:
            return

        try:
            payload_size = self._send_sched_tx(status, [], 0)
        except Exception as exc:
            self._append_log(
                f"RX retune failed  |  dial={status.frequency_hz / 1_000_000:.6f} MHz  |  reason={exc}"
            )
            return

        self._last_tuned_frequency_hz = status.frequency_hz
        self._append_log(
            f"RX retune OK  |  dial={status.frequency_hz / 1_000_000:.6f} MHz  |  payload={payload_size} B"
        )

    def _handle_status(self, status: StatusMessage):
        if not self._is_ft8_status(status):
            return

        self._last_udp_message_ts = time.monotonic()
        self._udp_status_var.set(
            f"UDP listener running  |  schema {status.schema}  |  last packet from {status.id or '-'}"
        )
        mode_label = status.tx_mode or status.mode or "-"
        self._wsjt_status_var.set(
            f"WSJT-X/JTDX: mode {mode_label}  |  tx_enabled={int(status.tx_enabled)}  |  transmitting={int(status.transmitting)}  |  decoding={int(status.decoding)}"
        )
        self._context_var.set(
            f"Dial: {status.frequency_hz / 1_000_000:.6f} MHz  |  Tx DF: {status.tx_df_hz} Hz  |  Instance: {status.id or '-'}"
        )
        self._message_var.set(f"Last Tx Message: {status.tx_message or '-'}")

        previous_frequency_hz = self._last_frequency_by_instance.get(status.id)
        previous = self._transmitting_by_instance.get(status.id)
        self._last_frequency_by_instance[status.id] = status.frequency_hz
        self._transmitting_by_instance[status.id] = status.transmitting

        self._maybe_retune_rx(status, previous_frequency_hz)

        if previous is None and status.transmitting:
            self._set_decision("observed ongoing TX on first sight; waiting for the next TX start edge")
            return

        if previous is False and status.transmitting:
            self._schedule_from_status(status)

    def _schedule_from_status(self, status: StatusMessage):
        if not self._is_uart_ready():
            self._set_decision("ignored TX start because UART is not connected or clock is not synced")
            return

        if not self._is_ft8_status(status):
            self._set_decision(f"ignored TX start for unsupported mode '{status.tx_mode or status.mode or '-'}'")
            return

        tx_message = (status.tx_message or "").strip().upper()
        if not tx_message:
            self._set_decision("ignored TX start because Status.Tx Message is missing in this WSJT-X/JTDX packet")
            return

        try:
            symbols = self._encode_message(tx_message)
        except Exception as exc:
            self._set_decision(f"ignored TX start because PyFT8 could not encode '{tx_message}': {exc}")
            return

        now_wall = time.time()
        now_mono = time.monotonic()
        slot_start_wall = math.floor(now_wall / FT8_CYCLE_SECONDS) * FT8_CYCLE_SECONDS
        slot_start_mono = now_mono - (now_wall - slot_start_wall)
        elapsed = now_wall - slot_start_wall

        if elapsed >= FT8_TX_SECONDS:
            self._set_decision("ignored TX start because the FT8 transmit window has already ended")
            return

        start_symbol_index = max(
            0,
            min(FT8_SYMBOL_COUNT, int(math.ceil(elapsed / FT8_SYMBOL_SECONDS - 1e-9))),
        )
        remaining_symbols = symbols[start_symbol_index:]
        if not remaining_symbols:
            self._set_decision("ignored TX start because no FT8 symbols remain in this slot")
            return

        schedule_key = (
            status.id,
            int(slot_start_wall),
            tx_message,
            start_symbol_index,
            status.frequency_hz,
            status.tx_df_hz,
        )
        if schedule_key == self._last_schedule_key:
            self._set_decision("ignored duplicate TX edge for the current slot")
            return

        symbol_start_mono = slot_start_mono + start_symbol_index * FT8_SYMBOL_SECONDS
        delay_s = max(0.0, symbol_start_mono - now_mono)
        start_at_pc = self._clock.now() + int(round(delay_s * 1_000_000))
        start_at_radio = self._clock.to_radio(start_at_pc)

        try:
            payload_size = self._send_sched_tx(status, remaining_symbols, start_at_radio)
        except Exception as exc:
            self._set_decision(f"failed to schedule batch TX: {exc}")
            return

        self._last_schedule_key = schedule_key
        self._last_tuned_frequency_hz = status.frequency_hz
        first_symbol_time = slot_start_wall + start_symbol_index * FT8_SYMBOL_SECONDS
        self._set_symbols_text(
            f"Message: {tx_message}\n"
            f"Start symbol: {start_symbol_index + 1}/{FT8_SYMBOL_COUNT}\n"
            f"Remaining symbols: {len(remaining_symbols)}\n"
            + " ".join(str(symbol) for symbol in remaining_symbols)
        )
        self._set_decision(
            f"scheduled {len(remaining_symbols)} symbols from #{start_symbol_index + 1} at {time.strftime('%H:%M:%S', time.localtime(first_symbol_time))}"
        )
        self._append_log(
            f"SCHED_TX OK  |  message='{tx_message}'  |  dial={status.frequency_hz / 1_000_000:.6f} MHz  |  tx_df={status.tx_df_hz} Hz  |  start_symbol={start_symbol_index + 1}  |  payload={payload_size} B  |  radio_start={start_at_radio}"
        )

    def _drain_events(self):
        for _ in range(100):
            try:
                kind, payload = self._event_queue.get_nowait()
            except queue.Empty:
                break
            if kind == "status":
                self._handle_status(payload)
            elif kind == "udp-log":
                self._udp_status_var.set(str(payload))
                self._append_log(str(payload))
            elif kind == "udp-error":
                self._udp_status_var.set(str(payload))
                self._append_log(str(payload))
                self._udp_btn.config(text="Start UDP Listener")
                self._udp_listener = None
            elif kind == "udp-stopped":
                if not (self._udp_listener and self._udp_listener.is_alive()):
                    self._udp_status_var.set("UDP listener stopped")
                    self._udp_btn.config(text="Start UDP Listener")
                    self._udp_listener = None
            elif kind == "clock-sync":
                sync_token, sync_state, port, rtt, offset = payload
                if self._sync_thread and not self._sync_thread.is_alive():
                    self._sync_thread = None
                if sync_token != self._sync_token:
                    continue
                if port != self._uart_port_name:
                    continue
                if sync_state == "ok" and self._is_uart_ready():
                    self._uart_status_var.set(
                        f"Connected to {port}  |  clock synced  |  RTT {rtt}us  |  offset {offset}us"
                    )
                    self._append_log(f"Background clock sync OK on {port} (RTT {rtt}us, offset {offset}us)")
                elif sync_state == "timeout":
                    self._append_log(f"Background clock sync timed out on {port}; UART link lost, restarting")
                    self._restart_uart_after_sync_timeout(port, reason="background clock sync timeout")
                elif sync_state == "lost":
                    self._append_log(f"Background clock sync lost device response on {port}; UART link lost, restarting")
                    self._restart_uart_after_sync_timeout(port, reason="background clock sync lost response")
                else:
                    self._append_log(f"Background clock sync failed on {port}")

    def _tick(self):
        self._drain_events()
        if self._last_udp_message_ts > 0.0:
            age = time.monotonic() - self._last_udp_message_ts
            if self._udp_listener and self._udp_listener.is_alive():
                base = self._udp_status_var.get().split("  |  age ")[0]
                self._udp_status_var.set(f"{base}  |  age {age:0.1f}s")
        if self._is_uart_ready() and time.monotonic() >= self._next_background_sync_ts:
            self._start_background_sync()
        self.root.after(UI_TICK_MS, self._tick)

    def on_close(self):
        self._stop_udp()
        self._disconnect()
        self.root.destroy()


def main():
    root = tk.Tk()
    app = App(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()


if __name__ == "__main__":
    main()