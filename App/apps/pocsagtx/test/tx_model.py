#!/usr/bin/env python3
"""Mirror of pocsagtx_app.c's encoder, checked against the POCSAG reference.

pocsagtx_app.c build_words() and the transmit loop are re-expressed here bit
for bit and compared with pocsag.py (the reference shared with POCSAG RX): the
codeword list of one page, then the full bit stream (preamble + sync words +
batches) must be identical. This is the same check the C encoder runs against
on the radio, without the radio.
"""
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "pocsagrx", "test"))
import pocsag

SYNC, IDLE = pocsag.SYNC, pocsag.IDLE
NUMERIC = pocsag.NUMERIC


def c_build_words(ric, func, text):
    """pocsagtx_app.c build_words(): the words of one page, no sync words."""
    w = []
    n = 0
    while (n >> 1) % 8 != (ric & 7):
        w.append(IDLE); n += 1
    if n & 1:
        w.append(IDLE); n += 1
    w.append(pocsag.codeword(((ric >> 3) << 2) | func)); n += 1
    k = 4 if func == 0 else 7
    acc = 0; na = 0
    for ch in text:
        v = NUMERIC.index(ch) if func == 0 else (ord(ch) & 127)
        for b in range(k):
            acc = (acc << 1) | ((v >> b) & 1)
            na += 1
            if na == 20:
                w.append(pocsag.codeword((1 << 20) | acc)); n += 1
                acc = 0; na = 0
    if na:
        while na < 20:
            acc = (acc << 1) | (((na >> 1) & 1) if func == 0 else 0)
            na += 1
        w.append(pocsag.codeword((1 << 20) | acc)); n += 1
    w.append(IDLE); n += 1
    while n % pocsag.BATCH_WORDS:
        w.append(IDLE); n += 1
    return w


def c_bits(ric, func, text):
    """pocsagtx_app.c transmit(): preamble, then sync + 16 words per batch."""
    out = [(i + 1) & 1 for i in range(pocsag.PREAMBLE_BITS)]
    words = c_build_words(ric, func, text)
    for i in range(0, len(words), pocsag.BATCH_WORDS):
        for cw in [SYNC] + words[i:i + pocsag.BATCH_WORDS]:
            out += [(cw >> b) & 1 for b in range(31, -1, -1)]
    return out


def c_fifo(ric, func, text):
    """pocsagtx_app.c transmit()'s 16-bit FIFO words, in write order."""
    fifo = [0xAAAA] * (pocsag.PREAMBLE_BITS // 16)   # preamble packed MSB-first
    words = c_build_words(ric, func, text)
    for i in range(0, len(words), pocsag.BATCH_WORDS):
        for cw in [SYNC] + words[i:i + pocsag.BATCH_WORDS]:
            fifo.append(cw >> 16)   # high word first: the FSK sends MSB first
            fifo.append(cw & 0xFFFF)
    return fifo


def fifo_bits(fifo):
    """The bit stream a FIFO word list produces, each word MSB-first."""
    return [b for w in fifo for b in ((w >> k) & 1 for k in range(15, -1, -1))]


if __name__ == "__main__":
    for name, pages in pocsag.TEST_PAGES.items():
        for ric, func, text in pages:
            mine = c_build_words(ric, func, text)
            ref = pocsag.batches([(ric, func, text)])
            ref_words = [cw for i, cw in enumerate(ref) if i % 17 != 0]
            assert mine == ref_words, (name, ric, func, mine, ref_words)
            assert c_bits(ric, func, text) == pocsag.bits([(ric, func, text)]), \
                (name, ric, func, "bit stream differs")
            fifo = c_fifo(ric, func, text)
            assert fifo_bits(fifo) == pocsag.bits([(ric, func, text)]), \
                (name, ric, func, "FIFO packing differs")
            assert len(fifo) <= 128, (name, ric, func, "FIFO overflows 128 words")
            nbits = len(fifo) * 16
            print("%-8s ric %7d func %d  %2d fifo words  %4d bits  %5.2f s at 1200" %
                  (name, ric, func, len(fifo), nbits, nbits / 1200))
    print("POCSAG TX encoder OK")

    # The app caps messages at 45 characters: 45 always fits one 128-word FIFO
    # load (104 words), 46 can need a 3rd batch (138 words) for a RIC in frame 7.
    assert all(len(c_fifo(ric, 3, "A" * 45)) <= 128 for ric in range(8))
    assert len(c_fifo(7, 3, "A" * 46)) > 128
    print("45-character FIFO boundary OK")
