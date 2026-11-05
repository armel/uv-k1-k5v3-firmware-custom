#!/usr/bin/env python3
"""VHF DSC (Digital Selective Calling, ITU-R M.493) reference: the call
sequence built and decoded bit by bit, and the fields of the calls DSC RX
shows.

  ten-unit code   the ATIS one (atisrx/test/atis.py): 7 information bits, least
                  significant first, then the number of B (0) elements among
                  them on 3 bits, most significant first
  sequence        20 dot bits, then DX/RX pairs sent alternately:
                    DX 125 x6   A  A  I1 I2 ... In  EOS ECC EOS EOS
                    RX 111..104       A  A  I1 ...  In  EOS ECC
                  A = format specifier, I1..In the call's characters, EOS the
                  end of sequence (117 acknowledgement requested, 122
                  acknowledgement given, 127 other), ECC the XOR of A (once),
                  I1..In and EOS. Each character is sent twice, the RX copy 5
                  symbols after the DX one (2 pairs later, as in ATIS).
  call            format specifier, then by format (M.493 annex 1):
                    112 distress    self ID, nature, position, time, subsequent comm.
                    116 all ships   category, self ID, telecommand 1, then
                                    distress ack / relay (category 112, telecommand
                                    110 / 112): distressed ID, nature, position,
                                    time, subsequent comm.; otherwise telecommand
                                    2, then a frequency or position message
                    120 individual  address (MMSI), then as 116
                    114 group       address (group MMSI), then as 116
                    102 area        address (area), then as 116
                    123 automatic   address, then as 116
  MMSI            9 digits sent as 5 symbols, the tenth digit 0
  position        10 digits: quadrant (0 NE, 1 NW, 2 SE, 3 SW), latitude
                  degrees and minutes (2 + 2), longitude (3 + 2); ten 9 when
                  unknown. Time: HHMM UTC, 8888 when unknown.

  dsc.py          self-test
"""
DOT_BITS = 20
PHASE_DX = 125
INFO0 = 14                 # stream index of the DX copy of the first character (A)
RX_LAG = 5                 # its RX copy comes 5 symbols later
EOS_SET = (117, 122, 127)

FORMATS = {102: "AREA", 112: "DISTRESS", 114: "GROUP", 116: "ALL SHIPS",
           120: "INDIVIDUAL", 123: "AUTOMATIC"}
CATEGORIES = {100: "ROUTINE", 108: "SAFETY", 110: "URGENCY", 112: "DISTRESS"}
NATURES = {100: "FIRE EXPLOSION", 101: "FLOODING", 102: "COLLISION",
           103: "GROUNDING", 104: "LISTING CAPSIZE", 105: "SINKING",
           106: "ADRIFT", 107: "UNDESIGNATED", 108: "ABANDON SHIP",
           109: "PIRACY", 110: "MAN OVERBOARD", 112: "EPIRB"}
TELECOMMANDS = {100: "F3E SIMPLEX", 101: "F3E DUPLEX", 103: "POLLING",
                104: "UNABLE COMPLY", 105: "END OF CALL", 106: "DATA",
                109: "J3E TP", 110: "DISTRESS ACK", 112: "DISTRESS RELAY",
                113: "TTY FEC", 115: "TTY ARQ", 118: "TEST",
                121: "POSITION UPDATE", 126: "NO INFO"}

