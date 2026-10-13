"""Test the SelCall CHIRP driver against the real CHIRP classes (no radio, no GUI).

Setup (once):
    git clone --depth 1 https://github.com/kk7ds/chirp.git /tmp/chirpsrc
    python3 -m venv /tmp/chirpvenv && /tmp/chirpvenv/bin/pip install lark pyserial requests yattag suds
Run:
    /tmp/chirpvenv/bin/python tools/selcall/chirp/test_chirp_selcall.py /tmp/chirpsrc/.. \\
        tools/selcall/chirp/f4hwn.chirp.v6.2.0-SelCall.py path/to/original/f4hwn.chirp.v6.1.0.py
(the first argument is the directory that contains the `chirp` checkout)
"""
import sys, types, importlib.util
S = sys.argv[1]; DRV = sys.argv[2]; ORIG = sys.argv[3]
sys.path.insert(0, S + "/chirp")   # S/chirp = the CHIRP source checkout
sys.modules["wx"] = types.ModuleType("wx")          # GUI not needed
from chirp import chirp_common, memmap, errors
from chirp.settings import InvalidValueError

def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m

fails = 0
def check(label, cond, extra=""):
    global fails
    print(("ok   " if cond else "FAIL ") + label + (("  " + str(extra)) if extra else ""))
    fails += (not cond)

drv = load(DRV, "selcall_drv")
R = drv.UVK5RadioEgzumer

def radio_from(data):
    r = R(None)
    r._mmap = memmap.MemoryMapBytes(data)
    r.process_mmap()
    return r

# 1. old-size image (original driver) is padded and parses, selcall reads OFF / empty
old = bytearray(b"\xFF" * drv.MEM_SIZE)
r = radio_from(bytes(old))
check("old image padded to IMAGE_SIZE", len(r._mmap) == drv.IMAGE_SIZE, hex(len(r._mmap)))
m = r.get_memory(1)
ex = {s.get_name(): str(s.value) for s in m.extra}
check("empty ch: selcall extras present", all(k in ex for k in ("selcall_type", "selcall_code1", "selcall_code2")))
check("empty ch: type OFF, codes empty", ex["selcall_type"] == "OFF" and ex["selcall_code1"] == "" and ex["selcall_code2"] == "", ex)

# 2. program channel 1 (433.650 FM) with ZVEI-1, 01234 / 11223 and read back
m.empty = False
m.freq = 433650000
m.mode = "FM"
m.power = r.get_features().valid_power_levels[0]
m.extra["selcall_type"].value = "ZVEI-1"
m.extra["selcall_code1"].value = "01234"
m.extra["selcall_code2"].value = "11223"
r.set_memory(m)
raw = r._mmap.get_packed()[0xD000:0xD008]
check("raw record ch1", raw == bytes([1, 0xD2, 0x04, 0x00, 0xD7, 0x2B, 0x00, 0xFF]), raw.hex())
m2 = r.get_memory(1)
ex2 = {s.get_name(): str(s.value) for s in m2.extra}
check("roundtrip type/codes", (ex2["selcall_type"], ex2["selcall_code1"], ex2["selcall_code2"]) == ("ZVEI-1", "01234", "11223"), ex2)
check("channel itself intact", m2.freq == 433650000 and not m2.empty, m2.freq)

# 3. clear code 2, ZVEI-2
m2.extra["selcall_type"].value = "ZVEI-2"
m2.extra["selcall_code2"].value = ""
r.set_memory(m2)
raw = r._mmap.get_packed()[0xD000:0xD008]
check("code 2 cleared -> 0xFFFFFF", raw == bytes([2, 0xD2, 0x04, 0x00, 0xFF, 0xFF, 0xFF, 0xFF]), raw.hex())

# 4. channel 1024 (last MR) -> record 1023 at 0xD000 + 1023*8
m = r.get_memory(1024); m.empty = False; m.freq = 145500000; m.mode = "FM"
m.power = r.get_features().valid_power_levels[0]
m.extra["selcall_type"].value = "ZVEI-2"; m.extra["selcall_code1"].value = "99999"
r.set_memory(m)
off = 0xD000 + 1023 * 8
raw = r._mmap.get_packed()[off:off + 8]
check("ch1024 -> record 1023", raw[:4] == bytes([2, 0x9F, 0x86, 0x01]), raw.hex())

# 4b. CCIR-2 on channel 2 (type value 4)
m = r.get_memory(2); m.empty = False; m.freq = 433500000; m.mode = "FM"
m.power = r.get_features().valid_power_levels[0]
m.extra["selcall_type"].value = "CCIR-2"; m.extra["selcall_code1"].value = "00042"
r.set_memory(m)
raw = r._mmap.get_packed()[0xD008:0xD010]
check("ch2 CCIR-2 00042", raw[:4] == bytes([4, 42, 0, 0]), raw.hex())
check("ch2 reads back CCIR-2", str(r.get_memory(2).extra["selcall_type"].value) == "CCIR-2")

# 5. deleting channel 1 clears its record
m = r.get_memory(1); m.empty = True; r.set_memory(m)
raw = r._mmap.get_packed()[0xD000:0xD008]
check("delete clears record", raw == b"\xFF" * 8, raw.hex())

# 6. validation: 3 digits refused, letters refused, empty accepted
m = r.get_memory(1)
setting = m.extra["selcall_code1"]
for bad in ("123", "12a45", "123456"):
    try:
        setting.value = bad; ok = False
    except (InvalidValueError, ValueError):
        ok = True
    check("refuses %r" % bad, ok)
try:
    setting.value = ""; check("accepts empty", True)
except Exception as e:
    check("accepts empty", False, e)

# 7. key actions
check("key action 24/25", drv.KEYACTIONS_LIST[24:26] == ["SELCALL 1", "SELCALL 2"] and len(drv.KEYACTIONS_LIST) == 26)

# 8. VFO specials map to records 1024.. (ch_num = 1024 + band*2 + vfo)
specials = r._get_specials() if hasattr(r, "_get_specials") else {}
if specials:
    name, ch_num = sorted(specials.items(), key=lambda kv: kv[1])[0]
    mv = r.get_memory(name)
    names = [s.get_name() for s in mv.extra]
    check("VFO special %s (ch_num %d) has selcall extras" % (name, ch_num), "selcall_type" in names and ch_num == 1024)

# 9. upload: areas written (fake serial), calibration off
writes = []
drv._sayhello = lambda sp: "F4HWN v6.1.0"
drv._writemem = lambda sp, data, addr: writes.append((addr, len(data)))
drv._resetradio = lambda sp: None
class FakePort:
    timeout = 0
r.pipe = FakePort()
r.status_fn = lambda s: None
r.upload_calibration = False
drv.do_upload(r)
starts = sorted({a for a, _ in writes})
areas = []
for a, n in sorted(writes):
    if areas and areas[-1][1] == a: areas[-1][1] = a + n
    else: areas.append([a, a + n])
check("upload areas", [tuple(x) for x in areas] == [(0, drv.PROG_SIZE), (0xD000, 0xF080)], [(hex(a), hex(b)) for a, b in areas])

# 10. download size
drv._readmem = lambda sp, addr, n: b"\xFF" * n
img = drv.do_download(r)
check("download size", len(img) == 0xF080, hex(len(img)))

# Cross-compatibility with the original driver: see test_compat_original.py.

print("PASSED" if not fails else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)
