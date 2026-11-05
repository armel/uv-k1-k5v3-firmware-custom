"""Check the v2.0 CW storage conversion of images saved before v2.0.

Builds a pre-v2.0 image (macros at 0x1C00, CW settings at 0xA140, schema at
0xA170), loads it through the module and checks the result against the v2.0
layout (the firmware no longer migrates; this is the only path), including the key action renumbering. Also checks
the upload gate refuses pre-v2.0 firmware.

    python3 tools/chirp-test/test_legacy_image.py [path/to/module.py]
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

from chirp import errors, memmap

MODULE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..',
    'chirp', 'brass.k1-k5v3.chirp.py')
spec = importlib.util.spec_from_file_location("nr7y_mod", MODULE)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

failures = []


def check(cond, what):
    print("%s  %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        failures.append(what)


def macro_block(text):
    payload = text.encode()
    block = bytearray(b'\xFF' * m._NR7Y_CW_MACRO_SIZE)
    block[0] = len(payload) | m._NR7Y_CW_MACRO_SIG
    block[1:1 + len(payload)] = payload
    block[-1] = sum(payload) & 0xFF
    return bytes(block)


# v1.x packed CW settings: 600 Hz with sidetone code 2, 22 WPM, key input 1
# with break-in, 4 s repeat, mode B, roger 9 dits, 300 ms hang
CW_SETTINGS = bytes([0x23, 22, 0x41, 4, 0x01, 9, 0xFF, 30])
CW_SETTINGS_V2 = bytes([60, 11, 22, 1, 1, 1, 4, 9, 30, 0, 0])
CHANNEL_455 = bytes(range(0x10, 0x20))   # a real channel inside macro block 3


# Key actions as v1.4 stored them: M long, SIDE1 short/long, SIDE2 short/long.
# MONITOR (3), PROPER ROGER (31), PLAY CW MSG 1 (21), CW SPEED (32), ALARM (6).
OLD_ACTIONS = bytes([3 << 1 | 1, 31, 21, 32, 6])
NEW_ACTIONS = bytes([3 << 1 | 1, 45, 37, 34, 0])


def legacy_image(schema=2, mix=False):
    img = bytearray(b'\xFF' * m.MAIN_END)
    img[0xA0A8:0xA0AD] = OLD_ACTIONS
    for i in range(m.MR_CHANNELS_MAX):
        img[0x4000 + i * 16:0x4000 + i * 16 + 16] = b'\x20' * 16
    img[0x1C00:0x1C30] = macro_block("CQCQ")   # macro 1: channels 448-450
    img[0x1C30:0x1C60] = macro_block("TEST")   # macro 2: channels 451-453
    img[455 * 16:456 * 16] = CHANNEL_455      # block 3 fails validation
    img[0x4000 + 455 * 16:0x4000 + 456 * 16] = b'KEEP'.ljust(16)
    img[0x8000 + 455 * 2:0x8000 + 456 * 2] = b'\x02\x00'
    img[0x8000 + 448 * 2:0x8000 + 449 * 2] = b'\x07\x00'
    img[0xA140:0xA148] = CW_SETTINGS
    img[0xA170] = schema
    if mix:
        img[0xA173:0xA177] = m.NR7Y_ARMEL_MIX_SIG
    return bytes(img)


def load(data):
    radio = m.UVK5_NR7Y_Fusion(None)
    radio._mmap = memmap.MemoryMapBytes(data)
    radio.process_mmap()
    return radio


def raw(radio, a, n):
    return bytes(radio._mmap[a:a + n])


# --- schema 2 image from v1.4 firmware -------------------------------------
r = load(legacy_image())
check(len(r._mmap) == m.MEM_SIZE, "image padded to MEM_SIZE")
check(raw(r, 0xA200, 8) == b'CWX\x01\x02\xFF\xFF\xFF', "CWX header, schema 2")
check(raw(r, 0xA0A8, 5) == NEW_ACTIONS, "key actions renumbered to fixed IDs")
check(raw(r, 0xD000, 16) == m._NR7Y_CW_MACRO_HDR, "macro header written")
check(raw(r, 0xD010, 48) == macro_block("CQCQ"), "macro 1 moved")
check(raw(r, 0xD040, 48) == macro_block("TEST"), "macro 2 moved")
check(raw(r, 0xD070, 96) == b'\xFF' * 96, "invalid and empty blocks written as 0xFF")
check(r._get_cw_msg(1) == "CQCQ" and r._get_cw_msg(2) == "TEST", "macros read back")
check(r._get_cw_msg(3) == "", "invalid block is no macro")
check(all(r.get_memory(ch + 1).empty for ch in range(448, 454)),
      "channels 449-454 empty")
check(raw(r, 448 * 16, 6 * 16) == b'\xFF' * 96, "legacy macro bytes erased")
check(raw(r, 0x8000 + 448 * 2, 2) == b'\xFF\xFF', "attributes marked deleted")
check(raw(r, 455 * 16, 16) == CHANNEL_455, "channel 456 (block 3) untouched")
check(raw(r, 0xA208, 11) == CW_SETTINGS_V2, "CW settings unpacked to one byte each")
check(r._cw_get("sidetone") == 11, "sidetone code 2 is level 11")
check(raw(r, 0xA140, 8) == b'\xFF' * 8, "legacy CW settings cleared")
check(raw(r, 0xA170, 1) == b'\x02', "legacy schema byte left alone")
check(raw(r, m.MAIN_END, m.CW_MACRO_START - m.MAIN_END) ==
      b'\xFF' * (m.CW_MACRO_START - m.MAIN_END), "hole stays erased")

# Converting again is a no-op
before = r._mmap.get_packed()
r.process_mmap()
check(r._mmap.get_packed() == before, "second load changes nothing")

# --- schema 1 image (pre-v1.4) is upgraded through the new header ----------
r = load(legacy_image(schema=0xFF))
check(raw(r, 0xA204, 1) == b'\x02', "pre-v1.4 image upgraded to schema 2 in header")
check(raw(r, 0xA0A8, 5) == NEW_ACTIONS, "pre-v1.4 image: key actions renumbered")

# --- image from armel firmware: 0xA170 is a scan-list mask -----------------
r = load(legacy_image(schema=0x05, mix=True))
check(raw(r, 0xA204, 1) == b'\x02', "MIX signature: schema treated as current")
check(raw(r, 0xA0A8, 5) == OLD_ACTIONS, "MIX signature: key actions left alone")

# --- macro save writes the header when it is missing -----------------------
r = load(legacy_image())
r._mmap[0xD000] = b'\xFF' * 16
check(r._get_cw_msg(1) == "", "no macro header: no macros")
r._set_cw_msg(4, "NEW")
check(raw(r, 0xD000, 16) == m._NR7Y_CW_MACRO_HDR and r._get_cw_msg(4) == "NEW",
      "macro save writes the header")

# --- upload gate ------------------------------------------------------------
def gate(reply, firmware="Brass v2.0"):
    m._readmem = lambda port, addr, n: reply
    try:
        m._check_upload_target(None, firmware)
        return None
    except errors.RadioError as exc:
        return str(exc)

check(gate(b'\xFF' * 8) is not None, "upload refused: pre-v2.0 firmware")
check(gate(b'CWX\x02\x02\xFF\xFF\xFF') is not None, "upload refused: newer CW block")
check(gate(b'CWX\x01\x03\xFF\xFF\xFF') is not None, "upload refused: newer schema")
check(gate(b'CWX\x01\x02\xFF\xFF\xFF') is None, "upload allowed: v2.0 firmware")
check("Update the firmware" in (gate(b'\xFF' * 8, "NR7Y v1.4") or ""),
      "upload refused: v1.x NR7Y firmware, pointed at a firmware update")
check("only programs" in (gate(b'CWX\x01\x02\xFF\xFF\xFF', "F4HWN v6.3.0") or ""),
      "upload refused: other firmware family")

print()
print("FAIL (%d)" % len(failures) if failures else "PASS")
sys.exit(1 if failures else 0)
