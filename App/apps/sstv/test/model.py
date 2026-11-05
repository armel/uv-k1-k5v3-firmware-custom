#!/usr/bin/env python3
"""Pure-Python model of the SSTV app, written to be transliterated to C: the
TX tone schedule as the app keys it, a radio channel, and the integer receiver
the app runs on the PA4 ADC samples. The modes are in modes.py (Dayton paper).

TX: the boot logo (128x64, 1 bit) in the chosen mode. Each scan carries the
logo row in 128 equal columns (scan / 128: whole cycles at 48 MHz); the 64 rows
are spread over the periods by an accumulator (racc += A, a new row each time
racc >= B). Dark LCD pixel -> 1500 Hz (black), clear -> 2300 Hz (white); every
colour scan carries the same row and the chroma is neutral (1900 Hz): grey.
  VIS: 1900 Hz 300 ms, 1200 Hz 10 ms, 1900 Hz 300 ms, start bit 1200 Hz 30 ms,
  7 bits LSB first + even parity (1100 Hz = 1, 1300 Hz = 0), stop 1200 Hz 30 ms.

RX (per ADC sample, 9.6 kHz):
  band-pass (APRS RX's biquad, 4x the gain) -> y
  p = y1 (y0 + y2), q = y1^2: for a sine of angular step w, p / q = 2 cos w,
  whatever its amplitude (ratio R = 256 P / Q = 512 cos w: 1100 Hz 380,
  1200 Hz 362, 1300 Hz 338, 1500 Hz 285, 1900 Hz 164, 2300 Hz 33).
  P, Q smoothed (1/8 per sample) give the per-sample flags:
    sync    4P > 5Q          (below ~1360 Hz)
    leader  2P > Q, 4P < 3Q  (~1800-2020 Hz)
    VIS 1   32P > 45Q        (below ~1210 Hz)
  The VIS byte picks the mode record (modes.record). Each period starts at the
  sync end (within +-10 ms of the prediction, else the prediction; a late sync
  restarts the period; after 3 periods without one, a sync anywhere does); its scans start at off[i] samples from there, scaled to
  the tracked period; pixel: lum = 285 - 256 sum(p) / sum(q), 0..255, weighted
  per scan (sum 4) into the line buffer; periods averaged per screen row, then
  thresholded (1-bit) or Bayer 4x4 dithered.

  model.py [DIR] [MODE...]       the test sweep (pictures as PGM into DIR)
  model.py wav F.wav [MODE] [LOGO.bin]
                                 the transmission as a 48 kHz WAV (the test
                                 logo, or a 1024 B / 1032 B boot logo dump), to
                                 play into the Robot36 app, MMSSTV or QSSTV
"""
import math, os, random, struct, sys, wave
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True
import modes as M

CPU = 48_000_000
FS = 9600
CPS = CPU // FS                     # 5000 cycles per ADC sample
MS = CPU // 1000                    # 48000 cycles per ms
reg71 = M.reg71

def mode_index(name):
    return [m[0] for m in M.MODES].index(name)

# ------------------------------------------------------------------ TX ------

LEAD_IN = 1000                      # ms of 1900 Hz before the VIS (sstv_app.c)

def vis_events(vis):
    ev = [(1900, 300 * MS), (1200, 10 * MS), (1900, 300 * MS), (1200, 30 * MS)]
    code = M.vis_byte(vis)
    for i in range(8):
        ev.append((1100 if code >> i & 1 else 1300, 30 * MS))
    ev.append((1200, 30 * MS))
    return ev

def tx_events(logo, mode=0):
    """(Hz, cycles) as the app keys them, in mode index `mode`."""
    name, vis, segs, w, periods, pre = M.MODES[mode]
    A, B = M.rows(periods)
    ev = vis_events(vis)
    ev[0] = (1900, (LEAD_IN + 300) * MS)    # the app's lead-in on the first leader
    if pre:
        ev.append((1200, M.cycles(pre)))
    row, acc = 0, 0
    for per in range(periods):
        for k, d in segs:
            if k == M.SCAN:
                pc = M.cycles(d) // 128
                for x in range(128):
                    dark = logo[(row >> 3) * 128 + x] >> (row & 7) & 1
                    ev.append((1500 if dark else 2300, pc))
            else:
                f = (2300 if per & 1 else 1500) if k == M.ALT else k
                ev.append((f, M.cycles(d)))
        acc += A
        if acc >= B:
            acc -= B
            row += 1
    return ev

