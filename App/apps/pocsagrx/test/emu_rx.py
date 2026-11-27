#!/usr/bin/env python3
"""The built app (../pocsagrx.bin, ../pocsagrx_assets.bin) run on an emulated
Cortex-M0 (Unicorn) against a model of the BK4829 FSK receiver, fed with the
transmissions of flipper_pocsag.test_bits(): each page the app shows is
checked against the one sent.

The modem is test/bk4829_fsk.py. The app reaches the radio only through the
app_api_t table: every service is a Python stub. Writes outside the overlay, the stack and the frame buffer, and
characters outside 32-126 handed to the font, are failures.

  pip install unicorn
  python3 test/emu_rx.py          (after ./build.sh, or ../../../compile-app.sh pocsagrx)
"""
import os
import random
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pocsag
from bk4829_fsk import Modem
from flipper_pocsag import test_bits

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UcError
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M0, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

APP = os.path.join(HERE, "..")
VMA = 0x20000280
RAM, RAM_SZ = 0x20000000, 0x4000
API, FB, STATUS = 0x20002800, 0x20002C00, 0x20002F80
TRAP, EXIT = 0x30000000, 0x30000FC0
BAUD = 1200
KEY_DOWN, KEY_EXIT, KEY_INVALID = 12, 13, 19
CW_COMPACT = 14


