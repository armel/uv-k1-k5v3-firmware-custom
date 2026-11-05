#!/usr/bin/env python3
"""POCSAG (CCIR Radiopaging Code No. 1, ITU-R M.584) reference: codewords,
batches and messages, built and decoded bit by bit.

  transmission   preamble of 576 alternating bits (1 0 1 0 ...), then batches
  batch          the sync codeword 0x7CD215D8, then 8 frames of 2 codewords
  codeword       32 bits, most significant first: 21 information bits, 10 BCH
                 (31,21) check bits (generator 0x769), 1 even-parity bit
  address word   bit 31 = 0, 18 address bits, 2 function bits. The 3 low bits
                 of the 21-bit address (RIC) are not sent: they are the number
                 of the frame the word is sent in.
  message word   bit 31 = 1, 20 message bits, following their address word
  idle word      0x7A89C197 fills the unused positions
  numeric        4 bits per character, least significant bit first:
                 0-9, spare (*), U, space, -, ), (
  alphanumeric   7-bit ASCII, least significant bit first, the characters
                 running across the 20-bit fields

  pocsag.py      self-test (sync and idle are valid codewords, round trips)
"""
SYNC, IDLE = 0x7CD215D8, 0x7A89C197
PREAMBLE_BITS = 576
BATCH_WORDS = 16
GEN = 0x769                     # x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1
NUMERIC = "0123456789*U -)("
FUNC_NUMERIC = 0                # by usage: function 0 numeric, the others text


def syndrome(cw):
    """Remainder of the 31 upper bits by the generator: 0 for a valid word."""
    r = cw >> 1
    for i in range(30, 9, -1):
        if r & (1 << i):
            r ^= GEN << (i - 10)
    return r


def parity(cw):
    return bin(cw).count("1") & 1


def codeword(info21):
    cw = info21 << 11
    cw |= syndrome(cw) << 1
    return cw | parity(cw)


def valid(cw):
    return syndrome(cw) == 0 and parity(cw) == 0


def correct(cw):
    """The codeword itself, or with its single wrong bit put right; None when
    more than one bit is wrong (a 2-bit error is detected, never mis-corrected:
    the code's distance is 6 with the parity bit)."""
    if valid(cw):
        return cw
    for i in range(32):
        if valid(cw ^ (1 << i)):
            return cw ^ (1 << i)
    return None


def message_bits(text, numeric):
    out = []
    for ch in text:
        v, n = (NUMERIC.index(ch), 4) if numeric else (ord(ch) & 127, 7)
        out += [(v >> i) & 1 for i in range(n)]
    return out


def batches(pages):
    """Codewords of a transmission for pages [(ric, function, text)], each
    page placed in its frame (ric & 7), idle words elsewhere; sync words
    included. Numeric text for function FUNC_NUMERIC."""
    words = []                                     # without the sync words
    for ric, func, text in pages:
        while (len(words) // 2) % 8 != (ric & 7):
            words.append(IDLE)
        if len(words) & 1:
            words.append(IDLE)
        words.append(codeword(((ric >> 3) << 2) | func))
        b = message_bits(text, func == FUNC_NUMERIC)
        fill = [0, 0, 1, 1] if func == FUNC_NUMERIC else [0]   # space digits / NUL bits
        while len(b) % 20:
            b.append(fill[len(b) % len(fill)] if func == FUNC_NUMERIC else 0)
        for i in range(0, len(b), 20):
            v = 0
            for bit in b[i:i + 20]:
                v = (v << 1) | bit
            words.append(codeword((1 << 20) | v))
    words.append(IDLE)                             # ends the last message
    while len(words) % BATCH_WORDS:
        words.append(IDLE)
    out = []
    for i in range(0, len(words), BATCH_WORDS):
        out += [SYNC] + words[i:i + BATCH_WORDS]
    return out


def bits(pages, preamble=PREAMBLE_BITS):
    out = [(i + 1) & 1 for i in range(preamble)]   # 1 0 1 0 ...
    for cw in batches(pages):
        out += [(cw >> i) & 1 for i in range(31, -1, -1)]
    return out


class Decoder:
    """Pages from codewords, as the app does: feed(position in the batch 0..15,
    codeword or None when it could not be corrected)."""
    def __init__(self):
        self.pages = []
        self.cur = None

    def close(self):
        if self.cur:
            ric, func, acc = self.cur
            n = 4 if func == FUNC_NUMERIC else 7
            text = ""
            for i in range(0, len(acc) - n + 1, n):
                v = sum(b << k for k, b in enumerate(acc[i:i + n]))
                text += NUMERIC[v] if n == 4 else (chr(v) if 32 <= v < 127 else "")
            self.pages.append((ric, func, text.rstrip(" ") if n == 4 else text))
        self.cur = None

    def feed(self, pos, cw):
        if cw is None or cw == IDLE:
            self.close()
        elif cw >> 31:
            if self.cur:
                self.cur[2].extend((cw >> i) & 1 for i in range(30, 10, -1))
        else:
            self.close()
            self.cur = (((cw >> 13) & 0x3FFFF) << 3 | (pos >> 1), (cw >> 11) & 3, [])


def decode(words):
    d = Decoder()
    for i, cw in enumerate(words):
        if i % (BATCH_WORDS + 1) == 0:
            assert cw == SYNC
            continue
        d.feed(i % (BATCH_WORDS + 1) - 1, correct(cw))
    d.close()
    return d.pages


TEST_PAGES = {
    "alpha":   [(1234567, 3, "F4HWN: POCSAG RX test on 433.650 MHz")],
    "numeric": [(1234560, 0, "0145 67-89")],
    "two":     [(2504, 3, "DAPNET time 2026-10-13 21:07"), (165856, 3, "Second page, same transmission.")],
    "long":    [(1000001, 3, "The quick brown fox jumps over the lazy dog 0123456789 times, then again.")],
    "same":    [(4520, 3, "First page to this address"), (4520, 3, "Second one, same address")],
    # tone-only pages (address alone), shown with key 4 on; RIC 8 and up, as
    # RIC 0 to 7 with function 0 is the all-zero codeword the app refuses
    "tone":    [(1234567, 1, "")],
    "tonemix": [(1234560, 0, ""), (1045677, 3, "After a tone-only page")],
}

if __name__ == "__main__":
    assert valid(SYNC) and valid(IDLE)
    assert codeword(SYNC >> 11) == SYNC and codeword(IDLE >> 11) == IDLE
    cw = codeword(0x12345)
    assert all(correct(cw ^ (1 << i)) == cw for i in range(32))
    assert all(correct(cw ^ (1 << i) ^ (1 << j)) is None for i in range(32) for j in range(i))
    for name, pages in TEST_PAGES.items():
        w = batches(pages)
        assert decode(w) == pages, (name, decode(w))
        print("%-8s %2d batches %5.2f s  %s" % (name, len(w) // 17, len(bits(pages)) / 1200, pages))
    print("POCSAG reference OK")
