"""Check the per-channel filter: the Filter column, the mode names, and the
v2.0 defaults for CW and USB channels in pre-v2.0 images.

Bit 1 of the flags byte is 0 wide and 1 narrow in every modulation. FM and AM
also show it as NFM/NAM, so CHIRP can import narrow channels from other radios.
CW starts narrow when a channel is new to it, as in the firmware.

    python3 tools/chirp-test/test_filter.py [path/to/module.py]
"""
import importlib.util
import logging
import os
import sys
import types

sys.path.insert(0, os.environ.get('CHIRP_SRC', os.path.expanduser('~/code/chirp')))
logging.disable(logging.CRITICAL)
wx = types.ModuleType("wx")
for n in ("OK", "CANCEL", "CANCEL_DEFAULT", "ICON_WARNING"):
    setattr(wx, n, 0)
wx.MessageBox = lambda *a, **k: 0
sys.modules["wx"] = wx

from chirp import chirp_common, memmap
from chirp.settings import RadioSetting, RadioSettingValueList

MODULE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..',
    'chirp', 'brass.k1-k5v3.chirp.py')
spec = importlib.util.spec_from_file_location("nr7y_mod", MODULE)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

FM, AM, USB, CW = 0, 1, 2, 3
failures = []


def check(cond, what):
    print("%s  %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        failures.append(what)


def blank_image():
    img = bytearray(b'\xFF' * m.MAIN_END)
    for i in range(m.MR_CHANNELS_MAX):
        img[0x4000 + i * 16:0x4000 + i * 16 + 16] = b'\x20' * 16
    return img


def chan(img, i, mod, bw, bit6=1, freq_hz=None):
    """A channel record: TX_LOCK (or schema 1's NARROWEST) in bit 6, LOW1
    power, the filter in bit 1."""
    freq_hz = freq_hz or 145500000 + i * 25000
    a = i * 16
    img[a:a + 16] = b'\x00' * 16
    img[a:a + 4] = (freq_hz // 10).to_bytes(4, 'little')
    img[a + 11] = mod << 4
    img[a + 12] = (bit6 << 6) | (1 << 2) | (bw << 1)
    img[a + 14] = 4                       # 12.5k step
    img[0x8000 + i * 2:0x8000 + i * 2 + 2] = b'\x02\x00'   # band 2, no scanlist


def v2_image():
    img = blank_image()
    img[0xA200:0xA208] = b'CWX\x01\x02\xFF\xFF\xFF'
    return img


def load(data):
    radio = m.UVK5_NR7Y_Fusion(None)
    radio._mmap = memmap.MemoryMapBytes(bytes(data))
    radio.process_mmap()
    return radio


def bit(radio, n):
    return int(radio._memobj.channel[n - 1].bandwidth)


def shown(radio, n):
    mem = radio.get_memory(n)
    return mem.mode, str(mem.extra["filter"].value)


def edit(radio, n, mode=None, column=None):
    mem = radio.get_memory(n)
    if mode is not None:
        mem.mode = mode
    if column is not None:
        mem.extra["filter"].value = column
    radio.set_memory(mem)
    return shown(radio, n)


def new(radio, n, mode, column=None, extras=True):
    """A new channel in an empty slot: from the slot's own (empty) memory, as
    the editor does, or from a bare Memory as a CSV import does."""
    mem = radio.get_memory(n) if extras else chirp_common.Memory()
    mem.number = n
    mem.empty = False
    mem.freq = 146000000 + n * 25000
    mem.tuning_step = 12.5
    mem.mode = mode
    if column is not None:
        mem.extra["filter"].value = column
    radio.set_memory(mem)
    return shown(radio, n)


# --- reading ---------------------------------------------------------------
img = v2_image()
layout = [(FM, 0), (FM, 1), (AM, 0), (AM, 1), (USB, 0), (USB, 1), (CW, 0), (CW, 1)]
for i, (mod, bw) in enumerate(layout):
    chan(img, i, mod, bw)
r = load(img)

expect = [("FM", "Wide"), ("NFM", "Narrow"), ("AM", "Wide"), ("NAM", "Narrow"),
          ("USB", "Wide"), ("USB", "Narrow"), ("CW", "Wide"), ("CW", "Narrow")]
for n, want in enumerate(expect, 1):
    check(shown(r, n) == want, "read ch%d as %s / %s" % ((n,) + want))
check(list(r.get_memory(1).extra.keys())[0] == "filter",
      "Filter is the first extra column")
check(str(r.get_memory(30).extra["filter"].value) == "Wide",
      "empty memory offers Filter, default Wide")

# --- editing an existing channel -------------------------------------------
check(edit(r, 1, column="Narrow") == ("NFM", "Narrow"), "FM: Filter column to Narrow gives NFM")
check(edit(r, 1, mode="FM") == ("FM", "Wide"), "NFM: mode to FM gives Wide")
check(edit(r, 1, mode="NFM") == ("NFM", "Narrow"), "FM: mode to NFM gives Narrow")
check(edit(r, 3, column="Narrow") == ("NAM", "Narrow"), "AM: Filter column to Narrow gives NAM")
check(edit(r, 5, column="Narrow") == ("USB", "Narrow"), "USB: Filter column to Narrow")
check(edit(r, 8, column="Wide") == ("CW", "Wide"), "CW: Filter column to Wide")
check(edit(r, 8, column="Narrow") == ("CW", "Narrow"), "CW: and back to Narrow")
check(edit(r, 7) == ("CW", "Wide"), "CW: saving without a change keeps Wide")
check(edit(r, 6, mode="USB") == ("USB", "Narrow"), "USB: saving without a change keeps Narrow")

# --- changing modulation ---------------------------------------------------
check(edit(r, 2, mode="CW") == ("CW", "Narrow"), "FM -> CW starts narrow")
check(edit(r, 7, mode="CW", column="Wide") == ("CW", "Wide"), "CW stays CW: Wide kept")
r2 = load(img)
check(edit(r2, 1, mode="CW") == ("CW", "Narrow"), "FM wide -> CW starts narrow")
check(edit(r2, 7, mode="FM") == ("FM", "Wide"), "CW -> FM takes the mode name: Wide")
check(edit(r2, 8, mode="NAM") == ("NAM", "Narrow"), "CW -> NAM takes the mode name: Narrow")
check(edit(r2, 5, mode="USB") == ("USB", "Wide"), "USB unchanged stays Wide")
check(edit(r2, 2, mode="USB") == ("USB", "Narrow"),
      "NFM -> USB keeps the Filter column (Narrow)")

# --- new channels ----------------------------------------------------------
r = load(img)
check(new(r, 20, "CW") == ("CW", "Narrow"), "new CW channel starts Narrow")
check(new(r, 21, "USB") == ("USB", "Wide"), "new USB channel starts Wide")
check(new(r, 22, "USB", column="Narrow") == ("USB", "Narrow"), "new USB channel set Narrow")
check(new(r, 23, "FM") == ("FM", "Wide"), "new FM channel is Wide")
check(new(r, 24, "NFM", extras=False) == ("NFM", "Narrow"),
      "imported NFM (no extras, as from CSV) is Narrow")
check(new(r, 25, "USB", extras=False) == ("USB", "Wide"),
      "imported USB (no extras) is Wide")

# A same-named extra from another driver, with its own options, is ignored
mem = r.get_memory(26)
mem.number, mem.empty, mem.freq, mem.tuning_step, mem.mode = (
    26, False, 146700000, 12.5, "USB")
mem.extra = chirp_common.Memory().extra
mem.extra.append(RadioSetting("filter", "Filter",
                              RadioSettingValueList(["A", "B", "C"], current_index=2)))
r.set_memory(mem)
check(shown(r, 26) == ("USB", "Wide"), "foreign 'filter' extra is ignored")

# Every mode the radio lists reads back as itself (CHIRP's test_mode contract)
for mode in r.get_features().valid_modes:
    check(edit(r, 1, mode=mode)[0] == mode, "mode %s round-trips" % mode)

# --- pre-v2.0 images -------------------------------------------------------
def legacy(schema):
    img = blank_image()
    img[0xA170] = schema
    # schema 1 kept NARROWEST in bit 6 on CW/USB; schema 2 has TX_LOCK there
    chan(img, 0, FM, 1)
    chan(img, 1, AM, 1)
    chan(img, 2, CW, 0, bit6=0)
    chan(img, 3, CW, 1, bit6=1)
    chan(img, 4, USB, 1, bit6=1)
    chan(img, 5, USB, 0, bit6=0)
    for v in range(14):                          # VFO slots: one CW, one USB
        a = 0x9000 + v * 16
        img[a:a + 16] = b'\x00' * 16
        img[a:a + 4] = (145000000 // 10).to_bytes(4, 'little')
        img[a + 11] = (CW if v == 0 else USB if v == 1 else FM) << 4
        img[a + 12] = (1 << 6) | (1 << 2) | ((v != 0) << 1)
        img[a + 14] = 4
    return img

for schema, label in ((2, "v1.4 image"), (0xFF, "pre-v1.4 image")):
    r = load(legacy(schema))
    check(bit(r, 1) == 1 and bit(r, 2) == 1, "%s: FM and AM narrow kept" % label)
    check(bit(r, 3) == 1 and bit(r, 4) == 1, "%s: CW channels narrow" % label)
    check(bit(r, 5) == 0 and bit(r, 6) == 0, "%s: USB channels wide" % label)
    vfo = r._memobj.vfo_channel
    check(int(vfo[0].bandwidth) == 1 and int(vfo[1].bandwidth) == 0,
          "%s: VFO slots too (CW narrow, USB wide)" % label)
    check(int(vfo[2].bandwidth) == 1, "%s: FM VFO slot keeps its bit" % label)
    if schema == 0xFF:
        check(all(int(r._memobj.channel[i].txLock) == 1 for i in range(2, 6)),
              "%s: TX_LOCK set on CW/USB" % label)

    # Once converted, the image is v2.0: the user's choice survives a reload
    edit(r, 3, column="Wide")
    r.process_mmap()
    check(bit(r, 3) == 0, "%s: CW set Wide survives a second load" % label)

# A v2.0 image is never touched
r = load(img)   # the v2.0 image from the top, channel 7 is CW wide
check(bit(r, 7) == 0, "v2.0 image: CW wide left alone")

print()
print("FAIL (%d)" % len(failures) if failures else "PASS")
sys.exit(1 if failures else 0)
