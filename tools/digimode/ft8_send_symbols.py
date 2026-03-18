#!/usr/bin/env python3
"""
Symbol-by-symbol FT8 transmitter for UV-K1/K5V3 digital mode firmware.

Streams SET_FREQ commands in real time — the PC controls symbol timing.
Use ft8_send_batch.py for autonomous scheduled playback (recommended).

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
    ap = argparse.ArgumentParser(
        description='Symbol-by-symbol FT8 TX via UV-K1/K5V3 digmode (streaming mode)')
    ap.add_argument('-p', '--port', default=None,
                    help='Serial port (auto-detects AIOC if omitted)')
    ap.add_argument('-b', '--baud', type=int, default=38400)
    ap.add_argument('--power', type=str, default=None,
                    help='TX power: LOW1-LOW5, MID, HIGH, or 0-7 (default: current VFO)')
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

    clock = Clock()
    print("[ft8] NTP sync...")
    if clock.sync(ser):
        print(f"[ft8] OK: offset={clock.offset} us, RTT={clock.rtt} us")
    else:
        print("[ft8] WARN: sync failed, zero offset")

    # Compute next 15-second boundary
    now_wall = time.time()
    slot = (now_wall // 15 + 1) * 15
    wait = slot - now_wall
    slot_str = time.strftime('%H:%M:%S', time.localtime(slot))
    print(f"[ft8] Next slot: {slot_str} (in {wait:.1f}s)")

    pc_slot = clock.now() + int(wait * 1_000_000)

    # Pre-compute tone frequencies (0.1 Hz units)
    tone_dhz = [round(BASE_AUDIO_HZ * 10 + t * FT8_TONE_STEP * 10) for t in range(8)]

    # Pre-build all SET_FREQ frames with radio-local timestamps
    frames = []
    for i, sym in enumerate(SYMBOLS):
        apply_at_pc = pc_slot + i * FT8_SYMBOL_US
        apply_at_radio = clock.to_radio(apply_at_pc)
        freq = tone_dhz[sym]
        payload = struct.pack('>H', freq) * 5 + struct.pack('>I', apply_at_radio)
        frames.append(build_frame(0x03, payload))

    # Wait until 1 second before slot — send heartbeats while waiting
    heartbeat_sleep(max(0, wait - 1.0))

    # START_TX
    pwr_str = POWER_NAMES[power_byte] if power_byte <= 7 else 'VFO'
    print(f"[ft8] >>> START TX  {BASE_FREQ_MHZ} MHz  +{BASE_AUDIO_HZ} Hz  power={pwr_str}")
    start_payload = struct.pack('>I', BASE_FREQ_10HZ)
    if power_byte <= 7:
        start_payload += struct.pack('B', power_byte)
    ser.write(build_frame(0x01, start_payload))
    time.sleep(0.05)
    ack = ser.read(64)
    if ack:
        r = parse_one(ack)
        if r and r[0] == 0x05:
            print(f"[ft8] ACK: {'OK' if r[1][1]==0 else 'ERR'}")

    # Stream SET_FREQ — pace at ~1 frame per 160ms to keep FIFO shallow
    PIPELINE_US = 300_000
    for i, frame in enumerate(frames):
        target_send_pc = pc_slot + i * FT8_SYMBOL_US - PIPELINE_US
        now_pc = clock.now()
        delta = ((target_send_pc - now_pc) & 0xFFFFFFFF)
        if delta < 0x7FFFFFFF and delta > 0:
            wait_s = delta / 1_000_000
            if wait_s > HEARTBEAT_INTERVAL:
                heartbeat_sleep(wait_s)
            else:
                time.sleep(wait_s)

        ser.write(frame)

        sym = SYMBOLS[i]
        f_hz = BASE_AUDIO_HZ + sym * FT8_TONE_STEP
        rf = BASE_FREQ_MHZ + f_hz / 1_000_000
        bar = '#' * (sym + 1)
        print(f"  [{i+1:2d}/79] tone={sym} AF={f_hz:7.2f} Hz  RF={rf:.6f} MHz  {bar}", end='\r')

    # Wait for last symbol to finish — keep heartbeating
    end_time = slot + len(SYMBOLS) * FT8_SYMBOL_US / 1_000_000
    remain = end_time - time.time() + 0.2
    if remain > 0:
        print(f"\n[ft8] Transmitting... {remain:.1f}s remaining")
        heartbeat_sleep(remain)

    # STOP_TX — retry until ACK or max attempts (EMF may corrupt frames)
    print("[ft8] <<< STOP TX")
    for attempt in range(10):
        ser.write(build_frame(0x02))
        time.sleep(0.15)
        resp = ser.read(64)
        if resp:
            r = parse_one(resp)
            if r and r[0] == 0x05 and len(r[1]) >= 2 and r[1][0] == 0x02:
                print(f"[ft8] STOP ACK received (attempt {attempt+1})")
                break
        if attempt < 9:
            print(f"[ft8] STOP retry {attempt+2}/10...")
    else:
        print("[ft8] WARN: no STOP ACK after 10 attempts")

    ser.close()
    print("[ft8] Done!")

if __name__ == '__main__':
    main()
