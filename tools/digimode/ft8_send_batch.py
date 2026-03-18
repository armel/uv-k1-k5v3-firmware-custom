#!/usr/bin/env python3
"""
One-shot FT8 transmitter for UV-K1/K5V3 digital mode firmware.

Sends "CQ BG7NZL OL63" on 144.174 MHz + 1500 Hz at the next 15-second boundary.
Pure UART control — no audio path needed.
"""

import argparse
import glob
import os
import statistics
import struct
import sys
import time

import serial


def find_aioc_port():
    """Auto-detect AIOC serial port via /dev/serial/by-id/."""
    for link in glob.glob('/dev/serial/by-id/*AIOC*'):
        return os.path.realpath(link)
    return None

# ---- UART Protocol (inlined) ----

SYNC = 0xAB

def xor_crc(data):
    c = 0
    for b in data:
        c ^= b
    return c

CMD_NOOP = 0x08

def build_frame(cmd, payload=b''):
    hdr = bytes([SYNC, cmd, len(payload)])
    body = hdr + payload
    return body + bytes([xor_crc(body)])

NOOP_FRAME = build_frame(CMD_NOOP)

def parse_one(data):
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
        if xor_crc(data[:fs-1]) != data[fs-1]:
            data = data[1:]
            continue
        return data[1], data[3:3+length], data[fs:]
    return None

def parse_ack(data):
    """Parse ACK frame → (orig_cmd, is_ok) or None.
    Result byte is repeated 5 times; majority vote handles bit flips."""
    r = parse_one(data)
    if not r or r[0] != 0x05 or len(r[1]) < 2:
        return None
    orig_cmd = r[1][0]
    results = r[1][1:6]
    ok_count = sum(1 for b in results if b == 0x00)
    return orig_cmd, ok_count >= 3

# ---- FT8 Constants ----

FT8_SYMBOL_US  = 160_000          # 160 ms per symbol
FT8_TONE_STEP  = 6.25             # Hz between tones
BASE_AUDIO_HZ  = 1500.0
BASE_FREQ_MHZ  = 144.174
BASE_FREQ_10HZ = round(BASE_FREQ_MHZ * 100_000)  # 14417400

SYMBOLS = [
    3,1,4,0,6,5,2,0,0,0,0,0,0,0,0,1,0,6,6,0,4,5,2,1,0,6,1,5,4,7,4,4,
    2,7,3,7,3,1,4,0,6,5,2,3,3,7,2,2,7,5,5,6,6,6,2,0,3,2,3,7,6,4,0,0,
    7,3,0,4,0,1,7,4,3,1,4,0,6,5,2,
]
assert len(SYMBOLS) == 79

# ---- NTP-like Clock Sync ----

class Clock:
    def __init__(self):
        self.offset = 0
        self.rtt = 0
        self._t0 = time.monotonic()

    def now(self):
        return int((time.monotonic() - self._t0) * 1_000_000) & 0xFFFFFFFF

    def to_radio(self, pc_us):
        return (pc_us + self.offset) & 0xFFFFFFFF

    def sync(self, ser, rounds=7):
        offsets, rtts = [], []
        for _ in range(rounds):
            t1 = self.now()
            ser.write(build_frame(0x06, struct.pack('>I', t1)))
            buf = b''
            deadline = time.monotonic() + 0.5
            result = None
            while time.monotonic() < deadline:
                chunk = ser.read(32)
                if chunk:
                    buf += chunk
                    result = parse_one(buf)
                    if result:
                        break
            if not result:
                continue
            t3 = self.now()
            cmd, payload, _ = result
            if cmd != 0x07 or len(payload) < 8:
                continue
            echo = struct.unpack('>I', payload[:4])[0]
            t2   = struct.unpack('>I', payload[4:8])[0]
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
        self.rtt    = int(statistics.median(rtts))
        return True

HEARTBEAT_INTERVAL = 0.08  # 80ms between NOOPs (well within 1s timeout)

POWER_NAMES = ['USER', 'LOW1', 'LOW2', 'LOW3', 'LOW4', 'LOW5', 'MID', 'HIGH']

# ---- Main ----