# ------------------------------------------------------------- channel ------

FS_SIM = 48000
LSB_PER_HZ = 0.065                  # APRS RX model: 3 kHz deviation ~ 195 LSB

def synth(events, fs=FS_SIM, ppm=0.0, amp=1.0, lead_s=0.5, tail_s=1.0):
    """Phase-continuous tones at fs; the TX clock off by ppm (its cycles are
    longer when ppm > 0). Silence (amp 0) around: 1 s after, for the receiver
    to finish Scottie's last period (its G and B scans come after the end)."""
    k = 1.0 + ppm * 1e-6
    out = [0.0] * int(lead_s * fs)
    ph, t_end = 0.0, 0.0
    for f, cyc in events:
        t_end += cyc * k / CPU
        n_end = int(round(t_end * fs))
        while len(out) - int(lead_s * fs) < n_end:
            ph += f / fs
            out.append(amp * math.sin(2 * math.pi * ph))
    out += [0.0] * int(tail_s * fs)
    return out

def _lp(x, fc, fs):
    w = math.tan(math.pi * fc / fs)
    q = 1 / math.sqrt(2)
    n = 1 / (1 + w / q + w * w)
    b0 = w * w * n; a1 = 2 * (w * w - 1) * n; a2 = (1 - w / q + w * w) * n
    y, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
    for v in x:
        o = b0 * (v + 2 * x1 + x2) - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y.append(o)
    return y

def _hp(x, fc, fs):
    a = math.exp(-2 * math.pi * fc / fs)
    y, acc, prev = [], 0.0, 0.0
    for v in x:
        acc = a * (acc + v - prev)
        prev = v
        y.append(acc)
    return y

def _deemph(x, fs, tau_us=750.0, ref_hz=1000.0):
    k = 1 - math.exp(-1 / (fs * tau_us * 1e-6))
    g = math.sqrt(1 + (2 * math.pi * ref_hz * tau_us * 1e-6) ** 2)
    y, acc = [], 0.0
    for v in x:
        acc += k * (v - acc)
        y.append(acc * g)
    return y

def channel(audio, dev=3000.0, snr_db=None, seed=1, deemph=True):
    """Receiver audio path (300 Hz high-pass, 750 us de-emphasis, 3 kHz
    low-pass) then the 12-bit ADC at 9.6 kHz around 2048."""
    rnd = random.Random(seed)
    x = [v * dev for v in audio]
    if snr_db is not None:
        sd = dev / math.sqrt(2) / 10 ** (snr_db / 20)
        x = [v + rnd.gauss(0, sd) for v in x]
    x = _hp(x, 300, FS_SIM)
    if deemph:
        x = _deemph(x, FS_SIM)
    x = _lp(x, 3000, FS_SIM)
    step = FS_SIM // FS
    return [min(4095, max(0, int(round(2048 + LSB_PER_HZ * v + rnd.gauss(0, 1)))))
            for v in x[::step]]

# -------------------------------------------------------------- receiver ----

BP_B0, BP_A1, BP_A2 = 4 * 5356, -10714, 5673 # APRS RX band-pass, Q14, 4x its gain
SYNC_WIN = 96                                 # +-10 ms around the prediction
LEAD_MIN = 384                                # leader run: 40 ms
VIS_RUN = 192                                 # start bit run: 20 ms
MISS_MAX = 20                                 # periods without a sync: lost
RELOCK = 3                                    # periods without a sync: take one anywhere
BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
THR = [b * 16 + 8 for b in BAYER]             # 4x4 dither thresholds (assets)
RECORDS = [M.record(i)[0] for i in range(len(M.MODES))]

