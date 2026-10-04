#!/usr/bin/env python3
"""
fsk_rtt_tester.py — Bidirectional FSK communication tester & Round-Trip Time (RTT) benchmark
=============================================================================================
This tool operates with two connected Quansheng UV-K1 / UV-K5 V3 radios (CAT variant):
  1. Configures both radios to the target RF frequency (default 433.960 MHz).
  2. Sets the lowest transmit power (PC0 = Low 1, ~20 mW) for safe bench testing.
  3. Configures FSK modems: tests 1200 baud and/or 2400 baud.
  4. Runs bidirectional transmission tests (A -> B, B -> A) and full Round-Trip (A -> B -> A).
  5. Measures exact one-way transit time and full cycle RTT.
  6. Reports and compares performance statistics between 1200 baud and 2400 baud.

Requirements:
    pip install pyserial

Usage:
    python tools/fsk_rtt_tester.py COM16 COM18                 # Test both rates (1200 & 2400)
    python tools/fsk_rtt_tester.py COM16 COM18 --fsk-baud 2400 # 2400 baud only
    python tools/fsk_rtt_tester.py COM16 COM18 --fsk-baud 1200 # 1200 baud only
    python tools/fsk_rtt_tester.py COM16 COM18 --packet-len 16 # Short 16-byte frames
    python tools/fsk_rtt_tester.py COM16 COM18 --count 10      # 10 iterations per test
    python tools/fsk_rtt_tester.py COM16 COM18 --auto-mute     # Auto-mute audio (FE2)
    python tools/fsk_rtt_tester.py --list-ports                # List available COM ports
"""

import sys
import time
import queue
import argparse
import threading
from typing import Optional, Tuple, List, Dict, Any

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("\n  Missing 'pyserial' module. Install via:\n")
    print("      pip install pyserial\n")
    sys.exit(1)


# ─────────────────────────────────────────────
# ANSI Color Codes
# ─────────────────────────────────────────────
class C:
    RESET  = "\033[0m"
    BOLD   = "\033[1m"
    RED    = "\033[91m"
    GREEN  = "\033[92m"
    YELLOW = "\033[93m"
    BLUE   = "\033[94m"
    MAGENTA= "\033[95m"
    CYAN   = "\033[96m"
    GRAY   = "\033[90m"
    WHITE  = "\033[97m"

    @staticmethod
    def ok(s):   return f"{C.GREEN}OK   {s}{C.RESET}"
    @staticmethod
    def fail(s): return f"{C.RED}FAIL {s}{C.RESET}"
    @staticmethod
    def warn(s): return f"{C.YELLOW}WARN {s}{C.RESET}"
    @staticmethod
    def info(s): return f"{C.CYAN}  >> {s}{C.RESET}"
    @staticmethod
    def hdr(s):  return f"{C.BOLD}{C.WHITE}{s}{C.RESET}"


# ─────────────────────────────────────────────
# Asynchronous Radio Worker via CAT
# ─────────────────────────────────────────────
class RadioWorker:
    def __init__(self, name: str, port: str, baud: int = 38400, verbose: bool = False):
        self.name     = name
        self.port     = port
        self.baud     = baud
        self.verbose  = verbose
        self._ser: Optional[serial.Serial] = None
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self.rx_queue: queue.Queue[Tuple[str, float]] = queue.Queue()

    def connect(self):
        self._ser = serial.Serial(
            port          = self.port,
            baudrate      = self.baud,
            bytesize      = 8,
            parity        = 'N',
            stopbits      = 1,
            timeout       = 0.05,
            write_timeout = 2.0,
        )
        time.sleep(0.1)
        self._ser.reset_input_buffer()
        self._running = True
        self._thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._thread.start()

    def disconnect(self):
        self._running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=0.5)
        if self._ser and self._ser.is_open:
            try:
                self._ser.close()
            except Exception:
                pass

    def _reader_loop(self):
        buf = ""
        while self._running and self._ser and self._ser.is_open:
            try:
                n = self._ser.in_waiting
                if n:
                    chunk = self._ser.read(n).decode("ascii", errors="ignore")
                    buf += chunk
                    while ";" in buf:
                        idx = buf.index(";")
                        msg = buf[:idx + 1].strip()
                        buf = buf[idx + 1:]
                        recv_time = time.perf_counter()
                        if msg:
                            if self.verbose:
                                print(f"  {C.GRAY}[{self.name} RX] {msg}{C.RESET}")
                            self.rx_queue.put((msg, recv_time))
                else:
                    time.sleep(0.002)
            except Exception:
                break

    def send(self, cmd: str, inter_char_delay: float = 0.002):
        if not cmd.endswith(";"):
            cmd += ";"
        if self.verbose:
            print(f"  {C.GRAY}[{self.name} TX] {cmd}{C.RESET}")
        for ch in cmd:
            self._ser.write(ch.encode("ascii"))
            self._ser.flush()
            if inter_char_delay > 0:
                time.sleep(inter_char_delay)

    def drain(self):
        while not self.rx_queue.empty():
            try:
                self.rx_queue.get_nowait()
            except queue.Empty:
                break

    def wait_for_packet(self, prefix: str = "FP", timeout: float = 2.5) -> Optional[Tuple[str, float]]:
        """Waits for a frame starting with prefix (e.g. FPA or FPX) and returns (content, recv_time)."""
        deadline = time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            remaining = deadline - time.perf_counter()
            try:
                msg, t_recv = self.rx_queue.get(timeout=max(0.01, remaining))
                if msg.startswith(prefix):
                    return msg, t_recv
            except queue.Empty:
                break
        return None


