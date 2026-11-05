#!/usr/bin/env python3
"""Catalogue of first-generation 406 MHz beacon frames, one per coding option
of C/S T.001 Issue 4 Rev. 11 (October 2023), Annex A.

Every frame is built field by field from the specification, with its BCH codes,
and carries the values a decoder should report: 15-hex ID (bits 26-85, position
bits at their default values for location protocols, section 3.2 / A3.2),
position (arc seconds, N and E positive) and whether the PDF-2 offsets apply.

  t001frames.py            list: name, hex frame, 15-hex ID, position
  t001frames.py NAME       hex frame of one entry

Positions: a test point at 49.07624 N, 0.73018 E (49 04'34" N, 0 43'49" E) and
a south-west point (33 52'08" S, 70 39'28" W) for the sign handling.

NEVER transmit these frames on 406.0-406.1 MHz (distress band).
"""
import sys

G1 = 0b1001101101100111100011   # BCH-1 (82,61), T.001 Annex B
G2 = 0b1010100111001            # BCH-2 (38,26)
SYNC_NORMAL = 0b000101111
SYNC_SELFTEST = 0b011010000

# Modified-Baudot code (Table A3), 6 bits, MSB first
BAUDOT = {
    'A': 0b111000, 'B': 0b110011, 'C': 0b101110, 'D': 0b110010, 'E': 0b110000,
    'F': 0b110110, 'G': 0b101011, 'H': 0b100101, 'I': 0b101100, 'J': 0b111010,
    'K': 0b111110, 'L': 0b101001, 'M': 0b100111, 'N': 0b100110, 'O': 0b100011,
    'P': 0b101101, 'Q': 0b111101, 'R': 0b101010, 'S': 0b110100, 'T': 0b100001,
    'U': 0b111100, 'V': 0b101111, 'W': 0b111001, 'X': 0b110111, 'Y': 0b110101,
    'Z': 0b110001, ' ': 0b100100, '-': 0b011000, '/': 0b010111,
    '0': 0b001101, '1': 0b011101, '2': 0b011001, '3': 0b010000, '4': 0b001010,
    '5': 0b000001, '6': 0b010101, '7': 0b011100, '8': 0b001100, '9': 0b000011,
}

POS = (round(49.07624 * 3600), round(0.73018 * 3600))             # N, E (arc seconds)
SW = (-(33 * 3600 + 52 * 60 + 8), -(70 * 3600 + 39 * 60 + 28))    # S, W
FRANCE = 227