def cdiv(a, b):
    """C division (toward zero); x / 0 = 0 as the API's idivmod."""
    if b == 0:
        return 0
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q

def lum(sp, sq):
    while sq >= 1 << 22:
        sp >>= 1
        sq >>= 1
    v = 285 - cdiv(sp * 256, sq)
    return 0 if v < 0 else 255 if v > 255 else v

class Rx:
    """The C receiver, sample by sample, field for field (g.* in sstv_app.c).
    screen: 64 rows x 128 bits (1 = dark), the app's img buffer."""
    H_HUNT, H_VIS, H_LINE = range(3)

    def __init__(self):
        self.x1 = self.x2 = self.y1 = self.y2 = 0
        self.P = self.Q = 0
        self.st = self.H_HUNT
        self.lead = 0             # leader run (samples)
        self.leadAt = -(1 << 20)  # last sample of a >= 40 ms leader run
        self.srun = 0             # sync run (samples)
        self.screen = [[0] * 128 for _ in range(64)]
        self.images = 0
        self.relocks = 0
        self.log = []
        self.m = None

    def scan_start(self):
        o = self.m["off"][self.sc]
        self.ys = self.c + o + cdiv(o * (self.per - self.m["per0"]), self.m["per0"])
        self.px = 0
        self.sp = self.sq = 0

    def restart(self):
        """The period from g.c (its sync end, measured or predicted)."""
        self.step = cdiv(self.m["stepNum"], self.per)
        self.sc = 0
        self.lbuf = [0] * 128
        self.scan_start()

    def end_line(self):
        if not self.found:
            self.errs.append(None)
            self.miss += 1
            if self.miss > MISS_MAX:
                self.log.append((self.c, "lost", self.line))
                self.st = self.H_HUNT
                self.lead = self.srun = 0
                return
        for x in range(128):
            self.acc[x] += self.lbuf[x]
        self.cnt += 1
        self.racc += self.m["rowA"]
        if self.racc >= self.m["rowB"]:
            self.racc -= self.m["rowB"]
            r = self.row
            for x in range(128):
                thr = THR[(r & 3) * 4 + (x & 3)]
                self.screen[r][x] = 1 if self.acc[x] < thr * self.cnt * 4 else 0
                self.acc[x] = 0
            self.cnt = 0
            self.row += 1
        self.line += 1
        if self.line >= self.m["periods"]:
            self.images += 1
            self.st = self.H_HUNT
            self.lead = self.srun = 0
            return
        self.pred = self.c + (self.per >> 4)
        self.c = self.pred
        self.prevFound = self.found
        self.found = False
        self.lineDone = True
        self.restart()

    def sample(self, n, adc):
        x = adc - 2048
        y = (BP_B0 * (x - self.x2) - BP_A1 * self.y1 - BP_A2 * self.y2) >> 14
        y1 = self.y1
        p = y1 * (y + self.y2)
        q = y1 * y1
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, y1, y
        self.P += (p - self.P) >> 3
        self.Q += (q - self.Q) >> 3
        P, Q = self.P, self.Q
        sync = 4 * P > 5 * Q

        st = self.st
        if st == self.H_HUNT:
            if 2 * P > Q and 4 * P < 3 * Q:
                self.lead += 1
                if self.lead >= LEAD_MIN:
                    self.leadAt = n
            else:
                self.lead = 0
            if not sync:
                self.srun = 0
            else:
                self.srun += 1
                # start bit: 20 ms below 1360 Hz, begun right after a leader
                if self.srun == VIS_RUN and n - VIS_RUN - self.leadAt < 48:
                    self.t0 = n - (VIS_RUN - 1)
                    self.bit = self.code = self.votes = 0
                    self.st = self.H_VIS
        elif st == self.H_VIS:
            # bit i: [t0 + 288 (i + 1), +288), votes in its middle 20 ms
            k = n - self.t0 - 288 * (self.bit + 1)
            if 48 <= k < 240 and 32 * P > 45 * Q:
                self.votes += 1
            if k == 240:
                if self.votes > 96:
                    self.code |= 1 << self.bit
                self.votes = 0
                self.bit += 1
                if self.bit == 8:
                    c = self.code
                    self.log.append((n, "vis", c))
                    self.lead = self.srun = 0
                    self.st = self.H_HUNT
                    for i, r in enumerate(RECORDS):
                        if r["vis"] == c:
                            self.mode, self.m = i, r
                            self.line = self.row = self.racc = self.cnt = 0
                            self.acc = [0] * 128
                            self.per = r["per0"]
                            self.miss = RELOCK    # the first sync: anywhere
                            self.pred = self.c = self.t0 + r["first"]
                            self.found = self.prevFound = False
                            self.errs = []
                            self.st = self.H_LINE
                            self.restart()
                            break
        else:
            # The sync end (1200 -> 1500 Hz) within +-10 ms of the prediction
            # (re)starts the period: the scans never read as a sync, and a late
            # sync restarts the line buffer before it reaches the picture.
            m = self.m
            if sync:
                self.srun += 1
            elif self.srun:
                ln = self.srun
                self.srun = 0
                # a sender with a fast clock: the next sync ends before this
                # period's last scan is over (or this period ran on the
                # prediction and missed its own sync); the period is cut there
                if ln >= m["syncMin"] and -SYNC_WIN <= n - (self.c + (self.per >> 4)) <= SYNC_WIN:
                    self.end_line()
                    self.lineDone = False
                    if self.st != self.H_LINE:
                        return
                    m = self.m
                err = n - self.pred
                if ln >= m["syncMin"] and not self.found:
                    inwin = -SYNC_WIN <= err <= SYNC_WIN
                    if inwin:
                        self.errs.append(err)
                        if self.prevFound:
                            per = self.per + err * 2
                            lim = m["per0"] >> 4
                            if per > m["per0"] + lim:
                                per = m["per0"] + lim
                            if per < m["per0"] - lim:
                                per = m["per0"] - lim
                            self.per = per
                        self.found = True
                        self.miss = 0
                    # lock lost (a sender whose first line is not where the VIS
                    # says, a fade): after RELOCK periods on the prediction a
                    # sync anywhere restarts the period. It does not count as
                    # found: only a sync in the window of the next prediction
                    # clears miss, so noise alone still ends as lost.
                    if inwin or self.miss >= RELOCK:
                        if not inwin:
                            self.relocks += 1
                        self.c = n
                        self.restart()
                        return
            d = n - self.ys
            if d >= 0:
                k = (d * self.step) >> 16
                if k != self.px:
                    self.lbuf[self.px] += m["w"][self.sc] * lum(self.sp, self.sq)
                    self.sp = self.sq = 0
                    self.px = k
                    if k >= 128:
                        self.sc += 1
                        if self.sc >= m["nscan"]:
                            self.end_line()
                        else:
                            self.scan_start()
                        return
                self.sp += p
                self.sq += q

    def run(self, samples):
        for n, a in enumerate(samples, 1):
            self.sample(n, a)
        return self