def api_offsets():
    """app_api_t field offsets: after the 4-byte prefix every field is a pointer."""
    src = open(os.path.join(APP, "..", "app_api.h")).read()
    body = src[src.index("typedef struct app_api {"):src.index("} app_api_t;")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    names = []
    for stmt in body.split(";")[:-1]:
        stmt = " ".join(stmt.split()).split("{")[-1]
        m = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(", stmt) or re.search(r"(\w+)\s*$", stmt)
        names.append(m.group(1))
    assert names[:4] == ["abi_major", "api_level", "api_size", "fb"]
    off = {n: 4 + 4 * i for i, n in enumerate(names[3:])}
    assert off["append_i32"] == 328 and off["serial_read"] == 368 and off["gps_position"] == 404
    return off


class Radio:
    def __init__(self, line, cfg, keys, seed=1):
        self.us, self.keys, self.cfg, self.bad, self.screens, self.screen = 0, keys, list(cfg), [], [], {}
        self.modem = Modem(line, seed)
        self.modem.reg.update({0x2B: 0, 0x73: 0x4692})
        code = open(os.path.join(APP, "pocsagrx.bin"), "rb").read()
        self.assets = open(os.path.join(APP, "pocsagrx_assets.bin"), "rb").read()
        mu = self.mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        mu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
        mu.mem_map(RAM, RAM_SZ)
        mu.mem_map(TRAP, 0x1000)
        mu.mem_write(VMA, code)
        mu.mem_write(TRAP, b"\x70\x47\x00\xbf" * 0x400)          # bx lr; nop
        api = bytearray(408)
        api[0], api[1] = 1, 4
        struct.pack_into("<H", api, 2, 408)
        self.stub = {}
        for i, (name, off) in enumerate(api_offsets().items()):
            addr = {"fb": FB, "status_line": STATUS}.get(name, TRAP + 4 * i)
            struct.pack_into("<I", api, off, addr | (addr >= TRAP))
            self.stub[addr] = name
        mu.mem_write(API, bytes(api))
        mu.hook_add(UC_HOOK_CODE, self.call, begin=TRAP, end=TRAP + 0xFFF)
        mu.hook_add(UC_HOOK_MEM_WRITE, self.wrote)

    def bit_now(self):
        return self.us * BAUD // 1000000

    def wrote(self, mu, access, addr, size, value, ud):
        if not (VMA <= addr < VMA + 0x1000 or RAM + RAM_SZ - 0x1000 <= addr < RAM + RAM_SZ or
                FB <= addr < FB + 7 * 128 or STATUS <= addr < STATUS + 128):
            self.bad.append("write at %08x (pc %08x)" % (addr, mu.reg_read(UC_ARM_REG_PC)))

    def cstr(self, a):
        n = min(160, RAM + RAM_SZ - a) if RAM <= a < RAM + RAM_SZ else 160
        b = bytes(self.mu.mem_read(a, n))
        return b[:b.index(0)].decode("latin1")

    def call(self, mu, addr, size, ud):
        if addr == EXIT:
            mu.emu_stop()
            return
        name = self.stub.get(addr)
        if name is None:
            return
        r = [mu.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        ret = None
        self.us += 5
        if name == "bk_read":
            ret = self.modem.read(r[0] & 0xFF, self.bit_now())
            if (r[0] & 0xFF) == 0x0C and not ret:
                self.us += 200                       # idle polling: let time pass
        elif name == "bk_write":
            self.modem.write(r[0] & 0xFF, r[1] & 0xFFFF, self.bit_now())
        elif name == "ticks_ms":
            ret = self.us // 10000 * 10
        elif name == "get_key":
            self.us += 50
            ret = next((k for t, k in self.keys if t <= self.us // 1000 < t + 100), KEY_INVALID)
        elif name in ("append_i32", "append_u32"):
            v = r[1] - (1 << 32) if name == "append_i32" and r[1] >> 31 else r[1]
            t = str(v).encode()
            mu.mem_write(r[0], t)
            ret = r[0] + len(t)
        elif name == "asset_read":
            data = self.assets[r[0]:r[0] + r[2]]
            mu.mem_write(r[1], data)
            ret = len(data)
        elif name == "cfg_load":
            mu.mem_write(r[0], bytes(self.cfg[:r[1]]))
        elif name == "cfg_save":
            self.cfg = list(mu.mem_read(r[0], r[1]))
        elif name == "rssi_dbm":
            ret = -85
        elif name == "rx_freq":
            ret = 43365000
        elif name in ("print_bold", "print_normal", "print_tiny", "print_inverse"):
            s = self.cstr(r[0])
            if any(not 32 <= ord(c) <= 126 for c in s):
                self.bad.append("%s(%r)" % (name, s))
            key = {"print_bold": ("bold", r[3]), "print_normal": ("normal", r[3]),
                   "print_tiny": ("tiny", 99 if r[3] else r[2]), "print_inverse": ("inverse", r[2])}[name]
            self.screen[key] = s
        elif name == "display_clear":
            self.screen = {}
        elif name == "blit_full":
            self.us += 11000
            self.screens.append((self.us // 1000, dict(self.screen)))
        elif name in ("status_clear", "draw_battery", "blit_status", "backlight_on", "backlight_update",
                      "battery_sample", "audio_path", "set_af", "led", "delay_ms", "draw_frequency"):
            pass
        else:
            raise RuntimeError("unexpected service " + name)
        if ret is not None:
            mu.reg_write(UC_ARM_REG_R0, ret & 0xFFFFFFFF)

    def run(self, ms):
        self.keys.append((ms, KEY_EXIT))
        mu = self.mu
        mu.reg_write(UC_ARM_REG_SP, RAM + RAM_SZ - 16)
        mu.reg_write(UC_ARM_REG_LR, EXIT | 1)
        mu.reg_write(UC_ARM_REG_R0, API)
        try:
            mu.emu_start(VMA | 1, EXIT, count=500_000_000)
        except UcError as e:
            raise RuntimeError("fault %s at pc %08x" % (e, mu.reg_read(UC_ARM_REG_PC)))

    def shown(self, t_ms):
        sc = {}
        for t, s in self.screens:
            if t <= t_ms:
                sc = s
        return sc


def pages_on_screen(line, n, tones=True):
    """Play the transmission (from 1 s), then walk the n newest pages with DOWN
    in the compact view; -> [(ric, text)] oldest first, and the radio."""
    end = 1000 + len(line) * 1000 // BAUD + 1500
    keys = [(end + 400 * i, KEY_DOWN) for i in range(1, n)]   # the newest is shown first
    rng = random.Random(7)
    padded = [rng.getrandbits(1) for _ in range(BAUD)] + list(line)   # 1 s of noise first
    radio = Radio(padded, [CW_COMPACT, 0, int(tones)], keys)
    radio.run(end + 400 * n + 300)
    out = []
    for i in range(n):
        sc = radio.shown(end + 400 * i + 300)
        if ("bold", 0) not in sc:
            continue
        rows = "".join(sc.get(("tiny", 8 + 6 * r), "") for r in range(4))
        if sc.get(("normal", 2)) in ("Tone Only", "Text Lost"):
            rows = ""
        out.append((int(sc[("bold", 0)]), rows))
    return out[::-1], radio


def main():
    fails = 0
    for name, (line, pages) in test_bits().items():
        want = [(ric, text.rstrip(" ") if func == pocsag.FUNC_NUMERIC and not name.startswith("func0alpha")
                 else text) for ric, func, text in pages]
        got, radio = pages_on_screen(line, len(want))
        ok = got == want and not radio.bad
        fails += not ok
        print("%-5s %-13s %s" % ("ok" if ok else "FAIL", name,
                                 "%d page(s)" % len(got) if ok else "got %r want %r %s" % (got, want, radio.bad[:3])))
    # without key 4, a tone-only page is not kept
    got, radio = pages_on_screen(test_bits()["tone"][0], 1, tones=False)
    ok = not got and not radio.bad
    fails += not ok
    print("%-5s %-13s %s" % ("ok" if ok else "FAIL", "tone, key 4 off", got))
    # noise only: no page, no write outside the app
    radio = Radio([], [0, 0, 1], [])
    radio.run(20000)
    sc = radio.shown(19900)
    ok = ("bold", 0) not in sc and not radio.bad
    fails += not ok
    print("%-5s %-13s %s" % ("ok" if ok else "FAIL", "20 s of noise", sc.get(("tiny", 49))))
    print("\n%d failure(s)" % fails)
    return fails


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