# MID -> country: the maritime identification digits most heard off the French
# coast, from the ITU table (RR appendix 42), typed from memory as in ATIS RX:
# check them before trusting a country name. The MMSI digits are always exact.
MIDS = {
    205: "Belgium", 209: "Cyprus", 210: "Cyprus", 211: "Germany",
    212: "Cyprus", 215: "Malta", 218: "Germany", 219: "Denmark",
    220: "Denmark", 224: "Spain", 225: "Spain", 226: "France",
    227: "France", 228: "France", 229: "Malta", 230: "Finland",
    232: "UK", 233: "UK", 234: "UK", 235: "UK", 237: "Greece",
    239: "Greece", 240: "Greece", 241: "Greece", 244: "Netherlands",
    245: "Netherlands", 246: "Netherlands", 247: "Italy", 248: "Malta",
    249: "Malta", 250: "Ireland", 256: "Malta", 257: "Norway",
    258: "Norway", 259: "Norway", 261: "Poland", 263: "Portugal",
    265: "Sweden", 266: "Sweden", 271: "Turkey", 304: "Antigua",
    305: "Antigua", 308: "Bahamas", 309: "Bahamas", 311: "Bahamas",
    351: "Panama", 352: "Panama", 353: "Panama", 354: "Panama",
    355: "Panama", 356: "Panama", 357: "Panama", 370: "Panama",
    371: "Panama", 372: "Panama", 373: "Panama", 538: "Marshall Is",
    636: "Liberia", 637: "Liberia",
}


def symbol_bits(v):
    """The ten bits of symbol v in transmission order (1 = Y, 0 = B)."""
    info = [(v >> i) & 1 for i in range(7)]
    zeros = info.count(0)
    return info + [(zeros >> 2) & 1, (zeros >> 1) & 1, zeros & 1]


def ecc(chars):
    x = 0
    for v in chars:
        x ^= v
    return x


def digits(s):
    """'227006480' -> the symbols of the digit string, padded with a 0 to an
    even length (an MMSI is 9 digits, sent as 10)."""
    if len(s) % 2:
        s += "0"
    return [int(s[i:i + 2]) for i in range(0, len(s), 2)]


def call(fmt, *fields, eos=127):
    """The characters A, I1..In, EOS, ECC of a call. fields: symbols (ints) or
    digit strings."""
    chars = [fmt]
    for f in fields:
        chars += digits(f) if isinstance(f, str) else [f]
    chars.append(eos)
    return chars + [ecc(chars)]


def sequence(chars):
    """The stream of symbols (DX, RX alternately) for the characters A..ECC."""
    info = chars[:-1]                    # A .. EOS
    n = len(chars)
    dx = [PHASE_DX] * 6 + [chars[0]] + chars + [chars[-2], chars[-2]]
    rx = list(range(111, 103, -1)) + [chars[0]] + chars
    assert len(dx) == len(rx) == n + 9
    out = []
    for x, r in zip(dx, rx):
        out += [x, r]
    # the stream ends on the RX copy of the ECC
    assert out[INFO0 + 2 * (n - 1) + RX_LAG] == chars[-1] and len(out) == INFO0 + 2 * (n - 1) + RX_LAG + 1
    assert info[-1] in EOS_SET
    return out


def bits(chars, dots=True, bad_ecc=False):
    seq = sequence(chars)
    if bad_ecc:                          # flip the ECC in both its copies
        n = len(chars)
        seq[INFO0 + 2 * (n - 1)] ^= 1
        seq[INFO0 + 2 * (n - 1) + RX_LAG] ^= 1
    out = [i & 1 for i in range(DOT_BITS)] if dots else []
    for v in seq:
        out += symbol_bits(v)
    return out


def decode(seq):
    """The characters A..EOS from a received stream (None = failed check
    bits), each from its DX copy, else its RX copy; None when the sequence is
    not a valid call."""
    chars = []
    k = 0
    while True:
        i = INFO0 + 2 * k
        if i + RX_LAG >= len(seq):
            return None
        v = seq[i] if seq[i] is not None else seq[i + RX_LAG]
        if v is None:
            return None
        chars.append(v)
        if k and v in EOS_SET:
            break
        k += 1
    i = INFO0 + 2 * (k + 1)
    if i + RX_LAG >= len(seq):
        return None
    e = seq[i] if seq[i] is not None else seq[i + RX_LAG]
    if e is None or ecc(chars) != e or chars[0] not in FORMATS:
        return None
    return chars


def mmsi(sym5):
    if any(v > 99 for v in sym5):
        return None
    return "".join("%02d" % v for v in sym5)[:9]


def position(sym5):
    d = "".join("%02d" % v for v in sym5)
    if d == "9999999999":
        return None
    ns = "S" if d[0] in "23" else "N"
    ew = "W" if d[0] in "13" else "E"
    return "%s %s%s %s %s%s" % (d[1:3], d[3:5], ns, d[5:8], d[8:10], ew)