# ─────────────────────────────────────────────
# Test Suite for a Single Modem Baud Rate
# ─────────────────────────────────────────────
def run_suite_for_baud(radio_a: RadioWorker, radio_b: RadioWorker,
                       fsk_baud: int, count: int, pkt_len: int = 32) -> Dict[str, Any]:
    print(f"\n  {'=' * 66}")
    print(f"  {C.hdr(f'STARTING BENCHMARK: FSK {fsk_baud} BAUD (Frame Length: {pkt_len} B)')}")
    print(f"  {'=' * 66}")

    # Configure FSK on both radios (identical parameters: ABCD sync word, pkt_len)
    sync_word = "ABCD"
    fc_cmd    = f"FC{fsk_baud},{sync_word},{pkt_len};"
    print(f"{C.info(f'Configuring parameters: {fc_cmd}')}")
    radio_a.send(fc_cmd)
    radio_b.send(fc_cmd)
    time.sleep(0.1)

    radio_a.drain()
    radio_b.drain()

    # ── Phase 1: One-way transit test A -> B ──
    print(f"\n  {C.hdr('─' * 66)}")
    print(f"  {C.hdr(f'[{fsk_baud} Baud] PHASE 1: Transit Latency: Radio A -> Radio B')}")
    print(f"  {C.hdr('─' * 66)}")

    def make_payload(prefix: str, seq: int) -> str:
        if pkt_len <= 8:
            return f"{prefix[0]}{seq % 10}"
        elif pkt_len <= 12:
            return f"{prefix[:2]}{seq:02d}"
        elif pkt_len <= 16:
            return f"{prefix[:3]}_{seq}"
        else:
            return f"{prefix}_{fsk_baud}_#{seq}"

    latencies_a_to_b: List[float] = []
    rssi_b_list: List[int] = []

    for i in range(1, count + 1):
        msg_payload = make_payload("A2B", i)
        radio_a.drain()
        radio_b.drain()
        time.sleep(0.08)

        t_send_start = time.perf_counter()
        radio_a.send(f"FTA{msg_payload};")

        resp = radio_b.wait_for_packet("FPA", timeout=2.0)

        if resp:
            raw_msg, t_recv = resp
            latency_ms = (t_recv - t_send_start) * 1000.0
            latencies_a_to_b.append(latency_ms)

            rssi_val = None
            body = raw_msg[3:-1] if raw_msg.endswith(";") else raw_msg[3:]
            if "," in body:
                text_part, rssi_part = body.rsplit(",", 1)
                try:
                    rssi_val = int(rssi_part)
                    rssi_b_list.append(rssi_val)
                except ValueError:
                    pass

            rssi_str = f"{rssi_val} dBm" if rssi_val is not None else "N/A"
            print(f"  {C.GREEN}PASS{C.RESET}  Seq {i:2d}/{count:2d}  "
                  f"Payload: {C.CYAN}{msg_payload:<16}{C.RESET}  "
                  f"Time: {C.BOLD}{latency_ms:6.1f} ms{C.RESET}  "
                  f"RSSI at B: {C.YELLOW}{rssi_str}{C.RESET}")
        else:
            print(f"  {C.RED}FAIL{C.RESET}  Seq {i:2d}/{count:2d}  "
                  f"Payload: {C.CYAN}{msg_payload:<16}{C.RESET}  "
                  f"{C.RED}TIMEOUT (no packet at B){C.RESET}")

        time.sleep(0.12)

    # ── Phase 2: One-way transit test B -> A ──
    print(f"\n  {C.hdr('─' * 66)}")
    print(f"  {C.hdr(f'[{fsk_baud} Baud] PHASE 2: Transit Latency: Radio B -> Radio A')}")
    print(f"  {C.hdr('─' * 66)}")

    latencies_b_to_a: List[float] = []
    rssi_a_list: List[int] = []

    for i in range(1, count + 1):
        msg_payload = make_payload("B2A", i)
        radio_a.drain()
        radio_b.drain()
        time.sleep(0.08)

        t_send_start = time.perf_counter()
        radio_b.send(f"FTA{msg_payload};")

        resp = radio_a.wait_for_packet("FPA", timeout=2.0)

        if resp:
            raw_msg, t_recv = resp
            latency_ms = (t_recv - t_send_start) * 1000.0
            latencies_b_to_a.append(latency_ms)

            rssi_val = None
            body = raw_msg[3:-1] if raw_msg.endswith(";") else raw_msg[3:]
            if "," in body:
                text_part, rssi_part = body.rsplit(",", 1)
                try:
                    rssi_val = int(rssi_part)
                    rssi_a_list.append(rssi_val)
                except ValueError:
                    pass

            rssi_str = f"{rssi_val} dBm" if rssi_val is not None else "N/A"
            print(f"  {C.GREEN}PASS{C.RESET}  Seq {i:2d}/{count:2d}  "
                  f"Payload: {C.CYAN}{msg_payload:<16}{C.RESET}  "
                  f"Time: {C.BOLD}{latency_ms:6.1f} ms{C.RESET}  "
                  f"RSSI at A: {C.YELLOW}{rssi_str}{C.RESET}")
        else:
            print(f"  {C.RED}FAIL{C.RESET}  Seq {i:2d}/{count:2d}  "
                  f"Payload: {C.CYAN}{msg_payload:<16}{C.RESET}  "
                  f"{C.RED}TIMEOUT (no packet at A){C.RESET}")

        time.sleep(0.12)

    # ── Phase 3: Full Round-Trip Cycle (A -> B -> A [Ping-Pong]) ──
    print(f"\n  {C.hdr('─' * 66)}")
    print(f"  {C.hdr(f'[{fsk_baud} Baud] PHASE 3: Round-Trip (Ping-Pong A -> B -> A)')}")
    print(f"  {C.hdr('─' * 66)}")

    rtt_list: List[float] = []

    for i in range(1, count + 1):
        if pkt_len <= 8:
            ping_token = f"P{i % 10}"
            pong_token = f"R{i % 10}"
        elif pkt_len <= 12:
            ping_token = f"PI{i:02d}"
            pong_token = f"PO{i:02d}"
        else:
            ping_token = f"P_{fsk_baud}_{i}"
            pong_token = f"R_{fsk_baud}_{i}"

        radio_a.drain()
        radio_b.drain()
        time.sleep(0.08)

        t_rtt_start = time.perf_counter()

        # Step 1: Radio A sends Ping
        radio_a.send(f"FTA{ping_token};")

        # Step 2: Radio B waits for Ping
        resp_b = radio_b.wait_for_packet("FPA", timeout=2.0)
        if not resp_b:
            print(f"  {C.RED}FAIL{C.RESET}  Seq {i:2d}/{count:2d}  "
                  f"{C.RED}TIMEOUT at Radio B (Ping dropped){C.RESET}")
            continue

        t_b_received = resp_b[1]
        t_leg1_ms = (t_b_received - t_rtt_start) * 1000.0

        # Turnaround delay: time to settle carrier and allow Radio A to switch from TX to RX
        time.sleep(0.04)

        # Step 3: Radio B replies Pong
        radio_b.send(f"FTA{pong_token};")

        # Step 4: Radio A waits for Pong
        resp_a = radio_a.wait_for_packet("FPA", timeout=2.0)
        if not resp_a:
            print(f"  {C.RED}FAIL{C.RESET}  Seq {i:2d}/{count:2d}  "
                  f"{C.RED}TIMEOUT at Radio A (Pong dropped){C.RESET}")
            continue

        t_a_received = resp_a[1]
        total_rtt_ms = (t_a_received - t_rtt_start) * 1000.0
        rtt_list.append(total_rtt_ms)

        print(f"  {C.GREEN}PASS{C.RESET}  Seq {i:2d}/{count:2d}  "
              f"Leg 1 (A->B): {t_leg1_ms:5.1f} ms  "
              f"Total RTT: {C.BOLD}{C.CYAN}{total_rtt_ms:6.1f} ms{C.RESET}")

        time.sleep(0.12)

    return {
        "baud": fsk_baud,
        "a_to_b": latencies_a_to_b,
        "b_to_a": latencies_b_to_a,
        "rtt": rtt_list,
        "rssi_a": rssi_a_list,
        "rssi_b": rssi_b_list,
        "count": count,
    }