def main():
    ap = argparse.ArgumentParser(description='One-shot FT8 TX via UV-K1/K5V3 digmode')
    ap.add_argument('-p', '--port', default=None,
                    help='Serial port (auto-detects AIOC if omitted)')
    ap.add_argument('-b', '--baud', type=int, default=38400)
    ap.add_argument('--power', type=str, default=None,
                    help='TX power: LOW1-LOW5, MID, HIGH, or 0-7 (default: current VFO)')
    ap.add_argument('--offset', type=float, default=0.5,
                    help='TX start offset within slot, seconds (default: 0.5)')
    args = ap.parse_args()

    port = args.port
    if port is None:
        port = find_aioc_port()
        if port is None:
            print("[ft8] ERROR: no AIOC found, specify --port manually")
            sys.exit(1)
    baud = args.baud

    power_byte = 0xFF
    if args.power is not None:
        p = args.power.upper()
        if p in POWER_NAMES:
            power_byte = POWER_NAMES.index(p)
        elif p.isdigit() and 0 <= int(p) <= 7:
            power_byte = int(p)
        else:
            print(f"[ft8] ERROR: invalid power '{args.power}'. Use LOW1-LOW5, MID, HIGH, or 0-7")
            sys.exit(1)

    print(f"[ft8] Port: {port} @ {baud}")
    ser = serial.Serial(port, baud, timeout=0.1)
    time.sleep(0.3)
    ser.reset_input_buffer()
    ser.write(b'\x00' * 8)
    time.sleep(0.1)
    ser.reset_input_buffer()

    def heartbeat_sleep(duration):
        """Sleep while sending NOOP heartbeats every ~80ms."""
        deadline = time.monotonic() + duration
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            time.sleep(min(remaining, HEARTBEAT_INTERVAL))
            ser.write(NOOP_FRAME)

    # Clock sync (enters digmode / USB RX)
    clock = Clock()
    print("[ft8] NTP sync...")
    if clock.sync(ser):
        print(f"[ft8] OK: offset={clock.offset} us, RTT={clock.rtt} us")
    else:
        print("[ft8] WARN: sync failed, zero offset")

    tx_offset = args.offset

    # Pre-compute tone frequencies (0.1 Hz units)
    tone_dhz = [round(BASE_AUDIO_HZ * 10 + t * FT8_TONE_STEP * 10) for t in range(8)]
    freq_list = [tone_dhz[sym] for sym in SYMBOLS]

    # Compute next 15-second slot + offset
    now_wall = time.time()
    slot = (now_wall // 15 + 1) * 15
    tx_start_wall = slot + tx_offset
    wait = tx_start_wall - now_wall
    slot_str = time.strftime('%H:%M:%S', time.localtime(tx_start_wall))
    print(f"[ft8] TX start: {slot_str} (in {wait:.1f}s, offset +{tx_offset}s)")

    # Convert TX start to radio-local timestamp via synced clock
    start_at_pc = clock.now() + int(wait * 1_000_000)
    start_at_radio = clock.to_radio(start_at_pc)

    # Build SCHED_TX: base(4B) + interval(4B) + power(1B) + start_at(4B) + freq(2B×N)
    CMD_SCHED_TX = 0x09
    pwr = power_byte if power_byte <= 7 else 0xFF
    pwr_str = POWER_NAMES[power_byte] if power_byte <= 7 else 'VFO'

    sched_payload  = struct.pack('>I', BASE_FREQ_10HZ)
    sched_payload += struct.pack('>I', FT8_SYMBOL_US)
    sched_payload += struct.pack('B', pwr)
    sched_payload += struct.pack('>I', start_at_radio)
    for f in freq_list:
        sched_payload += struct.pack('>H', f)

    print(f"[ft8] >>> SCHED_TX  {BASE_FREQ_MHZ} MHz  +{BASE_AUDIO_HZ} Hz  power={pwr_str}")
    print(f"[ft8]     {len(SYMBOLS)} symbols, {FT8_SYMBOL_US/1000:.0f}ms each, "
          f"payload {len(sched_payload)}B")
    print(f"[ft8]     start_at(radio): {start_at_radio} us")

    # Send schedule with retry — firmware replies ACK(OK) or ACK(ERR) on CRC fail
    MAX_RETRIES = 5
    accepted = False
    sched_frame = build_frame(CMD_SCHED_TX, sched_payload)
    for attempt in range(MAX_RETRIES):
        ser.reset_input_buffer()
        ser.write(sched_frame)
        time.sleep(0.08)
        resp = ser.read(128)
        if resp:
            ack = parse_ack(resp)
            if ack:
                orig_cmd, is_ok = ack
                if is_ok:
                    print(f"[ft8] ACK OK (attempt {attempt + 1})")
                    accepted = True
                    break
                else:
                    print(f"[ft8] NAK (CRC error), retry {attempt + 2}/{MAX_RETRIES}...")
                    continue
        if attempt < MAX_RETRIES - 1:
            print(f"[ft8] No response, retry {attempt + 2}/{MAX_RETRIES}...")

    if not accepted:
        print("[ft8] ERROR: SCHED_TX failed after all retries")
        ser.close()
        sys.exit(1)

    # Wait for firmware to start + play all symbols
    total_s = len(SYMBOLS) * FT8_SYMBOL_US / 1_000_000
    print(f"[ft8] Firmware will play {len(SYMBOLS)} symbols ({total_s:.2f}s)")

    # Show countdown until TX start, then progress
    while True:
        now = time.time()
        if now < tx_start_wall:
            remain = tx_start_wall - now
            print(f"  Waiting... TX in {remain:.1f}s", end='\r')
            time.sleep(0.2)
            continue

        elapsed_tx = now - tx_start_wall
        if elapsed_tx >= total_s + 0.5:
            break

        idx = min(int(elapsed_tx / (FT8_SYMBOL_US / 1_000_000)), len(SYMBOLS) - 1)
        sym = SYMBOLS[idx]
        f_hz = BASE_AUDIO_HZ + sym * FT8_TONE_STEP
        rf = BASE_FREQ_MHZ + f_hz / 1_000_000
        bar = '#' * (sym + 1)
        print(f"  [{idx+1:2d}/79] tone={sym} AF={f_hz:7.2f} Hz  RF={rf:.6f} MHz  {bar}",
              end='\r')
        time.sleep(0.2)
    print()

    ser.close()
    print("[ft8] Done!")

if __name__ == '__main__':
    main()
