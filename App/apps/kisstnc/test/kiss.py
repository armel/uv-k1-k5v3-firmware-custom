#!/usr/bin/env python3
"""KISS framing contract for the TNC.

Mirrors kisstnc_app.c: payload without FCS, CRC-16/X.25 on the air,
escape of 0xC0 and 0xDB, and the parameter commands. Run from this directory.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "aprsrx", "test"))
from ax25 import addr, build, crc16

FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD
PAYLOAD_MAX = 328


def residue(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc


def flag_count(ten_ms, minimum):
    v = ten_ms + (ten_ms >> 1)
    return minimum if v < minimum else v


def wrap(payload, cmd=0x00):
    out = bytearray([FEND, cmd])
    for b in payload:
        if b == FEND:
            out += bytes([FESC, TFEND])
        elif b == FESC:
            out += bytes([FESC, TFESC])
        else:
            out.append(b)
    out.append(FEND)
    return bytes(out)


class Kiss:
    """The host-to-TNC state machine in kisstnc_app.c."""

    def __init__(self):
        self.state = "idle"          # modem: wait / hold / tx
        self.k = "idle"
        self.store = False
        self.cmd = 0
        self.buf = bytearray()
        self.klen = 0
        self.param = 0
        self.pending = b""
        self.txd, self.p, self.slot, self.tail, self.fdx = 30, 255, 10, 3, 0
        self.drops = 0

    def finish(self):
        kind = self.cmd & 0x0F
        if kind == 0:
            if self.store and 15 <= self.klen <= PAYLOAD_MAX:
                body = bytes(self.buf[:self.klen])
                fcs = crc16(body)
                self.pending = body + bytes([fcs & 0xFF, fcs >> 8])
                self.state = "hold"
            elif self.klen:
                self.drops += 1
        elif self.klen:
            v = self.param
            if kind == 1:
                self.txd = v
            elif kind == 2:
                self.p = v
            elif kind == 3:
                self.slot = v
            elif kind == 4:
                self.tail = v
            elif kind == 5:
                self.fdx = 2 if v else 0
        self.store = False
        self.klen = 0

    def byte(self, b):
        if b == FEND:
            if self.k == "esc":
                self.store = False
            if self.k in ("data", "esc"):
                self.finish()
            self.k = "cmd"
            return
        if self.k == "cmd":
            self.cmd = b
            self.klen = 0
            self.buf = bytearray()
            self.store = (b & 0x0F) == 0 and self.state == "wait"
            self.k = "data"
            return
        if self.k == "esc":
            self.k = "data"
            if b == TFEND:
                b = FEND
            elif b == TFESC:
                b = FESC
            else:
                self.store = False
                self.klen = 1000
                return
        elif b == FESC:
            if self.k == "data":
                self.k = "esc"
            return
        elif self.k != "data":
            return
        if self.klen == 0:
            self.param = b
        if self.store:
            if self.klen >= PAYLOAD_MAX:
                self.store = False
            else:
                self.buf.append(b)
        if self.klen < 1000:
            self.klen += 1

    def feed(self, data):
        for b in data:
            self.byte(b)


def unwrap_host(stream):
    """Bytes the TNC sends to the host: one data frame, FCS already removed."""
    assert stream[0] == FEND and stream[1] == 0x00 and stream[-1] == FEND
    raw = bytearray()
    esc = False
    for b in stream[2:-1]:
        if esc:
            raw.append(FEND if b == TFEND else FESC if b == TFESC else b)
            esc = False
        elif b == FESC:
            esc = True
        else:
            raw.append(b)
    return bytes(raw)


def pump_out(payload):
    """kisstnc_app.c kissPump, starting at phase 1."""
    phase, idx, esc = 1, 0, 0
    out = bytearray()
    while phase:
        if phase == 1:
            out.append(FEND)
            phase = 2
        elif phase == 2:
            out.append(0x00)
            phase = 3
            idx = 0
        elif esc:
            out.append(esc)
            esc = 0
            idx += 1
        elif idx >= len(payload):
            out.append(FEND)
            phase = 0
        else:
            b = payload[idx]
            if b == FEND:
                out.append(FESC)
                esc = TFEND
            elif b == FESC:
                out.append(FESC)
                esc = TFESC
            else:
                out.append(b)
                idx += 1
    return bytes(out)


def main():
    aprs = build("F4HWN-7", "APZK5", ("WIDE1-1",), "!4850.90N/00216.25E[KISS")
    assert residue(aprs) == 0xF0B8
    sabm = addr("APZK5") + addr("F4HWN-7", last=True) + b"\x2f"
    sabm += bytes([crc16(sabm) & 0xFF, crc16(sabm) >> 8])
    assert residue(sabm) == 0xF0B8

    for frame in (aprs, sabm):
        body = frame[:-2]
        tnc = Kiss()
        tnc.state = "wait"
        tnc.feed(b"\xC0\xC0" + wrap(body))
        assert tnc.pending == frame, "FCS the TNC adds must match the air frame"
        assert residue(tnc.pending) == 0xF0B8
        assert unwrap_host(pump_out(body)) == body

    messy = bytes([0x00, FEND, FESC, 0xFF]) * 4 + b"hello ax.25!!"
    while len(messy) < 15:
        messy += b"X"
    assert unwrap_host(pump_out(messy)) == messy

    tnc = Kiss()
    tnc.state = "wait"
    tnc.feed(wrap(bytes([30]), 0x01))
    tnc.feed(wrap(bytes([63]), 0x02))
    tnc.feed(wrap(bytes([10]), 0x03))
    tnc.feed(wrap(bytes([4]), 0x04))
    tnc.feed(wrap(bytes([1]), 0x05))
    assert (tnc.txd, tnc.p, tnc.slot, tnc.tail, tnc.fdx) == (30, 63, 10, 4, 2)
    tnc.feed(wrap(bytes([0]), 0x05))
    assert tnc.fdx == 0

    # A frame that arrives while one is already queued is dropped.
    tnc.feed(wrap(aprs[:-2]))
    assert tnc.state == "hold"
    tnc.feed(wrap(sabm[:-2]))
    assert tnc.drops == 1
    assert tnc.pending == aprs

    # Too short to be AX.25, and a dangling escape, both drop.
    tnc.state = "wait"
    tnc.feed(wrap(b"short-frame!!"))          # 13 bytes
    assert tnc.drops == 2 and tnc.state == "wait"
    # A dangling escape after a real byte aborts the frame.
    tnc.feed(bytes([FEND, 0x00, 0x41, FESC, FEND]))
    assert tnc.drops == 3 and tnc.state == "wait"

    assert flag_count(30, 4) == 45            # 300 ms of flags
    assert flag_count(0, 4) == 4
    assert flag_count(3, 2) == 4              # tail 30 ms -> 4 flags
    assert abs(45 * 8 / 1200 - 0.300) < 1e-9

    print("KISS TNC model: all checks passed")


if __name__ == "__main__":
    main()
