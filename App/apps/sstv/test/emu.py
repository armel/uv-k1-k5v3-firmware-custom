#!/usr/bin/env python3
"""Run the built SSTV overlay (sstv.elf, Thumb, Cortex-M0+) in Unicorn with a
stubbed app_api_t and an emulated SysTick and ADC, and check it against
model.py: the TX tone sequence (REG_71 writes) and the received picture, pixel
for pixel, for a few channel cases.

Time T (48 MHz cycles) advances on every cycle-clock read (K cycles) and on the
stubbed services that cost time on the radio (key scan, blits, delays), so the
key/screen slots skip samples as on the radio.

  python3 -m venv venv && venv/bin/pip install unicorn pyelftools
  venv/bin/python emu.py [DIR]    DIR holds sstv.elf and sstv_assets.bin (default ..)
"""
import struct, sys
from unicorn import *
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile

import os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import model as m

BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..")
K = 1500                                  # cycles per SysTick read (< a 5000-cycle sample slot)
BLIT_PAGE = 72000                         # one LCD page: 128 B x ~11.25 us (1.5 ms)

API_NAMES = """fb display_clear status_clear draw_line draw_rect print_bold print_tiny
blit_full blit_line blit_status get_key delay_ms play_tone led print_normal
print_inverse display_freq rssi_dbm bk_read bk_write set_agc set_af audio_path
prepare_tone play_tone_raw tones_off_rx rx_freq cfg_load cfg_save draw_battery
battery_sample backlight_on backlight_update audio_scope status_line tx_state
tx_set_params tx_tone tx_mute tx_end tx_carrier tx_freq boot_callsign print_string
fm_enter fm_exit fm_set_freq fm_lo fm_hi fm_mute fm_valid fm_channels fm_state
fm_commit nav_dir beam_prepare beam_leave beam_get beam_save beam_send beam_rx
beam_rx_poll beam_draw ticks_ms rand32 asset_read idivmod uidivmod sys_edition
sys_version sys_build_date sys_build_time sys_build_commit sys_flash_end sys_ram_end
sys_battery_voltage sys_battery_type sys_battery_percent sys_storage_read
sys_stack_free_now sys_stack_free_min append_i32 af_capture_open af_capture_sample
af_capture_close cycles_48mhz draw_frequency append_u32 handoff handoff_read
tx_prepare serial_read serial_write serial_ctl blit_page lcd_feed viewer_update""".split()
API_SIZE = 4 + 4 * len(API_NAMES)
assert API_SIZE == 392

RAM, RAM_SZ = 0x20000000, 0x40000
API = 0x20030000
STUB = 0x20031000
FB = 0x20032000          # 7 x 128
SL = 0x20032400          # 128
MAGIC = 0x20033000       # return address of app_main

