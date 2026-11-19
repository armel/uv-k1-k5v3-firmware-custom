#!/usr/bin/env python3
"""Focused tests for the function-0 numeric/alpha heuristic."""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pocsag
from model_rx import prefer_alpha


def views(text, source_numeric):
    raw = pocsag.message_bits(text, source_numeric)
    fill = [0, 0, 1, 1] if source_numeric else [0]
    while len(raw) % 20:
        raw.append(fill[len(raw) % len(fill)] if source_numeric else 0)

    numeric = []
    for i in range(0, len(raw) - 3, 4):
        v = sum(b << k for k, b in enumerate(raw[i:i + 4]))
        numeric.append(ord(pocsag.NUMERIC[v]))
    while numeric and numeric[-1] == ord(" "):
        numeric.pop()

    alpha = []
    controls = 0
    for i in range(0, len(raw) - 6, 7):
        v = sum(b << k for k, b in enumerate(raw[i:i + 7]))
        if 32 <= v < 127:
            alpha.append(v)
        else:
            controls += 1
    return numeric, alpha, controls


def detected(text, source_numeric):
    numeric, alpha, controls = views(text, source_numeric)
    return prefer_alpha(numeric, alpha, controls)


for text in ("CAVSAV sur VTUB", "CODB sur VTUB", "EQID sur VTUB"):
    assert detected(text, False), text

for text in ("0", "112", "0145 67-89", "555-1234", "1234567890",
             "(01)45 67 89", "01(45)67-89", "U 123456", "(555) 123-4567"):
    assert not detected(text, True), text

# The override is intentionally conservative: too little text remains numeric.
assert not detected("TEST", False)
assert not detected("CALL 123 TEST", False)
assert not detected("CALL: TEST PAGE", False)

# Regression: a valid but unusual numeric page used to look alpha-like enough
# to override its standard interpretation.
assert not detected("46*7(4-(601)8 *(0**2U", True)

rnd = random.Random(0x50C5A6)
for _ in range(20000):
    text = "".join(rnd.choice(pocsag.NUMERIC) for _ in range(rnd.randint(1, 80)))
    assert not detected(text, True), text

print("function-0 format heuristic OK")
