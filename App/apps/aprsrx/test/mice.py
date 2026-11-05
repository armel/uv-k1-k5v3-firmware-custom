#!/usr/bin/env python3
"""Mic-E: an encoder written from the APRS 1.0.1 spec (chapter 10), and the
display decoder of aprsrx_app.c transliterated byte for byte (uint8 wrap, no
division), checked against each other and against a real FT3D frame.
Also the standard (uncompressed) position display, against real frames.

  mice.py        runs the tests and prints the screen rows
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MSG = ["Emergency", "Priority", "Special", "Committed",
       "Returning", "In Service", "En Route", "Off Duty"]   # index = bits A B C


# ------------------------------------------------------------ encoder (spec) --

def encode(call, lat, lon, speed_kn=0, course=0, sym="[/", msg=7, custom=False,
           comment="", path=(), marker="`", suffix="_0", alt_m=None):
    """lat/lon in signed decimal degrees -> AX.25 frame bytes."""
    north, east = lat >= 0, lon >= 0
    lat, lon = abs(lat), abs(lon)
    ld = int(lat); lm = (lat - ld) * 60
    lmi = int(lm); lh = int(round((lm - lmi) * 100))
    if lh == 100: lmi, lh = lmi + 1, 0
    digits = "%02d%02d%02d" % (ld, lmi, lh)
    od = int(lon); om = (lon - od) * 60
    omi = int(om); oh = int(round((om - omi) * 100))
    if oh == 100: omi, oh = omi + 1, 0
    offset = od < 10 or od >= 100
    bits = [(msg >> 2) & 1, (msg >> 1) & 1, msg & 1]
    dest = ""
    for k, ch in enumerate(digits):
        d = int(ch)
        if k < 3:
            one = bits[k]
            dest += chr((ord("A") if custom else ord("P")) + d) if one else chr(ord("0") + d)
        elif k == 3:
            dest += chr(ord("P") + d) if north else chr(ord("0") + d)
        elif k == 4:
            dest += chr(ord("P") + d) if offset else chr(ord("0") + d)
        else:
            dest += chr(ord("P") + d) if not east else chr(ord("0") + d)
    if od <= 9:      b1 = od + 118
    elif od <= 99:   b1 = od + 28
    elif od <= 109:  b1 = od + 8
    else:            b1 = od - 72
    b2 = omi + 88 if omi <= 9 else omi + 28
    b3 = oh + 28
    b4 = speed_kn // 10 + 28
    b5 = (speed_kn % 10) * 10 + course // 100 + 28
    b6 = course % 100 + 28
    info = bytes([0x60, b1, b2, b3, b4, b5, b6]) + sym.encode()
    body = comment
    if alt_m is not None:
        v = alt_m + 10000
        body = chr(v // 8281 + 33) + chr(v // 91 % 91 + 33) + chr(v % 91 + 33) + "}" + body
    if marker:
        body = marker + body + suffix
    return _frame(call, dest, path, info + body.encode("latin-1"))


def _frame(call, dest, path, info):
    from ax25 import addr, crc16
    hops = list(path)
    f = addr(dest) + addr(call, last=not hops)
    for i, h in enumerate(hops):
        f += addr(h, last=i == len(hops) - 1)
    f += b"\x03\xF0" + info
    c = crc16(f)
    return f + bytes([c & 0xFF, c >> 8])


# ------------------------------------------- decoder (mirror of aprsrx_app.c) --

def sub(v, d):
    q = 0
    while v >= d:
        v -= d; q += 1
    return q, v

def put2(v):
    q, v = sub(v, 10)
    return chr(48 + q) + chr(48 + v)

def put3(v):
    q, v = sub(v, 100)
    return chr(48 + q) + put2(v)

def mic_digit(c):
    if 0x30 <= c <= 0x39: return c - 0x30
    if 0x41 <= c <= 0x4A: return c - 0x41
    if 0x50 <= c <= 0x59: return c - 0x50
    return 0                                   # K, L, Z: position ambiguity

def mic_bit(c):
    return c >= 0x50 or (0x41 <= c <= 0x4B)

def text_row(o, f, i, end):
    while len(o) < 32 and i < end:
        ch = f[i]
        o += "." if ch < 0x20 or ch > 0x7E else chr(ch)
        i += 1
    return o, i

def is_mice(f, i, end):
    return end - i >= 9 and f[i] in (0x60, 0x27, 0x1C, 0x1D)

def draw_mice(f, i, end):
    c = [f[k] >> 1 for k in range(6)]
    # row 1: position
    o = ""
    for k in range(6):
        o += chr(48 + mic_digit(c[k]))
        if k == 1: o += " "
        if k == 3: o += "."
    o += "N" if mic_bit(c[3]) else "S"
    deg = (f[i + 1] - 28) & 0xFF
    if mic_bit(c[4]): deg += 100
    if 180 <= deg <= 189: deg -= 80
    elif 190 <= deg <= 199: deg -= 190
    mn = (f[i + 2] - 28) & 0xFF
    if mn >= 60: mn -= 60
    o += " " + put3(deg) + " " + put2(mn) + "." + put2((f[i + 3] - 28) & 0xFF)
    o += "W" if mic_bit(c[5]) else "E"
    r1 = o
    # row 2: speed, course, symbol, message
    dc = (f[i + 5] - 28) & 0xFF
    q, dc = sub(dc, 10)
    sp = ((f[i + 4] - 28) & 0xFF) * 10 + q
    crs = dc * 100 + ((f[i + 6] - 28) & 0xFF)
    if sp >= 800: sp -= 800
    if crs >= 400: crs -= 400
    kmh = (sp * 1897) >> 10
    o = "%dkm/h %d " % (kmh, crs) + chr(f[i + 7]) + chr(f[i + 8]) + " "
    idx = mic_bit(c[0]) * 4 + mic_bit(c[1]) * 2 + mic_bit(c[2])
    custom = any(0x41 <= x <= 0x4B for x in c[:3])
    o += ("Custom-" + chr(48 + 7 - idx)) if custom and idx else MSG[idx]
    r2 = o
    # row 3: comment without the device markers, altitude first
    j = i + 9
    while end > j and f[end - 1] in (0x0D, 0x0A): end -= 1
    if j < end:
        m = f[j]
        if m in (0x60, 0x27):
            j += 1
            if end - j >= 2: end -= 2
        elif m in (0x3E, 0x5D):
            j += 1
            if end > j and f[end - 1] in (0x3D, 0x5E): end -= 1
    o = ""
    if end - j >= 4 and f[j + 3] == 0x7D:
        alt = (((f[j] - 33) * 91 + (f[j + 1] - 33)) * 91 + (f[j + 2] - 33)) - 10000
        o = "%dm " % alt
        j += 4
    r3, _ = text_row(o, f, j, end)
    return r1, r2, r3


def safe(ch):
    return "." if ch < 0x20 or ch > 0x7E else chr(ch)

def draw_pos(f, i, end):
    """Uncompressed position ('!' '=' or, after a 7-char timestamp, '/' '@'):
    None when the info field is anything else (shown as raw text)."""
    t = f[i]
    p = i + 1
    if t in (0x2F, 0x40): p += 7
    elif t not in (0x21, 0x3D): return None
    if end < p + 19 or f[p + 4] != 0x2E or f[p + 14] != 0x2E: return None
    q = f[p:]
    o = ""
    for k in range(18):
        if k == 8: continue
        if k in (2, 9, 12): o += " "
        o += safe(q[k])
    r1 = o
    o = ""
    if p > i + 1:
        o += "".join(safe(f[k]) for k in range(i + 1, p)) + " "
    o += safe(q[8]) + safe(q[18]) + " "
    r2, j = text_row(o, f, p + 19, end)
    r3, _ = text_row("", f, j, end)
    return r1, r2, r3


def info_start(f):
    end = len(f) - 2
    e = 13
    while not (f[e] & 1) and e + 7 < end:
        e += 7
    return e + 3, end


# ------------------------------------------------------------------- tests --

def main():
    ok = True
    # Import the generator without writing assets; check every destination code.
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from gen_assets import a, mic_code, MIC_MSG
    blob, fields, _ = a.build()
    offsets = {name: (offset, size) for name, offset, size in fields}
    symbol_offset, symbol_size = offsets["SYM_MAP"]
    for slot in range(symbol_offset, symbol_offset + symbol_size, 3):
        assert not blob[slot] or blob[slot + 1] != 0
    # Verify the serialized front-end state independently of its generator.
    import struct
    init_offset, init_size = offsets["DEMOD_INIT"]
    assert init_size == 116
    words = struct.unpack_from('<29I', blob, init_offset)
    assert words[0] == 32768 and words[9:11] == (64, 64)
    assert all(value == 0 for i, value in enumerate(words) if i not in (0, 9, 10))
    assert offsets["COS1200"][0] == init_offset + init_size
    assert offsets["COS2200"][0] == init_offset + init_size + 8
    assert len(blob) <= 3840
    print(f"Demodulator initial state and table layout OK; assets {len(blob)}/3840 bytes")
    ui_size = dict(a.extra)["UI_SIZE"]
    for name in ("T_MIC", "T_MSG"):
        offset, size = offsets[name]
        assert offset + size <= ui_size
    lut_offset, lut_size = offsets["T_MIC"]
    assert lut_size == 128
    for c in range(128):
        packed = blob[lut_offset + c]
        assert packed == mic_code(c)
        assert packed & 15 == mic_digit(c)
        assert bool(packed & 16) == mic_bit(c)
        assert bool(packed & 32) == (0x41 <= c <= 0x4B)
    print("Mic-E asset lookup: all 128 codes equivalent")
    msg_offset, _ = offsets["T_MSG"]
    stride = dict(a.extra)["T_MSG_STRIDE"]
    for index, message in enumerate(MIC_MSG):
        start = msg_offset + index * stride
        assert blob[start:start + stride].split(b'\0', 1)[0].decode('ascii') == message
    print(f"UI asset read: all messages and lookup covered by {ui_size} bytes")
    # the FT3D frame received on 2026-09-30 (screen: >TXUPX9, `x,5l .[/`_0.)
    ft3d = _frame("F4HWN", "TXUPX9", (), b"`x,5l \x1c[/`_0\r")
    i, end = info_start(ft3d)
    rows = draw_mice(ft3d, i, end)
    print("FT3D  ", rows)
    ok &= rows == ("48 50.89N 002 16.25E", "0km/h 0 [/ Off Duty", "")

    cases = [
        # lat, lon, speed kn, course, msg, custom, alt, comment -> expected rows
        (48.848306, 2.270844, 0, 0, 7, False, None, "", "48 50.90N 002 16.25E"),
        (-33.8688, 151.2093, 25, 271, 6, False, 58, "Hello", "33 52.13S 151 12.56E"),
        (40.7128, -74.0060, 5, 90, 0, False, None, "", "40 42.77N 074 00.36W"),
        (51.5, -0.1276, 799, 359, 3, True, -12, "x", "51 30.00N 000 07.66W"),
        (10.0, 105.5, 12, 0, 5, False, 1234, "", "10 00.00N 105 30.00E"),
        (1.0, -179.99, 100, 180, 1, False, None, "", "01 00.00N 179 59.40W"),
    ]
    for lat, lon, sp, crs, msg, cu, alt, com, want in cases:
        f = encode("F4HWN-7", lat, lon, sp, crs, "[/", msg, cu, com, alt_m=alt)
        i, end = info_start(f)
        r1, r2, r3 = draw_mice(f, i, end)
        kmh = (sp * 1897) >> 10
        m = ("Custom-%d" % (7 - msg)) if cu and msg else MSG[msg]
        w2 = "%dkm/h %d [/ %s" % (kmh, crs, m)
        w3 = ("%dm " % alt if alt is not None else "") + com
        good = (r1, r2, r3) == (want, w2, w3) and all(len(r) <= 32 for r in (r1, r2, r3))
        ok &= good
        print("OK  " if good else "FAIL", (r1, r2, r3), "" if good else (want, w2, w3))
    # standard positions (aprsrx_app.c drawPos)
    pos_cases = [
        ("F1PRY-14", "@300158z4921.07N/00150.50EN APRS OISE V=12.5V",
         ("49 21.07N 001 50.50E", "300158z /N  APRS OISE V=12.5V", "")),
        ("F4HWN", "!4850.90N/00216.25E-UV-K5 APRS RX test",
         ("48 50.90N 002 16.25E", "/- UV-K5 APRS RX test", "")),
        ("F4HWN", "=4850.90S\\12345.67W>" + "x" * 60,
         ("48 50.90S 123 45.67W", "\\> " + "x" * 29, "x" * 31)),
        ("F4HWN", "!/5L!!<*e7>7P[", None),            # compressed: raw text
        ("F4HWN", "!4850.90N/00216.25", None),        # too short
        ("F4HWN", ">status text", None),
    ]
    for call, info, want in pos_cases:
        f = _frame(call, "APZK5", (), info.encode("latin-1"))
        i, end = info_start(f)
        got = draw_pos(f, i, end)
        good = got == want and (got is None or all(len(r) <= 32 for r in got))
        ok &= good
        print("OK  " if good else "FAIL", got, "" if good else want)
    print("ALL OK" if ok else "FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