class Emu:
    def __init__(self, adc=None, keys=None, logo=None):
        self.adc = adc or []
        self.keys = keys or (lambda emu: 19)
        self.logo = logo or bytes(1024)
        self.T = 0
        self.writes = []        # (T, reg, value)
        self.cfg = b"\xff\xff\xff\xff"   # erased: no saved picture
        self.saved = None
        self.assets = open(BUILD + "/sstv_assets.bin", "rb").read()
        self.blits = 0
        u = self.u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        u.mem_map(RAM, RAM_SZ)
        for base in (0x40007000, 0x40012000, 0x40013000, 0x40021000, 0x50000000, 0xE000E000):
            u.mem_map(base, 0x1000)
        u.mem_write(0xE000E014, struct.pack("<I", 479999))
        u.mem_write(0x40013008, struct.pack("<I", 3))  # SPI1 TXE | RXNE
        with open(BUILD + "/sstv.elf", "rb") as f:
            elf = ELFFile(f)
            for seg in elf.iter_segments():
                if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
                    u.mem_write(seg["p_paddr"], seg.data())
            self.sym = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}
        tab = bytearray(struct.pack("<BBH", 1, 3, API_SIZE))
        self.stubs = {}
        for i, name in enumerate(API_NAMES):
            if name == "fb":
                tab += struct.pack("<I", FB)
            elif name == "status_line":
                tab += struct.pack("<I", SL)
            else:
                a = STUB + 4 * i
                u.mem_write(a, b"\x70\x47\x00\xbf")     # bx lr; nop
                self.stubs[a] = name
                tab += struct.pack("<I", a | 1)
        u.mem_write(API, bytes(tab))
        u.mem_write(MAGIC, b"\x00\xbf\x00\xbf")
        u.hook_add(UC_HOOK_CODE, self.on_stub, begin=STUB, end=STUB + 4 * len(API_NAMES))
        u.hook_add(UC_HOOK_MEM_READ, self.on_syst, begin=0xE000E018, end=0xE000E01B)
        u.hook_add(UC_HOOK_MEM_READ, self.on_adc, begin=0x40012400, end=0x40012453)
        u.hook_add(UC_HOOK_MEM_WRITE, self.on_spi, begin=0x4001300C, end=0x4001300F)

    def on_syst(self, u, access, addr, size, value, data):
        self.T += K                         # the app's busy-wait loops advance time
        u.mem_write(0xE000E018, struct.pack("<I", 479999 - self.T % 480000))

    def on_adc(self, u, access, addr, size, value, data):
        off = addr - 0x40012400
        if off == 0x00:
            u.mem_write(addr, struct.pack("<I", 2))
        elif off == 0x50:
            i = self.T // 5000
            v = self.adc[i] if i < len(self.adc) else 2048
            u.mem_write(addr, struct.pack("<I", v))

    def on_spi(self, u, access, addr, size, value, data):
        self.T += 512                       # one byte at SPI 750 kHz

    def r(self, n):
        return self.u.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[n])

    def ret(self, v):
        self.u.reg_write(UC_ARM_REG_R0, v & 0xFFFFFFFF)

    def on_stub(self, u, addr, size, data):
        name = self.stubs.get(addr)
        if name is None:
            return
        if name == "get_key":
            self.T += 19200
            self.ret(self.keys(self))
        elif name in ("blit_full",):          # 7 x 128 B at SPI 750 kHz (DIV64)
            self.T += BLIT_PAGE * 7; self.blits += 1
        elif name in ("blit_status", "blit_line", "blit_page"):
            self.T += BLIT_PAGE
        elif name == "delay_ms":
            self.T += self.r(0) * 48000
        elif name == "bk_write":
            self.writes.append((self.T, self.r(0), self.r(1) & 0xFFFF))
        elif name == "tx_tone":
            self.writes.append((self.T, 0x71, m.reg71(self.r(0))))
            self.T += 50 * 48000
        elif name in ("display_clear",):
            u.mem_write(FB, bytes(896))
        elif name in ("status_clear",):
            u.mem_write(SL, bytes(128))
        elif name == "cycles_48mhz":          # the resident cycle clock (level 3)
            self.T += K                       # the app's busy-wait loops advance time
            self.ret(self.T)
        elif name == "af_capture_sample":     # the resident PA4 capture (level 3)
            i = self.T // 5000
            self.ret(self.adc[i] if i < len(self.adc) else 2048)
        elif name in ("append_i32", "append_u32"):
            v = self.r(1)
            if name == "append_i32":
                v = struct.unpack("<i", struct.pack("<I", v))[0]
            t = str(v).encode()
            u.mem_write(self.r(0), t)
            self.ret(self.r(0) + len(t))
        elif name == "ticks_ms":              # the 10 ms SysTick interrupt count
            self.ret(self.T // 480000 * 10)
        elif name == "rx_freq":
            self.ret(14450000)
        elif name == "tx_state":
            self.ret(0)
        elif name == "cfg_load":
            u.mem_write(self.r(0), self.cfg[: self.r(1)])
        elif name == "cfg_save":
            self.saved = bytes(u.mem_read(self.r(0), self.r(1)))
        elif name == "asset_read":
            off, buf, n = self.r(0), self.r(1), self.r(2)
            d = self.assets[off:off + n]
            u.mem_write(buf, d)
            self.ret(len(d))
        elif name == "idivmod":
            n = struct.unpack("<i", struct.pack("<I", self.r(0)))[0]
            d = struct.unpack("<i", struct.pack("<I", self.r(1)))[0]
            q = m.cdiv(n, d)
            rem = n - q * d if d else n
            u.reg_write(UC_ARM_REG_R0, q & 0xFFFFFFFF)
            u.reg_write(UC_ARM_REG_R1, rem & 0xFFFFFFFF)
        elif name == "sys_storage_read":
            a, buf, n = self.r(0), self.r(1), self.r(2)
            flash = bytearray(0x12000)
            flash[0x11008:0x11008 + 1024] = self.logo
            u.mem_write(buf, bytes(flash[a:a + n]))
        elif name in ("print_bold", "print_normal", "print_tiny", "print_inverse",
                      "display_freq", "draw_battery", "battery_sample", "backlight_on",
                      "backlight_update", "set_af", "audio_path", "tx_set_params",
                      "tx_mute", "tx_end", "af_capture_open", "af_capture_close",
                      "draw_frequency"):
            pass
        else:
            raise RuntimeError("unexpected API call " + name)

    def run(self):
        u = self.u
        u.reg_write(UC_ARM_REG_SP, RAM + 0x30000 - 16)
        u.reg_write(UC_ARM_REG_R0, API)
        u.reg_write(UC_ARM_REG_LR, MAGIC | 1)
        u.emu_start(self.sym["app_main"] | 1, MAGIC)
        return self

    def screen(self):
        sl = bytes(self.u.mem_read(SL, 128))
        fb = bytes(self.u.mem_read(FB, 896))
        pages = sl + fb
        return [[pages[(y >> 3) * 128 + x] >> (y & 7) & 1 for x in range(128)] for y in range(64)]

def tx_test(logo, mode):
    """MENU at once in TX mode `mode`; quit once the TX is over."""
    ev = m.tx_events(logo, mode)
    secs = sum(c for _, c in ev) / m.CPU + 2
    state = {"n": 0}
    def keys(e):
        state["n"] += 1
        if state["n"] == 1:
            return 10                      # MENU
        if e.T > 48_000_000 * secs:
            return 13                      # EXIT
        return 19
    e = Emu(keys=keys, logo=logo)
    e.cfg = bytes([1, mode, 0])
    e.run()
    tones = [(t, v) for t, reg, v in e.writes if reg == 0x71]
    # expected: model events with the same consecutive frequency merged
    exp, last = [], None
    t = 0
    for f, cyc in ev:
        if f != last:
            exp.append((t, m.reg71(f)))
            last = f
        t += cyc
    t0 = tones[0][0] + 50 * 48000          # the tx_tone settle, then clkStart()
    got = [(tt - t0, v) for tt, v in tones[1:]]
    exp = exp[1:]                          # the first 1900 Hz is tx_tone's
    assert [v for _, v in got] == [v for _, v in exp], (len(got), len(exp))
    lag = [g[0] - x[0] for g, x in zip(got, exp)]
    print(f"TX {m.M.MODES[mode][0]:10s}: {len(got)} REG_71 writes, sequence == model; write lag "
          f"{min(lag)}..{max(lag)} cycles (constant: no drift); LCD blits {e.blits}")

def rx_test(logo, sstv, name, sk, ck, mode=1):
    """sstv: the mode sent; mode: the saved rendering, 0 dither, 1 1-bit (default)."""
    m.THR = [b * 16 + 8 for b in m.BAYER] if mode == 0 else [128] * 16
    adc = m.channel(m.synth(m.tx_events(logo, sstv), **sk), **ck)
    ref = m.Rx().run(adc)
    end_T = len(adc) * 5000
    def keys(e):
        return 13 if e.T > end_T else 19
    e = Emu(adc=adc, keys=keys, logo=logo)
    e.cfg = bytes([mode, 0, 0])
    e.run()
    scr = e.screen()
    diff = sum(scr[y][x] != ref.screen[y][x] for y in range(64) for x in range(128))
    err = sum(scr[y][x] != m.logo_px(logo, x, y) for y in range(64) for x in range(128))
    print(f"RX {m.M.MODES[sstv][0]:10s} {name:8s} {('dither', '1-bit')[mode]}: C vs model {diff} px, C vs logo {err} px, model vs logo "
          f"{m.compare(logo, ref)} px, cfg saved {e.saved}")

def key_test(press_ms=120):
    """Receiver noise only; key 2 pressed press_ms at 5 s, EXIT at 8 s: the
    rendering saved must have toggled (1-bit -> dither)."""
    adc = m.channel([0.0] * (48000 * 10), snr_db=-20, seed=3)
    t3 = 5 * 48_000_000
    def keys(e):
        if t3 <= e.T < t3 + press_ms * 48000:
            return 2
        return 13 if e.T > 8 * 48_000_000 else 19
    e = Emu(adc=adc, keys=keys)
    e.cfg = bytes([1, 0, 0])
    e.run()
    print(f"keys: {press_ms} ms press of 2 on receiver noise -> rendering saved "
          f"{e.saved[0]} ({'toggled' if e.saved[0] == 0 else 'MISSED'})")

if __name__ == "__main__":
    logo = m.test_logo()
    key_test()
    idx = m.mode_index
    for name in ("Robot 36", "Scottie S1", "PD120"):
        tx_test(logo, idx(name))
    for sstv, name, sk, ck in [("Robot 36", "std", {}, {}), ("PD120", "std", {}, {}),
                               ("PD120", "tx +1%", dict(ppm=10000), {}),
                               ("PD120", "snr 12", {}, dict(snr_db=12)),
                               ("Martin M1", "std", {}, {}), ("Scottie S1", "std", {}, {})]:
        rx_test(logo, idx(sstv), name, sk, ck)
    rx_test(logo, idx("Robot 36"), "snr 20", {}, dict(snr_db=20), mode=0)
