"""Build a synthetic schema-2, v2.0-layout .img for CHIRP's driver test suite.

Covers every modulation and both filter settings, so the driver tests exercise
the mode table and the wide/narrow bit rather than just FM defaults.
"""
import os
import sys
import types
import importlib.util

if 'wx' not in sys.modules:
    wx = types.ModuleType("wx")
    for _n in ("OK", "CANCEL", "CANCEL_DEFAULT", "ICON_WARNING"):
        setattr(wx, _n, 0)
    wx.MessageBox = lambda *a, **k: 0
    sys.modules["wx"] = wx

from chirp import memmap

MODULE = os.environ['NR7Y_MODULE']
OUT = sys.argv[1]

spec = importlib.util.spec_from_file_location("nr7y_mod", MODULE)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

img = bytearray(b'\x00' * m.MEM_SIZE)


def chan(i, freq_hz, mod, bw, name):
    a = i * 16
    img[a:a + 4] = (freq_hz // 10).to_bytes(4, 'little')
    img[a + 11] = (mod << 4)
    img[a + 12] = (1 << 6) | (1 << 2) | (bw << 1)   # TX_LOCK on, LOW1 power
    img[a + 14] = 4                                  # 12.5k step
    att = 0x8000 + i * 2
    img[att] = 0x02                                  # band 2 (137-174 MHz)
    img[att + 1] = 0                                 # scanlist off
    n = 0x4000 + i * 16
    img[n:n + 16] = name.ljust(16).encode()[:16]


chan(0, 145500000, 0, 0, "FM WIDE")
chan(1, 145525000, 0, 1, "FM NARROW")
chan(2, 145550000, 3, 0, "CW WIDE")
chan(3, 145575000, 3, 1, "CW NARROW")
chan(4, 145600000, 2, 0, "USB WIDE")
chan(5, 145625000, 2, 1, "USB NARROW")
chan(6, 145650000, 1, 0, "AM WIDE")
chan(7, 145675000, 1, 1, "AM NARROW")

for i in range(8, m.MR_CHANNELS_MAX):
    img[i * 16:(i * 16) + 16] = b'\xFF' * 16
    img[0x8000 + i * 2:0x8000 + i * 2 + 2] = b'\xFF\xFF'

for v in range(14):
    a = 0x9000 + v * 16
    img[a:a + 4] = (145000000 // 10).to_bytes(4, 'little')
    img[a + 12] = (1 << 6) | (1 << 2)
    img[a + 14] = 4

# v2.0 layout: CW block header (schema in it), then the macro
# area with one macro. The holes the download skips stay erased.
img[m.MAIN_END:m.MEM_SIZE] = b'\xFF' * (m.MEM_SIZE - m.MAIN_END)
img[m.NR7Y_CW_BLOCK_ADDR:m.NR7Y_CW_BLOCK_ADDR + 8] = (
    m.NR7Y_CW_BLOCK_SIG + bytes([m.NR7Y_CW_BLOCK_VERSION,
                                 m.NR7Y_SCHEMA_CURRENT, 0xFF, 0xFF, 0xFF]))
img[m.CW_MACRO_START:m.CW_MACRO_START + 16] = m._NR7Y_CW_MACRO_HDR
cq = b'CQ' + bytes([ord('D') | 0x80, ord('E')])
macro = bytearray(b'\xFF' * m._NR7Y_CW_MACRO_SIZE)
macro[0] = len(cq) | m._NR7Y_CW_MACRO_SIG
macro[1:1 + len(cq)] = cq
macro[-1] = sum(cq) & 0xFF
a = m._NR7Y_CW_MACRO_ADDRS[0]
img[a:a + len(macro)] = macro

radio = m.UVK5_NR7Y_Fusion(None)
radio._mmap = memmap.MemoryMapBytes(bytes(img))
radio.process_mmap()
radio.metadata = {'nr7y_firmware': 'Brass v2.0',
                  'nr7y_schema': m.NR7Y_SCHEMA_CURRENT}
radio.save_mmap(OUT)
print("wrote %s (%d bytes)" % (OUT, os.path.getsize(OUT)))