class Msg:
    """Message bits 25..144 (index = bit number), set field by field."""

    def __init__(self, long_msg=True, country=FRANCE):
        self.b = [0] * 145
        self.long = long_msg
        self.b[25] = 1 if long_msg else 0
        self.set(27, 10, country)

    def set(self, first, n, v):
        for i in range(n):
            self.b[first + i] = (v >> (n - 1 - i)) & 1
        return first + n

    def get(self, first, n):
        v = 0
        for i in range(n):
            v = (v << 1) | self.b[first + i]
        return v

    def frame(self, selftest=False):
        """Hex frame: 15 bit-sync ones, frame sync, message with BCH."""
        self.set(86, 21, bch(self.b[25:86], G1, 21))
        last = 144 if self.long else 112
        if self.long:
            self.set(133, 12, bch(self.b[107:133], G2, 12))
        bits = [1] * 15 + [(SYNC_SELFTEST if selftest else SYNC_NORMAL) >> (8 - i) & 1
                           for i in range(9)] + self.b[25:last + 1]
        return "%0*X" % (len(bits) // 4, int("".join(map(str, bits)), 2))


def bch(data, gen, r):
    reg = 0
    for x in list(data) + [0] * r:
        reg = (reg << 1) | x
        if reg >> r:
            reg ^= gen
    return reg


def baudot(s, n=None):
    """Right-justified modified-Baudot string of n characters (space padded)."""
    s = s.rjust(n) if n else s
    v = 0
    for c in s:
        v = (v << 6) | BAUDOT[c]
    return v


def baudot5(s):
    """3-letter designator in the shortened 5-bit Baudot form (MSB dropped)."""
    v = 0
    for c in s:
        v = (v << 5) | (BAUDOT[c] & 0x1F)
    return v


def hex_id(m, base=None, layout=None):
    """15-hex ID: bits 26-85, position bits replaced by their defaults."""
    b = m.b[:]
    if layout:
        n = base
        for width, default in layout:
            for i in range(width):
                b[n + i] = (default >> (width - 1 - i)) & 1
            n += width
    v = int("".join(map(str, b[26:86])), 2)
    return "%015X" % v


# ---- position encoders (A3.3.1: coarse closest to the position, offset = position
# rounded to 4 s minus coarse, sign 1 = plus; zero offset -> sign at its default 1)

def split(a):
    return (1 if a < 0 else 0), abs(a)


def coarse_offset(a, step, max_min):
    """Coarse value (in steps) closest to |a|, and the 4 s rounded offset."""
    s, a = split(a)
    c = int((a + step // 2) // step)
    r = int(round(a / 4.0)) * 4
    off = r - c * step
    assert abs(off) <= max_min * 60 + 56, (a, step, off)
    return s, c, off


def put_offset(m, n, off, mbits):
    sign = 0 if off < 0 else 1
    o = abs(off)
    n = m.set(n, 1, sign)
    n = m.set(n, mbits, o // 60)
    return m.set(n, 4, (o % 60) // 4)


def put_offset_default(m, n, mbits):
    n = m.set(n, 1, 1)
    n = m.set(n, mbits, 0)
    return m.set(n, 4, 0b1111)


def decoded(sign, coarse, step, off):
    v = coarse * step + off
    return -v if sign else v


# ---- protocol builders ------------------------------------------------------

def user(proto, ident40_85, long_msg=False, country=FRANCE, pdf2=None, nonprot=0b010000):
    """User protocol (P=1): bits 40-85 as given (identification + 84-85)."""
    m = Msg(long_msg, country)
    m.set(26, 1, 1)
    m.set(37, 3, proto)
    m.set(40, 46, ident40_85)
    if long_msg:
        m.set(107, 26, pdf2 if pdf2 is not None else 0)
    else:
        m.set(107, 6, nonprot)
    return m


def user_location(proto, ident40_85, pos=POS, internal=1):
    """User-location protocol (A3.3.4): position to 4 minutes in PDF-2."""
    m = user(proto, ident40_85, long_msg=True)
    if pos is None:
        m.set(107, 1, internal)
        m.set(108, 12, 0b0_1111111_0000)
        m.set(120, 13, 0b0_11111111_0000)
        return m, None
    out = []
    n = m.set(107, 1, internal)
    for k, (a, dbits) in enumerate(((pos[0], 7), (pos[1], 8))):
        s, a = split(a)
        q = int((a + 120) // 240)                 # 4-minute steps, rounded
        n = m.set(n, 1, s)
        n = m.set(n, dbits, q // 15)
        n = m.set(n, 4, q % 15)
        out.append(-(q * 240) if s else q * 240)
    return m, tuple(out)


STD_LAYOUT = [(1, 0), (9, 0x1FF), (1, 0), (10, 0x3FF)]
NAT_LAYOUT = [(1, 0), (7, 0x7F), (5, 0), (1, 0), (8, 0xFF), (5, 0)]
HALF_LAYOUT = [(1, 0), (8, 0xFF), (1, 0), (9, 0x1FF)]


def std_location(code, ident24, pos=POS, internal=0, homing=1, fine=True):
    """Standard location protocol (A3.3.5)."""
    m = Msg()
    m.set(37, 4, code)
    m.set(41, 24, ident24)
    m.set(107, 4, 0b1101)
    m.set(111, 1, internal)
    m.set(112, 1, homing)
    if pos is None:
        m.set(65, 21, 0b0_111111111_0_1111111111)
        put_offset_default(m, put_offset_default(m, 113, 5), 5)
        return m, None, False
    (ls, lc, lo), (os_, oc, oo) = (coarse_offset(pos[0], 900, 30), coarse_offset(pos[1], 900, 30))
    n = m.set(65, 1, ls); n = m.set(n, 9, lc); n = m.set(n, 1, os_); m.set(n, 10, oc)
    if fine:
        put_offset(m, put_offset(m, 113, lo, 5), oo, 5)
        return m, (decoded(ls, lc, 900, lo), decoded(os_, oc, 900, oo)), True
    put_offset_default(m, put_offset_default(m, 113, 5), 5)
    return m, (decoded(ls, lc, 900, 0), decoded(os_, oc, 900, 0)), False


def national_location(code, ident18, pos=POS, internal=1, homing=1, fine=True, flag=1):
    """National location protocol (A3.3.6): 2-minute coarse, +/-3'56" offsets."""
    m = Msg()
    m.set(37, 4, code)
    m.set(41, 18, ident18)
    m.set(107, 3, 0b110)
    m.set(110, 1, flag)
    m.set(111, 1, internal)
    m.set(112, 1, homing)
    m.set(127, 6, 0)
    if pos is None:
        m.set(59, 13, 0b0_1111111_00000)
        m.set(72, 14, 0b0_11111111_00000)
        put_offset_default(m, put_offset_default(m, 113, 2), 2)
        return m, None, False
    res = []
    n = 59
    for a, dbits in ((pos[0], 7), (pos[1], 8)):
        s, c, off = coarse_offset(a, 120, 3)
        n = m.set(n, 1, s); n = m.set(n, dbits, c // 30); n = m.set(n, 5, c % 30)
        res.append((s, c, off))
    if fine and flag:
        put_offset(m, put_offset(m, 113, res[0][2], 2), res[1][2], 2)
        return m, tuple(decoded(s, c, 120, o) for s, c, o in res), True
    put_offset_default(m, put_offset_default(m, 113, 2), 2)
    return m, tuple(decoded(s, c, 120, 0) for s, c, o in res), False


def half_location(code, ident26, pdf2_8, pos=POS, fine=True):
    """RLS (A3.3.7) and ELT(DT) (A3.3.8) location: 30-minute coarse, +/-15'56"."""
    m = Msg()
    m.set(37, 4, code)
    m.set(41, 26, ident26)
    m.set(107, 8, pdf2_8)
    if pos is None:
        m.set(67, 19, 0b0_11111111_0_111111111)
        put_offset_default(m, put_offset_default(m, 115, 4), 4)
        return m, None, False
    (ls, lc, lo), (os_, oc, oo) = (coarse_offset(pos[0], 1800, 15), coarse_offset(pos[1], 1800, 15))
    n = m.set(67, 1, ls); n = m.set(n, 8, lc); n = m.set(n, 1, os_); m.set(n, 9, oc)
    if fine:
        put_offset(m, put_offset(m, 115, lo, 4), oo, 4)
        return m, (decoded(ls, lc, 1800, lo), decoded(os_, oc, 1800, oo)), True
    put_offset_default(m, put_offset_default(m, 115, 4), 4)
    return m, (decoded(ls, lc, 1800, 0), decoded(os_, oc, 1800, 0)), False


# ---- identification fields ---------------------------------------------------

def ident_maritime_mmsi(mmsi6="123456", beacon='0', rl=0b01):
    return (baudot(mmsi6, 6) << 10) | (BAUDOT[beacon] << 4) | (0 << 2) | rl


def ident_maritime_callsign(cs="FABC", beacon='0', rl=0b01):
    return (baudot(cs, 6) << 10) | (BAUDOT[beacon] << 4) | rl


def ident_radio_callsign(cs="FAB1234", beacon='0', rl=0b01):
    """First 4 characters Baudot, last 3 BCD (space = 1010), left justified."""
    cs = cs.ljust(7)
    v = 0
    for c in cs[:4]:
        v = (v << 6) | BAUDOT[c]
    for c in cs[4:]:
        v = (v << 4) | (0b1010 if c == ' ' else int(c))
    return (v << 10) | (BAUDOT[beacon] << 4) | rl


def ident_aviation(reg="FGABC", elt=0, rl=0b01):
    return (baudot(reg, 7) << 4) | (elt << 2) | rl


def ident_serial(ttt, serial, cert=None, nat=0, rl=0b01):
    """Serial user (A2.5.1): beacon type, C flag, 20-bit serial, 64-73, 74-83."""
    c = 1 if cert is not None else 0
    tail = cert if cert is not None else 0
    return (ttt << 43) | (c << 42) | (serial << 22) | (nat << 12) | (tail << 2) | rl


def ident_serial_24bit(addr, elt_no=0, cert=None, rl=0b01):
    c = 1 if cert is not None else 0
    return (0b011 << 43) | (c << 42) | (addr << 18) | (elt_no << 12) | ((cert or 0) << 2) | rl


def ident_serial_opdes(op="AFR", serial=1, cert=None, rl=0b01):
    c = 1 if cert is not None else 0
    return (0b001 << 43) | (c << 42) | (baudot(op) << 24) | (serial << 12) | ((cert or 0) << 2) | rl


# ---- the catalogue -----------------------------------------------------------

def catalogue():
    """[(name, description, frame_hex, hex_id, position or None, fine)]"""
    out = []

    def add(name, desc, m, layout=None, base=None, pos=None, fine=False, selftest=False):
        hid = hex_id(m, base, layout)
        out.append((name, desc, m.frame(selftest), hid, pos, fine))

    # Annex B worked example (short serial user, USA): bits 25-112 as printed,
    # BCH-1 recomputed here must match the one in the example
    m = Msg(False)
    ex = bin(int("56E6804002202009655250", 16))[2:].zfill(88)
    m.b[25:113] = [int(x) for x in ex]
    printed_bch = m.get(86, 21)
    assert m.frame() == "FFFE2F56E6804002202009655250", "Annex B BCH-1 mismatch"
    assert m.get(86, 21) == printed_bch
    add("annexb_example", "Annex B example (short serial user, USA 366)", m)

    # A2 user protocols, short messages (non-protected bits 107-112: auto+manual)
    add("u_maritime_mmsi", "Maritime user, MMSI 123456 (A2.2)", user(0b010, ident_maritime_mmsi()))
    add("u_maritime_cs", "Maritime user, call sign FABC (A2.2)", user(0b010, ident_maritime_callsign()))
    add("u_radio_callsign", "Radio call sign user FAB1234 (A2.3)", user(0b110, ident_radio_callsign()))
    add("u_aviation", "Aviation user F-GABC (A2.4)", user(0b001, ident_aviation()))
    add("u_serial_elt", "Serial user, ELT serial, TAC 105 (A2.5.1)",
        user(0b011, ident_serial(0b000, 12345, cert=105)))
    add("u_serial_epirb_ff", "Serial user, float-free EPIRB (A2.5.1)",
        user(0b011, ident_serial(0b010, 54321, cert=242)))
    add("u_serial_epirb_nff", "Serial user, non float-free EPIRB (A2.5.1)",
        user(0b011, ident_serial(0b100, 777, cert=242)))
    add("u_serial_plb", "Serial user, PLB, national serial (A2.5.1)",
        user(0b011, ident_serial(0b110, 4242, nat=0x2A)))
    add("u_serial_24bit", "Serial user, aircraft 24-bit address 3A1B2C (A2.5.2)",
        user(0b011, ident_serial_24bit(0x3A1B2C, elt_no=1, cert=318)))
    add("u_serial_opdes", "Serial user, operator AFR serial 42 (A2.5.3)",
        user(0b011, ident_serial_opdes("AFR", 42, cert=318)))
    add("u_test_short", "Test user, short (A2.6)", user(0b111, 0x123456789AB))
    # a long test user message is the test user-location protocol (A3.3.4): ul_test
    add("u_national_short", "National user, short (A2.8)", user(0b100, 0x2468ACE0246))
    add("u_national_long", "National user, long (A2.8)", user(0b100, 0x2468ACE0246, long_msg=True, pdf2=0x1555555))
    add("u_orbitography", "Orbitography (A2.7)", user(0b000, 0x0F0F0F0F0F0F))
    add("u_maritime_emerg", "Maritime user, emergency code: sinking (A2.9.1)",
        user(0b010, ident_maritime_mmsi(), nonprot=0b110110))
    add("u_aviation_emerg", "Aviation user, emergency: fire + medical (A2.9.2)",
        user(0b001, ident_aviation(), nonprot=0b111100))
    add("u_selftest", "Maritime user, self-test frame", user(0b010, ident_maritime_mmsi()), selftest=True)

    # A3.3.4 user-location protocols (long, position to 4 minutes in PDF-2)
    for name, desc, proto, ident in (
            ("ul_maritime", "User-location, maritime MMSI", 0b010, ident_maritime_mmsi()),
            ("ul_radio_callsign", "User-location, radio call sign", 0b110, ident_radio_callsign()),
            ("ul_aviation", "User-location, aviation F-GABC", 0b001, ident_aviation()),
            ("ul_serial_plb", "User-location, serial PLB", 0b011, ident_serial(0b110, 4242, cert=242)),
            ("ul_test", "User-location, test (protocol 111)", 0b111, 0x123456789AB)):
        m, p = user_location(proto, ident)
        add(name, desc + " (A3.3.4)", m, pos=p)
    m, p = user_location(0b010, ident_maritime_mmsi(), pos=SW)
    add("ul_maritime_sw", "User-location, maritime, S/W position (A3.3.4)", m, pos=p)
    m, p = user_location(0b010, ident_maritime_mmsi(), pos=None)
    add("ul_maritime_nopos", "User-location, maritime, default position (A3.3.4)", m, pos=p)

    # A3.3.5 standard location protocols
    std = (
        ("sl_mmsi", "Standard location, EPIRB MMSI 123456 #1", 0b0010, (123456 << 4) | 1),
        ("sl_24bit", "Standard location, ELT 24-bit address 3A1B2C", 0b0011, 0x3A1B2C),
        ("sl_elt_serial", "Standard location, ELT serial TAC 105 SN 1234", 0b0100, (105 << 14) | 1234),
        ("sl_elt_opdes", "Standard location, ELT operator AFR SN 42", 0b0101, (baudot5("AFR") << 9) | 42),
        ("sl_epirb_serial", "Standard location, EPIRB serial TAC 242 SN 99", 0b0110, (242 << 14) | 99),
        ("sl_plb_serial", "Standard location, PLB serial TAC 242 SN 7", 0b0111, (242 << 14) | 7),
        ("sl_ship_security", "Standard location, ship security MMSI 123456", 0b1100, 123456 << 4),
        ("sl_test", "Standard location, test protocol", 0b1110, 0x123456),
    )
    for name, desc, code, ident in std:
        m, p, f = std_location(code, ident, homing=0 if code == 0b1100 else 1)
        add(name, desc + " (A3.3.5)", m, STD_LAYOUT, 65, p, f)
    m, p, f = std_location(0b0010, (123456 << 4) | 1, pos=SW, internal=1)
    add("sl_mmsi_sw", "Standard location, MMSI, S/W, internal GNSS (A3.3.5)", m, STD_LAYOUT, 65, p, f)
    m, p, f = std_location(0b0010, (123456 << 4) | 1, fine=False)
    add("sl_mmsi_coarse", "Standard location, MMSI, default offsets (A3.3.5)", m, STD_LAYOUT, 65, p, f)
    m, p, f = std_location(0b0010, (123456 << 4) | 1, pos=None)
    add("sl_mmsi_nopos", "Standard location, MMSI, default position (A3.2)", m, STD_LAYOUT, 65, p, f)
    m, p, f = std_location(0b0010, (123456 << 4) | 1, pos=None)
    add("sl_mmsi_selftest", "Standard location, MMSI, self-test (A3.2)", m, STD_LAYOUT, 65, p, f,
        selftest=True)

    # A3.3.6 national location protocols
    for name, desc, code in (("nl_elt", "National location, ELT", 0b1000),
                             ("nl_epirb", "National location, EPIRB", 0b1010),
                             ("nl_plb", "National location, PLB", 0b1011),
                             ("nl_test", "National location, test protocol", 0b1111)):
        m, p, f = national_location(code, 0x2A5A5)
        add(name, desc + " (A3.3.6)", m, NAT_LAYOUT, 59, p, f)
    m, p, f = national_location(0b1010, 0x2A5A5, pos=SW)
    add("nl_epirb_sw", "National location, EPIRB, S/W (A3.3.6)", m, NAT_LAYOUT, 59, p, f)
    m, p, f = national_location(0b1010, 0x2A5A5, flag=0)
    add("nl_epirb_natuse", "National location, EPIRB, bits 113-126 national use (A3.3.6)",
        m, NAT_LAYOUT, 59, p, f)
    m, p, f = national_location(0b1010, 0x2A5A5, pos=None)
    add("nl_epirb_nopos", "National location, EPIRB, default position (A3.2)", m, NAT_LAYOUT, 59, p, f)

    # A3.3.7 RLS location protocol: PDF-2 107-114 = source, homing, RLM 109-112, provider
    # internal source, 121.5 MHz, RLM type-1 accepted, no feedback yet, Galileo
    rls_pdf2 = (1 << 7) | (1 << 6) | (0b10 << 4) | (0b00 << 2) | 0b01
    for name, desc, ident in (
            ("rls_epirb", "RLS location, EPIRB TAC 1042 SN 321", (0b01 << 24) | (42 << 14) | 321),
            ("rls_elt", "RLS location, ELT TAC 2105 SN 55", (0b00 << 24) | (105 << 14) | 55),
            ("rls_plb", "RLS location, PLB TAC 3242 SN 7", (0b10 << 24) | (242 << 14) | 7),
            ("rls_mmsi", "RLS location, MMSI 123456, first EPIRB", (0b00 << 24) | (0b1111 << 20) | 123456),
            ("rls_test", "RLS location test protocol", (0b11 << 24) | (242 << 14) | 1)):
        m, p, f = half_location(0b1101, ident, rls_pdf2)
        add(name, desc + " (A3.3.7)", m, HALF_LAYOUT, 67, p, f)
    m, p, f = half_location(0b1101, (0b01 << 24) | (42 << 14) | 321, rls_pdf2 | (0b11 << 2), pos=SW)
    add("rls_epirb_sw_ack", "RLS location, EPIRB, S/W, RLM type-1+2 received (A3.3.7)",
        m, HALF_LAYOUT, 67, p, f)
    m, p, f = half_location(0b1101, (0b01 << 24) | (42 << 14) | 321, rls_pdf2, pos=None)
    add("rls_epirb_nopos", "RLS location, EPIRB, default position (A3.2)", m, HALF_LAYOUT, 67, p, f)

    # A3.3.8 ELT(DT) location protocol: 107-108 activation, 109-112 altitude, 113-114 freshness
    def eltdt_pdf2(act=0b01, alt=0b0000, fresh=0b11):
        return (act << 6) | (alt << 2) | fresh
    for name, desc, ident, pdf2 in (
            ("eltdt_24bit", "ELT(DT), 24-bit address 3A1B2C, auto, <=400 m, current",
             (0b00 << 24) | 0x3A1B2C, eltdt_pdf2()),
            ("eltdt_opdes", "ELT(DT), operator AFR serial 42, manual, 10 km+, <=60 s",
             (0b01 << 24) | (baudot5("AFR") << 9) | 42, eltdt_pdf2(0b00, 0b1110, 0b10)),
            ("eltdt_tac", "ELT(DT), TAC 318 serial 1234, external, alt n/a, >60 s",
             (0b10 << 24) | (318 << 14) | 1234, eltdt_pdf2(0b10, 0b1111, 0b01)),
            ("eltdt_test", "ELT(DT) location test protocol (bits 43-66 all 0)",
             (0b00 << 24), eltdt_pdf2())):
        m, p, f = half_location(0b1001, ident, pdf2)
        add(name, desc + " (A3.3.8)", m, HALF_LAYOUT, 67, p, f)
    m, p, f = half_location(0b1001, 0x3A1B2C, eltdt_pdf2(), pos=SW)
    add("eltdt_24bit_sw", "ELT(DT), 24-bit address, S/W (A3.3.8)", m, HALF_LAYOUT, 67, p, f)
    # 3LD rotating field: 113-114 = 00, 115-117 = 000, 118-132 = AFR (5-bit Baudot)
    m, p, f = half_location(0b1001, 0x3A1B2C, eltdt_pdf2(fresh=0b00), fine=False)
    m.set(115, 3, 0); m.set(118, 15, baudot5("AFR"))
    add("eltdt_3ld_afr", "ELT(DT), rotating field: operator 3LD AFR (A3.3.8.3)",
        m, HALF_LAYOUT, 67, p, False)
    m, p, f = half_location(0b1001, 0x3A1B2C, eltdt_pdf2(fresh=0b00), fine=False)
    m.set(115, 3, 0); m.set(118, 15, baudot5("ZGA"))
    add("eltdt_3ld_zga", "ELT(DT), rotating field: no 3LD (default ZGA)", m, HALF_LAYOUT, 67, p, False)
    m, p, f = half_location(0b1001, 0x3A1B2C, eltdt_pdf2(fresh=0b01), pos=None)
    add("eltdt_nopos", "ELT(DT), default position (A3.2)", m, HALF_LAYOUT, 67, p, f)
    m, p, f = half_location(0b1001, 0x3A1B2C, eltdt_pdf2(), pos=POS)
    add("eltdt_selftest", "ELT(DT), GNSS self-test with position", m, HALF_LAYOUT, 67, p, f,
        selftest=True)
    # A3.3.8.5 cancellation message: fixed position and PDF-2 sequences
    m = Msg()
    m.set(37, 4, 0b1001); m.set(41, 26, 0x3A1B2C)
    m.set(67, 9, 0b1_11111010); m.set(76, 10, 0b1_111111010)
    m.set(107, 8, 0b00111100); m.set(115, 9, 0b0_1111_0000); m.set(124, 9, 0b0_1111_0000)
    add("eltdt_cancel", "ELT(DT) cancellation message (A3.3.8.5)", m, HALF_LAYOUT, 67, None, False)

    assert out[0][3] == "ADCD0080044040" "1", out[0][3]
    # Annex B Figure B2: 12-bit BCH of the 26 PDF-2 bits 10010101110000000000010111
    assert bch([int(x) for x in "10010101110000000000010111"], G2, 12) == 0b000101010001
    return out


def fmt_pos(p):
    if p is None:
        return "-"
    def one(s):
        a = abs(s)
        return "%s%d.%05d" % ("-" if s < 0 else "", a // 3600, (a % 3600) * 100000 // 3600)
    return "%s, %s" % (one(p[0]), one(p[1]))


if __name__ == "__main__":
    cat = catalogue()
    if len(sys.argv) > 1:
        for e in cat:
            if e[0] == sys.argv[1]:
                print(e[2]); sys.exit(0)
        sys.exit("unknown frame: " + sys.argv[1])
    for name, desc, fr, hid, pos, fine in cat:
        print("%-20s %-36s %s  %s%s" % (name, fr, hid, fmt_pos(pos), "" if fine or pos is None else " (coarse)"))