# ─────────────────────────────────────────────
# Main Test Routine
# ─────────────────────────────────────────────
def run_fsk_rtt_test(port_a: str, port_b: str, baud: int = 38400,
                     fsk_baud_mode: str = "both", freq_mhz: float = 433.960,
                     count: int = 5, packet_len: int = 32,
                     auto_mute: bool = False, verbose: bool = False):
    freq_hz = int(round(freq_mhz * 1_000_000))
    freq_cmd = f"FA{freq_hz:011d};"
    mute_mode = "2" if auto_mute else "1"
    mute_desc = "Auto-Mute (FE2)" if auto_mute else "Audible (FE1)"

    bauds_to_test = [1200, 2400] if fsk_baud_mode == "both" else [int(fsk_baud_mode)]

    print(f"\n  {'=' * 66}")
    print(f"  {C.hdr('BIDIRECTIONAL FSK COMMUNICATION TEST & RTT BENCHMARK')}")
    print(f"  {'=' * 66}")
    print(f"  Radio A (Port 1)  : {C.CYAN}{port_a}{C.RESET}")
    print(f"  Radio B (Port 2)  : {C.CYAN}{port_b}{C.RESET}")
    print(f"  UART Baudrate     : {C.CYAN}{baud}{C.RESET}")
    print(f"  RF Frequency      : {C.CYAN}{freq_mhz:.4f} MHz ({freq_hz} Hz){C.RESET}")
    print(f"  TX Output Power   : {C.CYAN}PC0 (Low 1, ~20 mW){C.RESET}")
    print(f"  FSK Frame Length  : {C.CYAN}{packet_len} bytes{C.RESET}")
    print(f"  FSK Audio Mode    : {C.CYAN}{mute_desc}{C.RESET}")
    print(f"  Tested FSK Bauds  : {C.CYAN}{', '.join(str(b) for b in bauds_to_test)} baud{C.RESET}")
    print(f"  Test Iterations   : {C.CYAN}{count}{C.RESET}")
    print()

    radio_a = RadioWorker("Radio-A", port_a, baud, verbose)
    radio_b = RadioWorker("Radio-B", port_b, baud, verbose)

    try:
        radio_a.connect()
        print(C.ok(f"Radio A connected ({port_a})"))
    except Exception as e:
        print(C.fail(f"Cannot connect to Radio A ({port_a}): {e}"))
        return False

    try:
        radio_b.connect()
        print(C.ok(f"Radio B connected ({port_b})"))
    except Exception as e:
        print(C.fail(f"Cannot connect to Radio B ({port_b}): {e}"))
        radio_a.disconnect()
        return False

    # ── Initialize Radios ────────────────────────
    print(f"\n{C.info('Configuring receivers and transmitters...')}")
    init_cmds = [
        ("FR0;", "Select VFO A"),
        (freq_cmd, f"Set RF frequency to {freq_mhz:.4f} MHz"),
        ("MD4;", "FM Modulation"),
        ("PC0;", "Minimum TX power (Low 1 ~20mW)"),
        ("SQ3;", "Squelch level 3"),
        ("OF;",  "Disable CTCSS/DCS subtones"),
        ("FM0;", "Disable busy channel lockout (FM0 - always transmit)"),
        (f"FE{mute_mode};", f"Enable FSK receiver ({mute_desc})"),
    ]

    for cmd, desc in init_cmds:
        radio_a.send(cmd)
        radio_b.send(cmd)
        time.sleep(0.04)

    time.sleep(0.2)
    radio_a.drain()
    radio_b.drain()
    print(C.ok("Radios configured and ready."))

    # ── Run Benchmark Suites ──────────────────────
    results_map: Dict[int, Dict[str, Any]] = {}
    try:
        for b in bauds_to_test:
            res = run_suite_for_baud(radio_a, radio_b, b, count, packet_len)
            results_map[b] = res
    finally:
        # ── Cleanup and Shutdown ──────────────────────
        print(f"\n{C.info('Disabling FSK receiver (FE0)...')}")
        try:
            radio_a.send("FE0;")
            radio_b.send("FE0;")
            time.sleep(0.1)
        finally:
            radio_a.disconnect()
            radio_b.disconnect()

    # ─────────────────────────────────────────────
    # Statistical Reports & Summary
    # ─────────────────────────────────────────────
    def calc_stats(vals: List[float]) -> str:
        if not vals:
            return "No data"
        return (f"Min: {min(vals):5.1f} ms | "
                f"Avg: {sum(vals)/len(vals):5.1f} ms | "
                f"Max: {max(vals):5.1f} ms")

    def calc_rssi(vals: List[int]) -> str:
        if not vals:
            return "N/A"
        return f"Avg: {sum(vals)/len(vals):.0f} dBm (Min: {min(vals)} / Max: {max(vals)})"

    for b, res in results_map.items():
        print(f"\n  {'=' * 66}")
        print(f"  {C.hdr(f'STATISTICAL SUMMARY — FSK {b} BAUD')}")
        print(f"  {'=' * 66}")

        ab_vals = res["a_to_b"]
        ba_vals = res["b_to_a"]
        rtt_vals = res["rtt"]

        loss_ab = ((count - len(ab_vals)) / count) * 100
        loss_ba = ((count - len(ba_vals)) / count) * 100
        loss_rtt = ((count - len(rtt_vals)) / count) * 100

        print(f"  Direction A -> B : {len(ab_vals):2d}/{count} received "
              f"({loss_ab:.0f}% loss)  | {calc_stats(ab_vals)}")
        print(f"    Signal at B    : {calc_rssi(res['rssi_b'])}")
        print()
        print(f"  Direction B -> A : {len(ba_vals):2d}/{count} received "
              f"({loss_ba:.0f}% loss)  | {calc_stats(ba_vals)}")
        print(f"    Signal at A    : {calc_rssi(res['rssi_a'])}")
        print()
        print(f"  Round-Trip RTT   : {len(rtt_vals):2d}/{count} completed "
              f"({loss_rtt:.0f}% loss)  | {calc_stats(rtt_vals)}")
        print()

    # ─────────────────────────────────────────────
    # Comparative Benchmark (if both baud rates were tested)
    # ─────────────────────────────────────────────
    if len(results_map) > 1 and 1200 in results_map and 2400 in results_map:
        r12 = results_map[1200]
        r24 = results_map[2400]

        avg_ab_12 = sum(r12["a_to_b"])/len(r12["a_to_b"]) if r12["a_to_b"] else 0
        avg_ab_24 = sum(r24["a_to_b"])/len(r24["a_to_b"]) if r24["a_to_b"] else 0

        avg_ba_12 = sum(r12["b_to_a"])/len(r12["b_to_a"]) if r12["b_to_a"] else 0
        avg_ba_24 = sum(r24["b_to_a"])/len(r24["b_to_a"]) if r24["b_to_a"] else 0

        avg_rtt_12 = sum(r12["rtt"])/len(r12["rtt"]) if r12["rtt"] else 0
        avg_rtt_24 = sum(r24["rtt"])/len(r24["rtt"]) if r24["rtt"] else 0

        print(f"\n  {'=' * 66}")
        print(f"  {C.hdr('SPEED COMPARISON: FSK 1200 BAUD vs 2400 BAUD')}")
        print(f"  {'=' * 66}")
        print(f"  {'Metric':<20} | {'1200 Baud':<12} | {'2400 Baud':<12} | {'Difference / Speedup'}")
        print(f"  {'-'*20}-+-{'-'*12}-+-{'-'*12}-+-{'-'*26}")

        def fmt_cmp(v12: float, v24: float) -> str:
            if v12 > 0 and v24 > 0:
                diff = v12 - v24
                ratio = v12 / v24
                return f"{ratio:4.2f}x faster ({diff:+5.1f} ms)"
            return "N/A"

        print(f"  {'Time A -> B (Avg)':<20} | {avg_ab_12:6.1f} ms    | {avg_ab_24:6.1f} ms    | {C.GREEN}{fmt_cmp(avg_ab_12, avg_ab_24)}{C.RESET}")
        print(f"  {'Time B -> A (Avg)':<20} | {avg_ba_12:6.1f} ms    | {avg_ba_24:6.1f} ms    | {C.GREEN}{fmt_cmp(avg_ba_12, avg_ba_24)}{C.RESET}")
        print(f"  {'Round-Trip RTT':<20} | {avg_rtt_12:6.1f} ms    | {avg_rtt_24:6.1f} ms    | {C.CYAN}{fmt_cmp(avg_rtt_12, avg_rtt_24)}{C.RESET}")
        print()

    all_passed = all(
        len(res["a_to_b"]) == count and
        len(res["b_to_a"]) == count and
        len(res["rtt"]) == count
        for res in results_map.values()
    )

    if all_passed:
        print(f"  {C.ok('All FSK communication tests passed with 100% success!')}")
    else:
        print(f"  {C.warn('Packet loss was observed in some test runs.')}")
    print()

    return all_passed


