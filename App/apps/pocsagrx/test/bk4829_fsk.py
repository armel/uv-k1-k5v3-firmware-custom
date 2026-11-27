#!/usr/bin/env python3
"""The BK4829's FSK receiver (REG_58 FSK 1.2K) as the app uses it, for the
emulated runs (test/emu_rx.py, ../app_screenshot.py).

Behaviour as Ferrite (RT-950, BK4829) and the UV-K1 measurements found it:
once the modem is on (REG_58 bit 0) and receiving (REG_59 bit 12) it needs
16 bytes of preamble, then finds its sync word (REG_5A/5B, 4 bytes) in either
polarity; each 16 bits that follow go to the FIFO (REG_5F), the first byte
off the air in the low half, until the REG_5D frame length (interrupt "done").
The almost-full interrupt rises when the FIFO reaches 4 words. Interrupts are
latched in REG_02 (cleared when read) and flagged in REG_0C bit 0 while
enabled in REG_3F. After the transmission the line is noise.

  Modem(bits).read(reg, k) / .write(reg, value, k), k = the bit period now
  (bit periods since the line's first bit, at 1200 bauds)."""
import random
from collections import deque

IRQ_SYNC, IRQ_AF, IRQ_DONE = 0x0002, 0x1000, 0x2000
PREAMBLE_BITS = 128
TX_REGS = (0x30, 0x36)


class Modem:
    def __init__(self, line, seed=1):
        self.line = list(line)
        self.rng = random.Random(seed)
        self.reg = {}
        self.writes = []
        self.reset(0)

    def reset(self, k):
        self.synced = self.done = self.af = False
        self.fifo = deque()
        self.last = self.pending = self.words = 0
        self.armed_at = self.scan = k

    def bit(self, k):
        return self.line[k] if 0 <= k < len(self.line) else self.rng.getrandbits(1)

    def step(self, now):
        if not (self.reg.get(0x58, 0) & 1 and self.reg.get(0x59, 0) & 0x1000) or self.done:
            return
        if not self.synced:
            sync = self.reg.get(0x5A, 0) << 16 | self.reg.get(0x5B, 0)
            pat = [(sync >> (31 - i)) & 1 for i in range(32)]
            neg = [1 - b for b in pat]
            k = max(self.armed_at + PREAMBLE_BITS, self.scan)
            while k + 32 <= now:
                if [self.bit(k + i) for i in range(32)] in (pat, neg):
                    self.synced, self.pos = True, k + 32
                    self.pending |= IRQ_SYNC
                    break
                k += 1
            self.scan = k
            return
        size = (self.reg.get(0x5D, 0) >> 8 | ((self.reg.get(0x5D, 0) >> 5) & 7) << 8) + 1
        while self.pos + 16 <= now and not self.done:
            w = 0
            for i in range(16):
                w = w << 1 | self.bit(self.pos + i)
            self.pos += 16
            self.words += 1
            self.fifo.append((w >> 8 | w << 8) & 0xFFFF)
            if len(self.fifo) >= 4 and not self.af:
                self.af = True
                self.pending |= IRQ_AF
            if 2 * self.words >= size:
                self.done = True
                self.pending |= IRQ_DONE

    def read(self, reg, now):
        self.step(now)
        if reg == 0x0C:
            return int(bool(self.pending & self.reg.get(0x3F, 0)))
        if reg == 0x02:
            p, self.pending = self.pending & self.reg.get(0x3F, 0), 0
            return p
        if reg == 0x5F:
            if self.fifo:
                self.last = self.fifo.popleft()
            if len(self.fifo) < 4:
                self.af = False
            return self.last                    # an empty FIFO reads its last word again
        return self.reg.get(reg, 0)

    def write(self, reg, value, now):
        self.step(now)
        self.writes.append((reg, value))
        if reg in TX_REGS:
            raise RuntimeError("REG_%02X written: the receiver must not key the transmitter" % reg)
        if reg == 0x02:
            return
        if reg == 0x58 and not value & 1:
            self.reset(now)
        if reg == 0x59 and value & 0x4000:
            self.fifo.clear()
        self.reg[reg] = value
        if (reg == 0x58 and value & 1) or (reg == 0x59 and value & 0x1000):
            self.armed_at = self.scan = now
