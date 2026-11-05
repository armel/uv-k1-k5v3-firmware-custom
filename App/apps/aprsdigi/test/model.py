#!/usr/bin/env python3
"""Host checks for APRS DIGI path rewriting and duplicate identity."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "aprsrx", "test"))
from ax25 import addr, build, crc16, decode, test_frames


def valid(frame):
    return len(frame) >= 18 and crc16(frame) == 0x0F47


def addresses(frame):
    out = []
    pos = 0
    while pos + 7 <= len(frame) - 4:
        field = frame[pos:pos + 7]
        out.append(field)
        pos += 7
        if field[6] & 1:
            return out, pos
    raise ValueError("unterminated address field")


def logical_key(frame):
    fields, end = addresses(frame)
    return frame[:14] + frame[end:-2]


def digipeat(frame, own="F4HWN-3", wide2=False):
    if not valid(frame):
        return None
    fields, end = addresses(frame)
    if frame[end:end + 2] != b"\x03\xf0":
        return None
    own_addr = addr(own)
    target = None
    for index, field in enumerate(fields[2:], 2):
        if field[6] & 0x80 and field[:6] == own_addr[:6] and (field[6] & 0x1E) == (own_addr[6] & 0x1E):
            return None
        if target is None and not field[6] & 0x80:
            target = index
    if target is None:
        return None
    aliases = [addr("WIDE1-1", last=bool(fields[target][6] & 1))]
    if wide2:
        aliases.append(addr("WIDE2-1", last=bool(fields[target][6] & 1)))
    if not any(fields[target][:6] == alias[:6] and
               (fields[target][6] & 0x1E) == (alias[6] & 0x1E)
               for alias in aliases):
        return None

    out = bytearray(frame[:-2])
    off = target * 7
    out[off:off + 6] = own_addr[:6]
    out[off + 6] = (frame[off + 6] & 1) | 0xE0 | (3 << 1)
    fcs = crc16(out)
    out += bytes((fcs & 0xFF, fcs >> 8))
    return bytes(out)


def main():
    frames = test_frames("F1SRC-7")
    pos = digipeat(frames["pos"])
    assert pos is not None and valid(pos)
    assert decode(pos).startswith("F1SRC-7>APZK5,F4HWN-3*:")
    assert logical_key(pos) == logical_key(frames["pos"])

    msg = digipeat(frames["msg"])
    assert msg is not None and logical_key(msg) == logical_key(frames["msg"])

    long_frame = digipeat(frames["long"])
    assert long_frame is not None and ",F4HWN-3*,WIDE2-2:" in decode(long_frame)
    assert len(long_frame) == len(frames["long"])

    assert digipeat(frames["status"]) is None
    assert digipeat(frames["digi"]) is None
    wide2 = digipeat(frames["digi"], wide2=True)
    assert wide2 is not None and valid(wide2)
    assert decode(wide2).startswith("F1SRC-7>APZK5,F1ZZZ-10*,F4HWN-3*:")
    assert logical_key(wide2) == logical_key(frames["digi"])
    assert digipeat(frames["pos"], wide2=True) is not None
    assert digipeat(build("F1SRC-7", path=["WIDE2-2"], info=">two hops"),
                    wide2=True) is None
    assert digipeat(frames["badfcs"]) is None
    assert digipeat(build("F1SRC-7", path=["F4HWN-3*", "WIDE1-1"], info=">loop")) is None
    assert digipeat(build("F1SRC-7", path=["WIDE1-1*"], info=">used")) is None
    assert digipeat(build("F1SRC-7", path=["F4HWN-3*", "WIDE2-1"],
                          info=">loop"), wide2=True) is None

    maximum = build("F1SRC-7", path=["WIDE1-1"] + ["WIDE2-1"] * 7,
                    info="X" * 256)
    maximum_out = digipeat(maximum)
    assert len(maximum) == 330 and maximum_out is not None
    assert len(maximum_out) == 330 and valid(maximum_out)
    assert logical_key(maximum_out) == logical_key(maximum)

    # The logical identity deliberately ignores the path and FCS.
    relayed_again = build("F1SRC-7", path=["F9AAA-2*", "WIDE2-1"],
                          info="!4850.90N/00216.25E-UV-K5 APRS RX test")
    assert logical_key(frames["pos"]) == logical_key(relayed_again)
    print("APRS DIGI model: all checks passed")


if __name__ == "__main__":
    main()
