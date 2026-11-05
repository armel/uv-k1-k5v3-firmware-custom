#!/usr/bin/env python3
"""ATIS (Automatic Transmitter Identification System, inland waterways) reference:
the signal sequence of ETSI EN 300 698 annex B, built and decoded bit by bit.

  ten-unit code   7 information bits, least significant first, then the number
                  of B (0) elements among them on 3 bits, most significant first
  sequence        20 dot bits, then 17 DX/RX pairs sent alternately:
                    DX 125 125 125 125 125 125  A   A   B1 B2 B3 B4 B5  C   D   C   C
                    RX 111 110 109 108 107 106 105 104  A   A  B1 B2 B3 B4 B5   C   D
                  A = format specifier 121, B1..B5 = the 10-digit identity two
                  digits per symbol, C = end of sequence 127, D = error check
                  (XOR of A, B1..B5 and C). Each information symbol is sent
                  twice, the RX copy 5 symbols after the DX one.
  identity        9 MID X1X2 X3X4X5X6: X1X2 = second letter of the call sign
                  (01 = A), X3..X6 = its number; the first letter is implied by
                  the MID (clause B.1.11).

  atis.py         self-test against the example of annex C (PC8075 -> ECC 110)
"""
PHASE_DX, FORMAT, EOS = 125, 121, 127
DOT_BITS = 20
PAIRS = 17
SYMBOLS = 2 * PAIRS
INFO0 = 14                 # stream index of the DX copy of the first information symbol
RX_LAG = 5                 # the RX copy comes 5 symbols later

# MID -> (first letter of the call sign, country). The first letter is what the
# MID stands for in clause B.1.11; it is a guess where a country has several
# call sign series ('?' when unknown). Maritime identification digits from the
# ITU table (RR appendix 42), typed from memory: to be checked before relying
# on a country name.
MIDS = {
    203: ("O", "Austria"),     205: ("O", "Belgium"),     207: ("L", "Bulgaria"),
    211: ("D", "Germany"),     218: ("D", "Germany"),     214: ("E", "Moldova"),
    226: ("F", "France"),      227: ("F", "France"),      228: ("F", "France"),
    232: ("M", "UK"),          233: ("M", "UK"),          234: ("M", "UK"),
    235: ("M", "UK"),          238: ("9", "Croatia"),     243: ("H", "Hungary"),
    244: ("P", "Netherlands"), 245: ("P", "Netherlands"), 246: ("P", "Netherlands"),
    247: ("I", "Italy"),       253: ("L", "Luxembourg"),  261: ("S", "Poland"),
    262: ("4", "Montenegro"),  264: ("Y", "Romania"),     267: ("O", "Slovakia"),
    269: ("H", "Switzerland"), 270: ("O", "Czechia"),     272: ("U", "Ukraine"),
    273: ("U", "Russia"),      278: ("S", "Slovenia"),    279: ("Y", "Serbia"),
}


def symbol_bits(v):
    """The ten bits of symbol v in transmission order (1 = Y, 0 = B)."""
    info = [(v >> i) & 1 for i in range(7)]
    zeros = info.count(0)
    return info + [(zeros >> 2) & 1, (zeros >> 1) & 1, zeros & 1]


def symbol_value(bits):
    """Symbol of ten received bits, or None when the check bits disagree."""
    v = sum(b << i for i, b in enumerate(bits[:7]))
    return v if symbol_bits(v) == list(bits) else None


def ecc(info):
    x = 0
    for v in info:
        x ^= v
    return x


def sequence(ident):
    """The 34 symbols of a signal sequence for the 10-digit identity string."""
    assert len(ident) == 10 and ident.isdigit()
    b = [int(ident[i:i + 2]) for i in range(0, 10, 2)]
    d = ecc([FORMAT] + b + [EOS])
    dx = [PHASE_DX] * 6 + [FORMAT, FORMAT] + b + [EOS, d, EOS, EOS]
    rx = list(range(111, 103, -1)) + [FORMAT, FORMAT] + b + [EOS, d]
    out = []
    for x, r in zip(dx, rx):
        out += [x, r]
    return out


def bits(ident, dots=True, bad_ecc=False):
    """The bit stream of a transmission (dot pattern included by default)."""
    seq = sequence(ident)
    if bad_ecc:
        seq[28] ^= 1
        seq[33] ^= 1
    out = [i & 1 for i in range(DOT_BITS)] if dots else []     # B Y B Y ...
    for v in seq:
        out += symbol_bits(v)
    return out


def decode(seq):
    """Identity string from 34 received symbols (None = failed its check bits):
    each information symbol is taken from its DX copy, else from its RX copy.
    None when the sequence is not a valid ATIS one."""
    info = []
    for k in range(8):
        v = seq[INFO0 + 2 * k]
        if v is None:
            v = seq[INFO0 + 2 * k + RX_LAG]
        if v is None:
            return None
        info.append(v)
    if info[0] != FORMAT or info[6] != EOS or ecc(info[:7]) != info[7]:
        return None
    if any(v > 99 for v in info[1:6]):
        return None
    return "".join("%02d" % v for v in info[1:6])


def from_call(call, mid):
    """Clause B.1.11: 'PC8075', 244 -> '9244038075'."""
    assert len(call) == 6 and call[1].isalpha() and call[2:].isdigit()
    return "9%03d%02d%s" % (mid, ord(call[1].upper()) - 64, call[2:])


def to_call(ident):
    """The call sign an identity stands for, or None (identity not built from
    a call sign, or unknown MID)."""
    mid, x = int(ident[1:4]), int(ident[4:6])
    if ident[0] != "9" or mid not in MIDS or not 1 <= x <= 26 or MIDS[mid][0] == "?":
        return None
    return MIDS[mid][0] + chr(64 + x) + ident[6:]


def country(ident):
    m = MIDS.get(int(ident[1:4]))
    return m[1] if m else None


TEST_IDS = {
    "pc8075": from_call("PC8075", 244),     # the example of annex C
    "fm1234": from_call("FM1234", 227),
    "dc5513": from_call("DC5513", 211),
    "mmsi":   "9227999123",                 # not a call sign: shown as digits
}

if __name__ == "__main__":
    seq = sequence("9244038075")
    assert seq[0::2] == [125] * 6 + [121, 121, 92, 44, 3, 80, 75, 127, 110, 127, 127], seq[0::2]
    assert seq[1::2] == [111, 110, 109, 108, 107, 106, 105, 104, 121, 121, 92, 44, 3, 80, 75, 127, 110]
    yb = lambda v: "".join("Y" if b else "B" for b in symbol_bits(v))
    assert yb(125) == "YBYYYYYBBY" and yb(111) == "YYYYBYYBBY" and yb(0) == "BBBBBBBYYY"
    assert yb(121)[:7] == "YBBYYYY" and yb(110) == "BYYYBYY" + "BYB"      # annex C, example 2
    assert all(symbol_value(symbol_bits(v)) == v for v in range(128))
    assert decode(seq) == "9244038075" and to_call("9244038075") == "PC8075"
    hit = list(seq); hit[16] = hit[18] = hit[31] = None                    # DX lost: RX copies used
    assert decode(hit) == "9244038075"
    hit[21] = None                                                          # both copies lost
    assert decode(hit) is None
    b = bits("9244038075")
    assert len(b) == 360
    print("ATIS reference: annex C example OK, %d bits = %.0f ms" % (len(b), len(b) / 1.2))
    for n, i in TEST_IDS.items():
        print("  %-7s %s  %-7s %s" % (n, i, to_call(i) or "-", country(i) or "-"))