# ─────────────────────────────────────────────
# Port Discovery Helper
# ─────────────────────────────────────────────
def list_available_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No COM ports available.")
    else:
        print("Available serial COM ports:")
        for p in ports:
            print(f"  {p.device:8}  {p.description}")


# ─────────────────────────────────────────────
# CLI Entry Point
# ─────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Bidirectional FSK communication tester & Round-Trip Time (RTT) benchmark for two UV-K5 / UV-K1 radios",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("port1", nargs="?", help="COM port of Radio A (e.g. COM16)")
    parser.add_argument("port2", nargs="?", help="COM port of Radio B (e.g. COM18)")
    parser.add_argument("--baud", "-b", type=int, default=38400,
                        help="UART baud rate (default: 38400)")
    parser.add_argument("--fsk-baud", choices=["1200", "2400", "both"], default="both",
                        help="FSK modem baud rate (1200, 2400, or both — default: both)")
    parser.add_argument("--packet-len", "-p", type=int, default=32,
                        help="FSK frame length in bytes (default: 32, even integer between 8 and 72, e.g. 8, 12, 16, 32, 64)")
    parser.add_argument("--freq", "-f", type=float, default=433.960,
                        help="Test RF frequency in MHz (default: 433.960)")
    parser.add_argument("--count", "-c", type=int, default=5,
                        help="Number of test iterations (default: 5)")
    parser.add_argument("--auto-mute", "-m", action="store_true",
                        help="Enable Auto-Mute mode (FE2) instead of Audible (FE1)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show verbose log of all transmitted and received CAT frames")
    parser.add_argument("--list-ports", "-l", action="store_true",
                        help="List available COM ports and exit")

    args = parser.parse_args()

    # Enable ANSI color escape codes on Windows console
    if sys.platform == "win32":
        import os
        os.system("")

    if args.packet_len < 8 or args.packet_len > 72 or args.packet_len % 2 != 0:
        print(f"{C.fail('Packet length (--packet-len) must be an even integer between 8 and 72!')}")
        sys.exit(1)

    if args.list_ports:
        list_available_ports()
        return

    if not args.port1 or not args.port2:
        ports = [p.device for p in serial.tools.list_ports.comports()
                 if any(kw in (p.description or "").upper() for kw in ("USB", "CH34", "CP210", "FTDI", "SILABS"))]
        if len(ports) >= 2 and not args.port1 and not args.port2:
            args.port1 = ports[0]
            args.port2 = ports[1]
            print(f"{C.info(f'Auto-detected serial ports: {args.port1} and {args.port2}')}")
        else:
            print(f"{C.fail('Two serial COM ports are required!')}")
            print("Usage: python tools/fsk_rtt_tester.py <PORT_A> <PORT_B>")
            print("Example: python tools/fsk_rtt_tester.py COM16 COM18")
            print()
            list_available_ports()
            sys.exit(1)

    success = run_fsk_rtt_test(
        port_a         = args.port1,
        port_b         = args.port2,
        baud           = args.baud,
        fsk_baud_mode  = args.fsk_baud,
        freq_mhz       = args.freq,
        count          = args.count,
        packet_len     = args.packet_len,
        auto_mute      = args.auto_mute,
        verbose        = args.verbose,
    )
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
