#!/usr/bin/env python3
"""AX.25 UI frames for APRS, and their Bell 202 bit stream.

build(src, dst, path, info) -> frame bytes (addresses + 0x03 0xF0 + info + FCS)
hdlc_bits(frame)            -> bits on the air before NRZI: flags, the frame
                               LSB first with bit stuffing, flags
decode(frame)               -> "SRC>DST,PATH:info" (None if the FCS is wrong)

  ax25.py [--call F4HWN]   prints the test frames and their length
"""
import argparse

TXDELAY_FLAGS = 40      # ~267 ms of 0x7E before the frame (1 flag = 6.67 ms)
TAIL_FLAGS = 3


def crc16(data):
    """CRC-16/X.25 (the AX.25 FCS): reflected 0x1021, init/xorout 0xFFFF."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def addr(call, last=False, repeated=False):
    """One 7-byte address field: 6 chars shifted left, then the SSID byte."""
    call, _, ssid = call.upper().rstrip("*").partition("-")
    out = bytes((ord(c) << 1) for c in call.ljust(6)[:6])
    sb = 0x60 | ((int(ssid) if ssid else 0) << 1)
    if repeated:
        sb |= 0x80                          # H bit: this digipeater has relayed it
    if last:
        sb |= 0x01                          # end of the address field
    return out + bytes([sb])


def build(src, dst="APZK5", path=(), info=""):
    hops = list(path)
    f = addr(dst) + addr(src, last=not hops)
    for i, h in enumerate(hops):
        f += addr(h, last=i == len(hops) - 1, repeated=h.endswith("*"))
    f += b"\x03\xF0" + info.encode("latin-1")
    fcs = crc16(f)
    return f + bytes([fcs & 0xFF, fcs >> 8])


def hdlc_bits(frame, lead=TXDELAY_FLAGS, tail=TAIL_FLAGS):
    flag = [(0x7E >> i) & 1 for i in range(8)]
    bits = flag * lead
    ones = 0
    for b in frame:
        for i in range(8):
            bit = (b >> i) & 1
            bits.append(bit)
            ones = ones + 1 if bit else 0
            if ones == 5:
                bits.append(0)              # stuffed zero
                ones = 0
    return bits + flag * tail


def decode(frame):
    """Readable form of a received frame, or None if too short / bad FCS."""
    if len(frame) < 18 or crc16(frame[:-2]) != frame[-2] | (frame[-1] << 8):
        return None
    calls, i = [], 0
    while i + 7 <= len(frame) - 2:
        c = "".join(chr(x >> 1) for x in frame[i:i + 6]).rstrip()
        ssid = (frame[i + 6] >> 1) & 15
        if ssid:
            c += "-%d" % ssid
        if len(calls) >= 2 and frame[i + 6] & 0x80:
            c += "*"
        calls.append(c)
        i += 7
        if frame[i - 1] & 1:
            break
    info = frame[i + 2:-2].decode("latin-1")
    return "%s>%s%s:%s" % (calls[1], calls[0],
                           "".join("," + c for c in calls[2:]), info)


def test_frames(call="F4HWN"):
    """name -> frame bytes. 'badfcs' has one flipped info bit: must be rejected."""
    pos = "!4850.90N/00216.25E-UV-K5 APRS RX test"
    fr = {
        "pos":    build(call, path=["WIDE1-1"], info=pos),
        "digi":   build(call, path=["F1ZZZ-10*", "WIDE2-1"], info=pos),
        "msg":    build(call, path=["WIDE1-1"], info=":%-9s:Hello from the Flipper{01" % call),
        "status": build(call, info=">Flipper Zero AFSK square-wave test"),
        "long":   build(call, path=["WIDE1-1", "WIDE2-2"],
                        info=">" + "".join(chr(0x41 + i % 26) for i in range(200))),
    }
    bad = bytearray(fr["pos"])
    bad[20] ^= 0x04
    fr["badfcs"] = bytes(bad)
    return fr


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--call", default="F4HWN")
    a = ap.parse_args()
    for name, f in test_frames(a.call).items():
        print("%-7s %3d bytes  %s" % (name, len(f), decode(f)))