# ---------------------------------------------------------------- tests -----

def test_logo():
    """A stand-in boot logo: the house title-screen lettering."""
    from app_art import Canvas
    c = Canvas()
    c.two_words("ROBOT", "LAB")
    for x in range(128):                     # a frame and a gradient-free bar
        c.set(x, 0); c.set(x, 63)
    for y in range(64):
        c.set(0, y); c.set(127, y)
    out = bytearray()
    for page in range(8):
        for x in range(128):
            b = 0
            for bit in range(8):
                if c.px[page * 8 + bit][x]:
                    b |= 1 << bit
            out.append(b)
    return bytes(out)

def logo_px(logo, x, y):
    return logo[(y >> 3) * 128 + x] >> (y & 7) & 1

def compare(logo, rx):
    return sum(rx.screen[y][x] != logo_px(logo, x, y)
               for y in range(64) for x in range(128))

def pgm(path, rows, scale=3):
    with open(path, "wb") as f:
        f.write(b"P5 %d %d 255\n" % (128 * scale, len(rows) * scale))
        for r in rows:
            line = bytes((40 if v else 200) for v in r for _ in range(scale))
            f.write(line * scale)

def write_wav(path, events, fs=48000):
    a = synth(events, fs=fs, amp=0.6, lead_s=0.2, tail_s=0.2)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(fs)
        w.writeframes(b"".join(struct.pack("<h", int(v * 32767)) for v in a))

