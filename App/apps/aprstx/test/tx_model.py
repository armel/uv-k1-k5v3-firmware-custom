#!/usr/bin/env python3
"""Model of the APRS TX app: the frame and bit stream of aprstx_app.c rebuilt
from the real assets (gen_assets.py), checked against the AX.25 reference of
the RX app (ax25.py), then sent as AFSK and decoded by the APRS RX model
(model_rx.py) through a receiver audio path.

The tone generator is modelled with a phase-continuous NCO (expected: REG_71 is
rewritten, not the phase) and, as the worst case, with a phase reset at each
tone change. The TX tone path is assumed flat (no pre-emphasis): a receiver with
de-emphasis (STD path, FT3D) then sees 2200 Hz ~5 dB low, which the app's twist
setting (2200 Hz gain x (8 + tw) / 8) compensates.

  tx_model.py        runs the checks and the decode sweep
"""
import math, os, re, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "aprsrx", "test"))
from ax25 import build, hdlc_bits, crc16, decode
import model_rx as M

CALL = "F4HWN"            # boot message
TXDELAY_FLAGS, TAIL_FLAGS = 40, 3
LEAD_S = 0.050 + 1 / 1200 # tx_tone's 50 ms settle, then the first bit edge
DEV_MARK = 3000.0         # Hz of deviation for the mark tone at the chosen level


def assets():
    d = tempfile.mkdtemp()
    b, h = os.path.join(d, "a.bin"), os.path.join(d, "a.h")
    subprocess.run([sys.executable, os.path.join(HERE, "..", "gen_assets.py"), b, h], check=True)
    defs = dict(re.findall(r"#define (\w+) (\d+)u", open(h).read()))
    return open(b, "rb").read(), {k: int(v) for k, v in defs.items()}


def put_coord(d, h):
    """aprstx_app.c putCoord(): 4850.90N / 00216.25E"""
    n = len(d)
    return "".join(("." if i == n - 2 else "") + str(x) for i, x in enumerate(d)) + h


def pos_ok(d):
    """aprstx_app.c posOk()"""
    if any(x > 9 for x in d): return False
    la, lam, lo, lom = d[0] * 10 + d[1], d[2] * 10 + d[3], d[6] * 100 + d[7] * 10 + d[8], d[9] * 10 + d[10]
    if la > 90 or lam > 59 or lo > 180 or lom > 59: return False
    if la == 90 and (lam | d[4] | d[5]): return False
    if lo == 180 and (lom | d[11] | d[12]): return False
    return True


CW_COMPACT = 14                      # aprstx_app.c: the compact view