def utc(sym2):
    d = "%02d%02d" % tuple(sym2)
    return None if d == "8888" else d


def fields(chars):
    """A dict of what DSC RX shows, from the characters A..EOS."""
    out = {"format": chars[0]}
    p = 1
    fmt = chars[0]

    def take(n):
        nonlocal p
        v = chars[p:p + n]
        if len(v) < n or chars[-1] in v:
            raise ValueError("call too short")
        p += n
        return v

    if fmt != 112:
        if fmt != 116:
            out["to"] = mmsi(take(5)) if fmt != 102 else "AREA"
        out["category"] = take(1)[0]
    else:
        out["category"] = 112
    out["from"] = mmsi(take(5))
    if fmt == 112:
        tail = True
    else:
        out["tc1"] = take(1)[0]
        tail = out["category"] == 112 and out["tc1"] in (110, 112)
        if tail:
            out["distressed"] = mmsi(take(5))
        else:
            out["tc2"] = take(1)[0]
            rest = chars[p:-1]
            if len(rest) >= 3 and rest[0] // 10 == 9:
                out["channel"] = int("%02d%02d" % (rest[1], rest[2]))
    if tail:
        out["nature"] = take(1)[0]
        out["position"] = position(take(5))
        out["utc"] = utc(take(2))
        out["subsequent"] = take(1)[0]
    return out


def country(m):
    return MIDS.get(int(m[:3])) if m else None


# Test calls (the MMSIs are made up; 227 is France).
SHIP = "227006480"
TEST_CALLS = {
    # MAYDAY: sinking at 43 18N 005 22E, 14:35 UTC, voice on 16 follows
    "distress":  call(112, SHIP, 105, "0431800522", "1435", 100),
    # distress, nothing known but the identity
    "dist_nopos": call(112, SHIP, 107, "9999999999", "8888", 100),
    # a coast station acknowledges it to all ships
    "dist_ack":  call(116, 112, "002275300", 110, SHIP, 105, "0431800522", "1435", 100),
    # PAN PAN to all ships, voice on channel 16
    "urgency":   call(116, 110, "002275300", 100, 126, "900016"),
    # SECURITE to all ships, voice on channel 67
    "safety":    call(116, 108, "002275300", 100, 126, "900067"),
    # a routine call from a ship to another, work on channel 72, ack requested
    "routine":   call(120, "228123456", 100, SHIP, 100, 126, "900072", eos=117),
    # a test call to a coast station
    "test":      call(120, "002275300", 100, SHIP, 118, 126, "126126", eos=117),
}

if __name__ == "__main__":
    yb = lambda v: "".join("Y" if b else "B" for b in symbol_bits(v))
    assert yb(125) == "YBYYYYYBBY" and yb(111) == "YYYYBYYBBY"       # as ATIS
    for name, chars in TEST_CALLS.items():
        seq = sequence(chars)
        assert decode(seq) == chars[:-1], name
        hit = list(seq)
        for k in range(len(chars)):                                  # every DX copy lost
            hit[INFO0 + 2 * k] = None
        assert decode(hit) == chars[:-1], name
        hit[INFO0 + RX_LAG] = None                                    # both copies of A lost
        assert decode(hit) is None
        b = bits(chars)
        f = fields(chars[:-1])
        print("%-10s %3d bits %4.0f ms  %s" % (name, len(b), len(b) / 1.2, f))
    assert decode(sequence(TEST_CALLS["distress"])[:-1]) is None      # cut before the RX ECC
    f = fields(TEST_CALLS["distress"][:-1])
    assert f["position"] == "43 18N 005 22E" and f["utc"] == "1435" and f["nature"] == 105
    assert fields(TEST_CALLS["urgency"][:-1])["channel"] == 16
    assert fields(TEST_CALLS["dist_ack"][:-1])["distressed"] == SHIP
    assert country(SHIP) == "France"
    print("DSC reference OK")