CASES = [("std", {}, {}), ("clean", {}, dict(deemph=False)),
         ("tx +1%", dict(ppm=10000), {}), ("tx -1.5%", dict(ppm=-15000), {}),
         ("dev 1.5k", {}, dict(dev=1500)), ("snr 20", {}, dict(snr_db=20)),
         ("snr 12", {}, dict(snr_db=12)), ("snr 6", {}, dict(snr_db=6)),
         ("late 25", dict(late_ms=25), {}), ("late 70", dict(late_ms=70), {}),
         ("late-1.5%", dict(late_ms=25, ppm=-15000), {}),
         ("late snr6", dict(late_ms=25), dict(snr_db=6))]

def run_case(logo, mode, case, outdir=None):
    """One mode through one channel case, rendered dithered and 1-bit."""
    global THR
    cname, sk, ck = next(c for c in CASES if c[0] == case)
    sk = dict(sk)
    ev = tx_events(logo, mode)
    late = sk.pop("late_ms", 0)     # black between the VIS and the first line
    if late:
        ev.insert(13, (1500, late * MS))
    adc = channel(synth(ev, **sk), **ck)
    bayer = list(THR)
    out = []
    for thr in (bayer, [128] * 16):
        THR = thr
        rx = Rx().run(adc)
        out.append(compare(logo, rx))
        if outdir:
            pgm(os.path.join(outdir, (M.MODES[mode][0] + "_" + cname).replace(" ", "_")
                             .replace("%", "") + ("_dither" if thr is bayer else "_1bit")
                             + ".pgm"), rx.screen)
    THR = bayer
    errs = [e for e in getattr(rx, "errs", []) if e is not None] or [0]
    print(f"{M.MODES[mode][0]:10s} {cname:9s} dither {out[0]:5d}  1-bit {out[1]:5d} px wrong"
          f"  sync err {min(errs)}..{max(errs)}  free-run {getattr(rx, 'errs', []).count(None)}"
          f"  period {getattr(rx, 'per', 0) / 16:.1f} ({rx.m['per0'] / 16 if rx.m else 0:.1f})"
          f"  relock {rx.relocks}  images {rx.images} {rx.log[-1:]}", flush=True)
    return out

def read_logo(path):
    """A boot logo dump: 1024 B, or the 1032 B flash record (8-byte header)."""
    data = open(path, "rb").read()
    return data[8:1032] if len(data) >= 1032 else data[:1024].ljust(1024, b"\0")

def main():
    args = sys.argv[1:]
    if args and args[0] == "wav":
        mode = mode_index(args[2]) if len(args) > 2 else 0
        logo = read_logo(args[3]) if len(args) > 3 else test_logo()
        write_wav(args[1], tx_events(logo, mode))
        print("wrote", args[1], M.MODES[mode][0])
        return
    outdir = args[0] if args and os.path.isdir(args[0]) else None
    names = [a for a in args if a != outdir] or None
    logo = test_logo()
    ev = tx_events(logo, 0)
    total = sum(c for _, c in ev)
    assert sum(c for _, c in ev[:13]) == (LEAD_IN + 910) * MS
    assert (total - (LEAD_IN + 910) * MS) == 240 * 150 * MS, total
    if names:                       # the given modes through every case
        for n in names:
            for c in CASES:
                run_case(logo, mode_index(n), c[0], outdir)
        return
    for i in range(len(M.MODES)):   # every mode, standard path
        run_case(logo, i, "std", outdir)

if __name__ == "__main__":
    main()