def cfg_pack(lvl, tw, pos, hemi, ssid, path, cw=0, sym=5):
    c = [0xA8, lvl, tw & 0xFF] + [0] * 10
    for i, x in enumerate(pos): c[3 + i // 2] |= x << ((i & 1) * 4)
    c[9] |= hemi << 4
    c[10] = 0x80 | path << 4 | ssid
    c[11] = cw
    c[12] = sym
    return bytes(c)


def cfg_unpack(c, D):
    """aprstx_app.c app_main(): position, hemispheres, SSID, path (the
    defaults when byte 10 is erased or from a v0.2 config), view (scroll unless
    byte 11 is CW_COMPACT) and symbol (default when byte 12 is erased)"""
    pos = [(c[3 + i // 2] >> ((i & 1) * 4)) & 15 for i in range(13)]
    cw = CW_COMPACT if c[11] == CW_COMPACT else 0
    h = c[10]
    if (h & 0xC0) == 0x80 and (h >> 4) & 3 <= 2:
        return pos, c[9] >> 4, h & 15, (h >> 4) & 3, cw, c[12] if c[12] < D["SYM_COUNT"] else D["SYM_INDEX"]
    return pos, c[9] >> 4, D["CFG_SSID"], D["CFG_PATH"], cw, c[12] if c[12] < D["SYM_COUNT"] else D["SYM_INDEX"]


def edit_row(label, d, h, cur):
    """aprstx_app.c editRow(): the text and the bold column (stops: the n
    digits, then the hemisphere at cur = n)"""
    o, col, n = label, None, len(d)
    for i, x in enumerate(d):
        if i == n - 4: o += " "
        if i == n - 2: o += "."
        if i == cur: col = len(o)
        o += str(x)
    o += " "
    if cur == n: col = len(o)
    return o + h, col


CUR_NS, CUR_EW, CUR_SSID, CUR_PATH, CUR_SYM = 6, 14, 15, 16, 17


def nav_dir(key, set_nav):
    """app_overlay.c app_nav_dir(): the raw UP ('U') / DOWN ('D') key as a value
    direction, +1 / -1 with SET_NAV, -1 / +1 without; 0 for any other key"""
    d = 1 if key == "U" else -1 if key == "D" else 0
    return d if set_nav else -d


def editor_keys(keys, ed, hemi=0, ssid=7, path=1, sym=5, cur=0):
    """aprstx_app.c handleKeys() in the editor: digits 0-9, '*', raw UP 'U' /
    DOWN 'D', 'F'"""
    ed = list(ed)
    farm = False
    for k in keys:
        if k == "F":
            farm = not farm
            continue
        back, farm = farm, False
        lon = CUR_NS < cur <= CUR_EW
        if k.isdigit():
            if cur < CUR_NS or (lon and cur < CUR_EW):
                ed[cur - lon] = int(k)
                cur += 1
                if cur == CUR_NS: cur += 1
                if cur == CUR_EW: cur -= 1
        elif k == "*":
            if cur <= CUR_EW: hemi ^= 2 if lon else 1
            elif cur == CUR_SSID: ssid = (ssid + (15 if back else 1)) & 15
            elif cur == CUR_PATH:
                path = path - 1 if back and path else 2 if back else path + 1 if path < 2 else 0
            elif back: sym = sym - 1 if sym else 47
            else: sym = sym + 1 if sym < 47 else 0
        else:
            d = (k == "D") - (k == "U")
            if d < 0 and cur: cur -= 1
            if d > 0 and cur < CUR_SYM: cur += 1
    return ed, hemi, ssid, path, sym, cur


def build_c(blob, D, call=CALL, pos=None, hemi=None, ssid=None, path=None, sym=None):
    """aprstx_app.c build()"""
    if not call or len(call) > 6 or "/" in call:
        return None
    rd = lambda name: blob[D[name]:D[name] + D[name + "_LEN"]]
    pd = rd("POS_DEF")
    pos = list(pd[:13]) if pos is None else pos
    hemi = pd[13] if hemi is None else hemi
    ssid = D["CFG_SSID"] if ssid is None else ssid
    path = D["CFG_PATH"] if path is None else path
    sym = D["SYM_INDEX"] if sym is None else sym
    pair = rd("SYM_CODES")[sym * 2:sym * 2 + 2].decode()
    info = "!" + put_coord(pos[:6], "S" if hemi & 1 else "N") + pair[0] + \
        put_coord(pos[6:], "W" if hemi & 2 else "E") + pair[1] + rd("F_COMMENT").decode()
    f = bytearray(rd("F_DEST"))
    f += bytes(ord(call[i]) << 1 if i < len(call) else 0x40 for i in range(6))
    f.append(0x60 | (ssid << 1) | (0 if path else 1))
    p = bytearray(rd("F_WIDE")[:7 * path])         # the first path WIDE entries
    if path: p[-1] |= 1                             # end of the address field
    f += p + b"\x03\xF0" + info.encode()
    c = 0xFFFF
    for x in f:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ 0x8408 if c & 1 else c >> 1
    c ^= 0xFFFF
    return bytes(f + bytes([c & 0xFF, c >> 8]))


def bits_c(frame):
    """aprstx_app.c sendByte()/sendBit(): the bits on the air, before NRZI"""
    out, ones = [], 0
    def byte(v, stuff):
        nonlocal ones
        for _ in range(8):
            b = v & 1; v >>= 1
            out.append(b)
            if stuff:
                if not b: ones = 0
                else:
                    ones += 1
                    if ones == 5: out.append(0); ones = 0
    for _ in range(TXDELAY_FLAGS): byte(0x7E, False)
    ones = 0
    for x in frame: byte(x, True)
    for _ in range(TAIL_FLAGS): byte(0x7E, False)
    return out


def afsk(bits, tw=0, reset=False, twist_path_db=0.0):
    """discriminator output (Hz) of the transmitted AFSK; tw as in the app,
    twist_path_db an extra 2200 Hz gain of the TX audio path (unknown)"""
    fs = M.FS_SIM
    a_sp = DEV_MARK * min(127, 66 * (8 + tw) // 8) / 66 * 10 ** (twist_path_db / 20)
    out, ph, space = [], 0.0, False
    for i in range(int(LEAD_S * fs)):                  # plain mark before the flags
        ph += 1200 / fs
        out.append(DEV_MARK * math.sin(2 * math.pi * ph))
    spb = fs / 1200
    t = 0.0
    for b in bits:
        if not b:
            space = not space
            if reset: ph = 0.0
        f, a = (2200, a_sp) if space else (1200, DEV_MARK)
        t += spb
        while len(out) < int(LEAD_S * fs + t):
            ph += f / fs
            out.append(a * math.sin(2 * math.pi * ph))
    return out


def main():
    blob, D = assets()
    ok = True
    f = build_c(blob, D)
    ssid = D["CFG_SSID"]
    WIDE = ["WIDE1-1", "WIDE2-1"]                                # gen_assets.py WIDE
    info = "!4850.90N/00216.25E[UV-K5/K1 F4HWN Firmware"          # gen_assets.py defaults
    ref = build("%s-%d" % (CALL, ssid) if ssid else CALL, dst="APZK5",
                path=WIDE[:D["CFG_PATH"]], info=info)
    print("frame  ", decode(f), "(%d bytes)" % len(f))
    ok &= f == ref
    print("frame == ax25.build:", f == ref)
    bc = bits_c(f)
    ok &= bc == hdlc_bits(f, TXDELAY_FLAGS, TAIL_FLAGS)
    print("bits  == ax25.hdlc_bits:", bc == hdlc_bits(f, TXDELAY_FLAGS, TAIL_FLAGS),
          "(%d bits, %.0f ms + %.0f ms lead)" % (len(bc), len(bc) / 1.2, LEAD_S * 1000))
    for bad in ("", "F4HWN/P", "F4HWN73"):
        ok &= build_c(blob, D, bad) is None
    print("bad boot callsigns refused:", True)

    # position editor: another position, south/west, the frame and the config
    pos = [3, 3, 5, 2, 1, 3, 1, 5, 1, 1, 2, 5, 6]           # 33 52.13S 151 12.56W
    f2 = build_c(blob, D, pos=pos, hemi=3)
    want = build("F4HWN-7", dst="APZK5", path=["WIDE1-1"],
                 info="!3352.13S/15112.56W[UV-K5/K1 F4HWN Firmware")
    ok &= f2 == want
    print("edited position frame:", f2 == want, decode(f2))
    # SSID and path edited: every path, SSID 0 (no suffix) and 15
    for sid, path in ((0, 0), (15, 1), (9, 2)):
        f3 = build_c(blob, D, ssid=sid, path=path)
        want = build("F4HWN-%d" % sid if sid else "F4HWN", dst="APZK5", path=WIDE[:path], info=info)
        ok &= f3 == want
        print("SSID %2d, path %d frame:" % (sid, path), f3 == want, decode(f3))
    f3 = build_c(blob, D, sym=0)
    want = build("F4HWN-7", dst="APZK5", path=["WIDE1-1"],
                 info="!4850.90N/00216.25E#UV-K5/K1 F4HWN Firmware")
    ok &= f3 == want
    print("selected /# symbol frame:", f3 == want, decode(f3))
    c = cfg_pack(70, -2, pos, 3, 9, 2, CW_COMPACT)
    good = len(c) == 13 and cfg_unpack(c, D) == (pos, 3, 9, 2, CW_COMPACT, 5)
    old = cfg_pack(70, -2, pos, 3, 0, 0)[:10]               # a v0.2 config: byte 10 erased or 0
    good &= all(cfg_unpack(old + bytes([b, 0xFF, 0xFF]), D) ==
                (pos, 3, D["CFG_SSID"], D["CFG_PATH"], 0, D["SYM_INDEX"])
                for b in (0xFF, 0x00))                      # byte 11 erased: scroll view
    ok &= good
    print("config round trip, v0.2 config -> default SSID and path, scroll view:", good, c.hex())
    rows = (edit_row("LAT  ", pos[:6], "S", 2), edit_row("LON ", pos[6:], "W", -1),
            edit_row("LAT  ", pos[:6], "S", CUR_NS),          # N/S in bold
            edit_row("LON ", pos[6:], "W", CUR_EW - 7),       # E/W in bold
            edit_row("LON ", pos[6:], "W", CUR_SSID - 7))     # cursor on SSID: none
    ok &= rows == (("LAT  33 52.13 S", 8), ("LON 151 12.56 W", None), ("LAT  33 52.13 S", 14),
                   ("LON 151 12.56 W", 14), ("LON 151 12.56 W", None))
    print("editor rows:", rows)
    # nav_dir() as app_overlay.c: UP +1 / DOWN -1 with SET_NAV, swapped without
    good = [nav_dir(k, sn) for sn in (True, False) for k in "UD*"] == [1, -1, 0, -1, 1, 0]
    # 13 digits typed in a row skip N/S and stay on the last digit; the
    # navigation keys reach N/S, E/W, SSID and path; '*' acts on the field under
    # the cursor. DOWN / RIGHT moves forward and UP / LEFT moves backward,
    # independently of SET_NAV.
    typed = editor_keys("3352131511256", [0] * 13)
    good &= typed == (pos, 0, 7, 1, 5, 13)
    good &= editor_keys("2*", pos)[:2] == ([2] + pos[1:], 1)              # '*' on a LAT digit
    N, P = "D", "U"                                                      # next / previous field
    k = lambda keys, **kw: editor_keys(keys, pos, **kw)
    good &= k(N)[5] == 1 and k(P)[5] == 0 and k(N + P)[5] == 0           # the raw keys' direction
    good &= k(P * 13 + N * 6 + "*")[1:] == (0 ^ 1, 7, 1, 5, CUR_NS)
    good &= k(N * 14 + "*" + "5")[:2] == (pos, 2)                        # E/W: '*', a digit ignored
    good &= k(N * 15 + "**")[2:] == (9, 1, 5, CUR_SSID)
    good &= k(N * 16 + "**" + N)[3:] == (0, 5, CUR_SYM)
    good &= k(N * 15 + "F*F*")[2] == 5                                   # F then '*': SSID back
    good &= k(N * 15 + "F*" * 8, ssid=3)[2] == 11                        # ... wrapping 0 -> 15
    good &= k(N * 16 + "F*F*F*")[3] == 1                                 # path back, 1 -> 0 -> 2 -> 1
    good &= k(N * 15 + "FF*")[2] == 8                                    # F twice: disarmed
    good &= k(N * 15 + "F" + N + "*")[3:] == (2, 5, CUR_PATH)            # F used by a move, '*' forward
    good &= k(N * 17 + "**")[4:] == (7, CUR_SYM)                         # symbol forward
    good &= k(N * 17 + "F*")[4:] == (4, CUR_SYM)                         # symbol backward
    ok &= good
    print("editor keys (DOWN/RIGHT forward):", good)
    checks = [([4,8,5,0,9,0,0,0,2,1,6,2,5], True), ([9,0,0,0,0,0,1,8,0,0,0,0,0], True),
              ([9,0,0,1,0,0,0,0,0,0,0,0,0], False), ([4,8,6,0,0,0,0,0,0,0,0,0,0], False),
              ([4,8,0,0,0,0,1,8,0,0,0,0,1], False), ([4,8,0,0,0,0,1,8,1,0,0,0,0], False),
              ([4,8,0,0,0,0,0,0,0,6,0,0,0], False)]
    good = all(pos_ok(d) == w for d, w in checks)
    ok &= good
    print("posOk limits:", good)

    print("\n%-6s %-5s %4s %6s %5s | frames (3 seeds)" % ("phase", "rx", "tw", "path", "noise"))
    for reset in (False, True):
        for mode in ("raw", "std"):
            for tw, path_db in ((0, 0.0), (4, 0.0), (0, 5.0)):
                for noise in (0, 1500):
                    w = afsk(bc, tw, reset, path_db)
                    n = 0
                    for seed in (1, 2, 3):
                        adc = M.channel(w, mode, noise, 0, 0, seed)
                        d = M.Demod()
                        for s in adc:
                            d.sample(s)
                        n += d.frames == [f]
                    print("%-6s %-5s %4d %+5.0fdB %5d | %d/3" % (
                        "reset" if reset else "cont", mode, tw, path_db, noise, n))
                    if not reset and noise == 0:
                        ok &= n == 3
    print("\nALL OK" if ok else "\nFAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
