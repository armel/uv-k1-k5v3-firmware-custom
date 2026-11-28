#!/usr/bin/env python3
# Host test of the KISS TNC: kisstnc_app.c is built for the computer
# (host_tnc.c mocks the API, host_hw.h the MCU registers) and checked in both
# directions with APRS RX's channel model (aprsrx/test/model_rx.py):
#
#   RX    synthetic APRS audio (sine AFSK, radio audio path, noise) -> the C
#         demodulator -> KISS to the host: every frame the Python reference
#         decoder finds, FCS stripped, exactly once; the bad-FCS frame never.
#   TX    KISS data frames from the host -> the C modulator: the bit stream is
#         rebuilt from the tone writes and its timing; flags, bit stuffing,
#         NRZI, FCS and the frame are checked, and KISS TXDELAY is honoured.
#   LOOP  the C modulator's tones, turned into audio through the channel model,
#         decoded by the C demodulator: what the host sent comes back, a burst
#         of frames on shared flags too.
#   KISS  the queue (one key-up for all, ACKMODE ids back, a full queue), port
#         filter, SetHardware TNC: / TXBUF:, carrier sense.
#
#   python3 test_tnc.py           (needs cc / clang)
import os, shutil, struct, subprocess, sys, tempfile, math
HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
APPS = os.path.dirname(APP)
sys.path.insert(0, os.path.join(APPS, 'aprsrx', 'test'))
sys.dont_write_bytecode = True
from ax25 import test_frames, crc16, build, addr
from model_rx import channel, sine_wave, run, FS_SIM

FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD
CYC_PER_BIT = 40000

# ---- build -------------------------------------------------------------------
tmp = tempfile.mkdtemp()
api_h = os.path.join(APPS, 'app_api.h')
subprocess.run([sys.executable, os.path.join(APP, 'gen_assets.py'),
                os.path.join(tmp, 'kisstnc_assets.bin'), os.path.join(tmp, 'kisstnc_assets.h')],
               check=True, cwd=APP)
src = open(os.path.join(APP, 'kisstnc_app.c')).read()
src = src.replace('__attribute__((section(".text.entry"),used))', '')
src = src.replace('#include "../app_api.h"', '#include "%s"' % api_h)
open(os.path.join(tmp, 'app.c'), 'w').write(src)
shutil.copy(os.path.join(HERE, 'host_hw.h'), tmp)
h = open(os.path.join(HERE, 'host_tnc.c')).read().replace('"../../app_api.h"', '"%s"' % api_h)
open(os.path.join(tmp, 'host_tnc.c'), 'w').write(h)
cc = shutil.which('clang') or shutil.which('gcc')
exe = os.path.join(tmp, 'tnc')
subprocess.run([cc, '-O2', '-Wall', '-Wno-unused-function', '-I', tmp, '-o', exe,
                os.path.join(tmp, 'host_tnc.c')], check=True)

other = []                                   # the last run's non-data KISS frames: (command, bytes)
screen = {}                                  # the last run's screen at exit: line -> text
gains = set()                                # the last run's tone gains (REG_70)
r43 = set()                                  # the last run's REG_43 writes
r2b = []                                     # the last run's REG_2B writes, in order
txcalls = []                                 # the last run's key-up API calls
leds = []                                    # the last run's green LED changes
status = []                                  # the last run's status-line bytes 72-80, each draw
lapped = []                                  # the last run: did the 256-byte USB ring lap?
def run_tnc(adc, host=b'', failw=None, rssi=None, sql=None, late=None, wide=None, lock=None, ring=None, reply=None):
    a = os.path.join(tmp, 'adc.bin'); hb = os.path.join(tmp, 'host.bin')
    open(a, 'wb').write(struct.pack('<%dH' % len(adc), *adc))
    open(hb, 'wb').write(host)
    args = [exe, os.path.join(tmp, 'kisstnc_assets.bin'), a, hb]
    env = dict(os.environ)
    if failw: env['FAILW'] = str(failw)
    if rssi is not None: env['RSSI'] = str(rssi)
    if sql is not None: env['SQL'] = str(sql)
    if late is not None: env['LATE'] = '%d,%g' % late
    if wide is not None: env['WIDE'] = str(wide)
    if lock is not None: env['LOCK'] = str(lock)
    if ring is not None: env['RING'] = str(ring)
    if reply is not None: env['REPLY'] = str(reply)
    out = subprocess.run(args, env=env,
                         capture_output=True, text=True, check=True).stdout.splitlines()
    blob = b''.join(bytes(int(x, 16) for x in l.split()[1:]) for l in out if l.startswith('OUT'))
    frames = []
    other.clear(); screen.clear(); gains.clear(); status.clear(); lapped[:] = [any(l.startswith('LAPPED') for l in out)]
    status.extend(bytes(int(x, 16) for x in l.split()[1:]) for l in out if l.startswith('STATUS'))
    gains.update(int(l.split()[1]) for l in out if l.startswith('GAIN'))
    r43.clear(); r43.update(int(l.split()[1], 16) for l in out if l.startswith('R43'))
    r2b[:] = [int(l.split()[1], 16) for l in out if l.startswith('R2B')]
    txcalls[:] = [l.split()[1] for l in out if l.startswith('TXCALL')]
    leds[:] = [int(l.split()[1]) for l in out if l.startswith('LED')]
    rows = {}                                # the last screen: each line's texts by column
    for l in out:
        if l.startswith('LINE '):
            n, x, text = l[5:].split(' ', 2)
            n, col = int(n), int(x) // 7
            if n == 0 and col == 0 and rows: rows = {}   # line 0 again: a new screen
            r = rows.setdefault(n, [' '] * 24)
            r[col:col + len(text)] = list(text)
    screen.update({n: ''.join(r).rstrip() for n, r in rows.items()})
    for part in blob.split(bytes([FEND])):
        if part:
            body = part[1:].replace(b'\xdb\xdc', b'\xc0').replace(b'\xdb\xdd', b'\xdb')
            (frames if part[0] == 0 else other).append(body if part[0] == 0 else (part[0], body))
    tones = [(int(l.split()[1]), int(l.split()[2])) for l in out if l.startswith('TONE')]
    tx = [(int(l.split()[1])) for l in out if l.startswith(('TXSTART', 'TXEND'))]
    return frames, tones, tx

def kiss(f, cmd=0):
    return bytes([FEND, cmd]) + f.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc') + bytes([FEND])

ok = True
def check(name, cond, detail=''):
    global ok
    ok &= bool(cond)
    print(('PASS ' if cond else 'FAIL ') + name + (('  ' + detail) if detail and not cond else ''))

# ---- RX ------------------------------------------------------------------------
SLICERS = ((5, 6), (1, 1), (6, 5))             # the app's decision weights (APRS RX: 2:3, 1:1, 3:2)
fr = test_frames()
for mode in ('std', 'raw'):
    for noise in (0, 1500, 3000):
        adc, want = [], []
        for seed, name in enumerate(('pos', 'digi', 'badfcs', 'msg', 'long', 'status')):
            a = channel(sine_wave(fr[name]), mode, noise, 0, 0, seed + 1)
            adc += a
        want = [g[:-2] for g in run(adc, slicers=SLICERS)]   # Python reference on the same stream, FCS stripped
        got, _, _ = run_tnc(adc)
        check('RX %s noise %4d Hz: %d/%d frames as the reference' % (mode, noise, len(got), len(want)),
              got == want, 'got %s' % [g[:12].hex() for g in got])
check('RX never passes the bad-FCS frame', all(g != fr['badfcs'][:-2] for g in got))

# ---- RX of U/S frames (no PID) -------------------------------------------------
# AX.25 connected mode: U and S frames carry no PID, so 2 addresses + control +
# FCS = 17 bytes, one short of a UI frame. A TNC (unlike the APRS-only reference)
# must pass them, or connected mode can't work in either direction (issue #840).
def us_frame(control):
    f = addr('APZK1') + addr('N0CALL-7', last=True) + bytes([control])
    fcs = crc16(f)
    return f + bytes([fcs & 0xFF, fcs >> 8])
for name, control in (('SABM', 0x3F), ('UA', 0x73), ('RR', 0x01), ('DISC', 0x53)):
    f = us_frame(control)
    adc = channel(sine_wave(f), 'std', 0, 0, 0, 11)
    got, _, _ = run_tnc(adc)
    check('RX passes the 17-byte %s frame (no PID)' % name, got == [f[:-2]],
          'len %d, got %s' % (len(f), [g.hex() for g in got]))

# ---- USB write timeout ----------------------------------------------------------
# The host does not take the 2nd write (inside the 200-byte frame): that frame
# stops there, the next one arrives whole, the parser resyncs on its FEND.
adc = []
for seed, name in enumerate(('long', 'pos')):
    adc += channel(sine_wave(fr[name]), 'std', 0, 0, 0, seed + 1)
got, _, _ = run_tnc(adc, failw=2)
check('USB timeout: the cut frame is short, the next one whole',
      len(got) == 2 and got[1] == fr['pos'][:-2] and fr['long'][:-2].startswith(got[0]) and got[0] != fr['long'][:-2],
      'got %s' % [g[:12].hex() for g in got])

# ---- TX ------------------------------------------------------------------------
def bits_from_tones(tones, t0):
    """Each bit plays from a bit edge: a tone write at edge k means bit k is 0
    (NRZI: a change), no write means 1."""
    changes = set()
    for t, _ in tones:
        changes.add((t - t0) // CYC_PER_BIT)
    return changes

def hdlc_decode(bits):
    """Flags, destuffing, frames with their FCS checked."""
    frames, cur, ones, sr, inframe, nflags_lead = [], [], 0, 0, False, 0
    byte, nb = 0, 0
    for b in bits:
        sr = ((sr >> 1) | (b << 7)) & 0xFF
        if sr == 0x7E:
            if inframe and len(cur) >= 3:
                frames.append(bytes(cur))
            if not frames and not cur:
                nflags_lead += 1
            inframe, cur, ones, byte, nb = True, [], 0, 0, 0
            continue
        if b:
            ones += 1
            if ones > 6:
                inframe = False
                continue
        else:
            if ones == 5:
                ones = 0
                continue
            ones = 0
        if not inframe:
            continue
        byte |= b << nb
        nb += 1
        if nb == 8:
            cur.append(byte); byte, nb = 0, 0
    return frames, nflags_lead

def tx_case(host_bytes, idle_s=2.0, sql=None):
    adc = [2048] * int(9600 * idle_s)
    got, tones, tx = run_tnc(adc, host_bytes, sql=sql)
    starts = tx[0::2]
    t0 = starts[0] if starts else 0
    # the first write (mark, before the clock starts) is not a bit
    first = tones[0][0] if tones else 0
    return got, tones, tx, first

aprs = build('N0CALL-7', dst='APDR16', path=['WIDE1-1'], info='=4317.75N/00522.18E>APRSdroid \xc0\xdb test')
body = aprs[:-2]
got, tones, tx, _ = tx_case(kiss(body))
check('TX keyed once, released once', len(tx) == 2)
# rebuild the bit stream: bit k plays from clkStart + (k+1) * 40000 cycles; the
# clock starts right after the first tone write
t_start = tones[0][0]
changes = set()
for t, _ in tones[1:]:
    changes.add((t - t_start) // CYC_PER_BIT - 1)
nbits = (tx[1] - t_start) // CYC_PER_BIT
bits = [0 if k in changes else 1 for k in range(nbits)]
frames, lead = hdlc_decode(bits)
check('TX sends 40 flags before the frame', lead == 40, 'lead %d' % lead)
check('TX frame on air == host frame + FCS', frames and frames[0] == aprs,
      'got %s' % (frames[0].hex() if frames else None))
check('TX tones are 1200 / 2200 Hz only', {v for _, v in tones} <= {12389, 22714})
check('TX keys up with tx_prepare, without tx_set_params or tx_tone (their 25 + 50 ms of waits)',
      [c for c in txcalls if not c.startswith('af')][-1:] == ['prepare'] and 'set_params' not in txcalls and 'tone' not in txcalls,
      'calls %s' % txcalls)
check('TX mutes AF for the key-up and sets AF FM after tx_end (its squelch setup mutes AF: RX deaf otherwise)',
      txcalls[txcalls.index('prepare') + 1:][:1] == ['af0'] and 'af1' in txcalls[txcalls.index('prepare') + 2:], 'calls %s' % txcalls)

check('TX tone gains on a narrow VFO: 47 at 1200 Hz, 80 at 2200 Hz', gains == {47, 80}, 'gains %s' % gains)
check('RX on a narrow VFO: audio 6 dB up (REG_43 <2>), no 300 Hz high-pass (REG_2B <10>)',
      r43 == {0x4048 | 4} and set(r2b) == {0x0400}, 'REG_43 writes %s, REG_2B writes %s' % ([hex(v) for v in r43], sorted({hex(v) for v in r2b})))
got, tones, tx = run_tnc([2048] * 9600 * 2, kiss(body), wide=1)
check('TX tone gains on a wide VFO (REG_43 <5:4> 2): 35 and 68', len(tx) == 2 and gains == {35, 68}, 'gains %s' % gains)
check('RX on a wide VFO: the demodulated audio 6 dB up (REG_43 <2> set)', r43 == {0x3028 | 4}, 'REG_43 writes %s' % [hex(v) for v in r43])
got, tones, tx = run_tnc([2048] * 9600 * 2, kiss(body), wide=2)
check('TX on a narrower VFO (6.25 kHz): nothing sent, the frame counted as lost',
      tx == [] and screen.get(1, '').split()[2:] == ['LOS', '1'], 'key-ups %d, line 1 %r' % (len(tx) // 2, screen.get(1)))

got, tones, tx, _ = tx_case(kiss(bytes([30]), cmd=1) + kiss(body))
t_start = tones[0][0]
changes = {(t - t_start) // CYC_PER_BIT - 1 for t, _ in tones[1:]}
nbits = (tx[1] - t_start) // CYC_PER_BIT
frames, lead = hdlc_decode([0 if k in changes else 1 for k in range(nbits)])
check('TX KISS TXDELAY 30 (300 ms) -> 45 flags', lead == 45, 'lead %d' % lead)

def on_air(tones, tx):
    """The frames of the first key-up, and its leading flags."""
    t_start = tones[0][0]
    changes = {(t - t_start) // CYC_PER_BIT - 1 for t, _ in tones[1:] if t < tx[1]}
    nbits = (tx[1] - t_start) // CYC_PER_BIT
    return hdlc_decode([0 if k in changes else 1 for k in range(nbits)])

# ---- KISS: queue, ACKMODE, ports, SetHardware, carrier --------------------------
burst = [build('N0CALL-%d' % k, dst='APZK1', info='>burst %d' % k) for k in range(1, 5)]
got, tones, tx, _ = tx_case(b''.join(kiss(f[:-2]) for f in burst))
frames, lead = on_air(tones, tx)
check('KISS 4 frames: one key-up, all of them in order', len(tx) == 2 and frames == burst,
      'key-ups %d, frames %d' % (len(tx) // 2, len(frames)))
window = [build('N0CALL-%d' % k, dst='APZK1', info='>' + 'w%d' % k * 99) for k in range(1, 5)]
got, tones, tx = run_tnc([2048] * 9600 * 4, b''.join(kiss(f[:-2]) for f in window), ring=1)
frames, _ = on_air(tones, tx) if tx else ([], 0)
check('KISS a window of 4 frames (850 B) at USB speed into the 256-byte ring: no lap, all 4 sent',
      not lapped[0] and frames == window, 'lapped %s, frames %d' % (lapped[0], len(frames)))
two = [kiss(f[:-2]) for f in window[:2]]
got, tones, tx = run_tnc([2048] * 9600 * 3, two[0] + two[1], ring=1, late=(len(two[1]), 0.11))
frames, _ = on_air(tones, tx) if tx else ([], 0)
check('KISS a window in two writes 10 ms apart: no key-up in between, both frames in one, whole',
      not lapped[0] and len(tx) == 2 and frames == window[:2], 'lapped %s, key-ups %d, frames %d' % (lapped[0], len(tx) // 2, len(frames)))
answer = b''.join(kiss(f[:-2]) for f in window)
got, tones, tx = run_tnc(channel(sine_wave(fr['pos']), 'std', 0, 0, 0, 1) + [2048] * 9600 * 3, answer, ring=1, reply=len(answer))
frames, _ = on_air(tones, tx) if tx else ([], 0)
check('KISS a host answering a received frame 55 ms later with a window: no lap, all 4 sent',
      got == [fr['pos'][:-2]] and not lapped[0] and frames == window, 'lapped %s, frames %d' % (lapped[0], len(frames)))
got, tones, tx, _ = tx_case(kiss(b'\x12\x34' + body, cmd=0x0C))
frames, _ = on_air(tones, tx)
check('KISS ACKMODE: the frame on air, its id back after', frames == [aprs] and other == [(0x0C, b'\x12\x34')],
      'other %s' % other)
got, tones, tx, _ = tx_case(kiss(body, cmd=0x10))
check('KISS a frame for port 1 is not sent', tx == [])
got, _, _ = run_tnc([2048] * 9600, kiss(b'T', cmd=6) + kiss(b'TNC:', cmd=6) + kiss(b'TXBUF:', cmd=6))
check('KISS SetHardware TNC: and TXBUF: answered, a lone T not', other == [(6, b'TNC:KISS TNC 2.0'), (6, b'TXBUF:0')],
      'other %s' % other)
got, tones, tx, _ = tx_case(kiss(body), sql=1)
check('carrier (the squelch open): nothing sent', tx == [])
got, tones, tx, _ = tx_case(kiss(body), idle_s=13.0, sql=1)
check('a carrier that never ends (SQL 1 and up) holds the queue', tx == [])
second = kiss(build('N0CALL-7', dst='APZK1', info='>second')[:-2])
got, tones, tx = run_tnc([2048] * int(9600 * 4), kiss(body) + second, sql=2, late=(len(second), 2.0))
check('SQL 0 (the squelch always open): no carrier, both frames go out at once',
      len(tx) == 4 and tx[0] < 48e6 * 1 and tx[2] < 48e6 * 3, 'key-ups at %s' % [round(t / 48e6, 1) for t in tx[0::2]])
big = build('N0CALL-7', dst='APZK1', info='>' + 'x' * 300)[:-2]
got, tones, tx, _ = tx_case(b''.join(kiss(big) for _ in range(4)) + kiss(b'TXBUF:', cmd=6), sql=1)
check('KISS queue full: 3 of 4 long frames wait, the 4th is dropped',
      tx == [] and other == [(6, b'TXBUF:%d' % (3 * len(big)))], 'other %s' % other)

# ---- screen: slicer count, BAD ------------------------------------------------
got, _, _ = run_tnc(channel(sine_wave(fr['pos']), 'std', 0, 0, 0, 1))
check('screen: the last station\'s RSSI and how many slicers decoded it, below them its call',
      screen.get(2, '') == 'dBm -120 DEC 3/3' and screen.get(3, '') == 'FRM F4HWN',
      'lines 2-3 %r %r' % (screen.get(2), screen.get(3)))
bad = channel(sine_wave(fr['pos']), 'std', 0, 0, 0, 1)
mid = int((0.15 + 0.1 + 0.267 + len(fr['pos']) * 8 / 2400) * 9600)          # gap, lead, preamble, half the frame
bad[mid:mid + 300] = [2048] * 300   # the frame's middle lost: DCD, a frame begun, none decoded
got, _, _ = run_tnc(bad, sql=1)
check('screen: a packet with a carrier that does not decode counts as BAD', got == [] and screen.get(1, '').split()[:2] == ['BAD', '1'],
      'line 1 %r' % screen.get(1))
from ax25 import TXDELAY_FLAGS
pre = sine_wave(fr['pos'])
pre = pre[:int(0.1 * FS_SIM) + TXDELAY_FLAGS * 8 * FS_SIM // 1200]   # the lead-in flags, then nothing
got, _, _ = run_tnc(channel(pre, 'std', 0, 0, 0, 1) + [2048] * 9600, sql=1)
check('screen: a preamble alone with a carrier (flags, no frame) is not BAD', got == [] and screen.get(1, '').split()[:2] == ['BAD', '0'],
      'line 1 %r' % screen.get(1))
got, _, _ = run_tnc(channel(pre, 'std', 0, 0, 0, 1) + [2048] * 9600, sql=2)
check('green LED: off for a preamble alone at SQL 0 (no carrier), which noise mimics', 1 not in leds, 'LED changes %s' % leds)
import random
_rnd = random.Random(7)
noise = [int(min(4095, max(0, 2048 + _rnd.gauss(0, 150)))) for _ in range(9600 * 20)]   # 20 s, no carrier
got, _, _ = run_tnc(noise, sql=2)
check('green LED: off in 20 s of noise without a carrier (random flags and call bytes)', got == [] and 1 not in leds,
      'frames %d, LED changes %s' % (len(got), leds[:8]))
got, _, _ = run_tnc(channel(sine_wave(fr['pos']), 'std', 0, 0, 0, 1) + [2048] * 9600, sql=2)
check('green LED: on for a frame coming in, off again after it', got == [fr['pos'][:-2]] and leds[-2:] == [1, 0], 'LED changes %s' % leds)
got, _, _ = run_tnc([2048] * 9600 * 3, sql=1)
check('screen: a carrier without a packet is not BAD', screen.get(1, '').split()[:2] == ['BAD', '0'], 'line 1 %r' % screen.get(1))
LOCKBMP = bytes([0x7c, 0x46, 0x45, 0x45, 0x45, 0x45, 0x45, 0x46, 0x7c])
got, _, _ = run_tnc([2048] * 9600 * 3, lock=1)
check('# held 400 ms locks the keys: the lock in the status bar, gone again after # held once more',
      LOCKBMP in status and status[-1] != LOCKBMP, 'status %s' % [b.hex() for b in status[:4]])
got, _, _ = run_tnc([2048] * 9600 * 2, lock=2)
check('# tapped (0.2 s) does not lock', LOCKBMP not in status, 'status %s' % [b.hex() for b in status[:4]])
check('screen: every count in its place from the start, 0 until it counts, on a grid; no station line before one is heard',
      screen.get(0) == 'RX  0    TX  0' and screen.get(1) == 'BAD 0    LOS 0' and 2 not in screen and 3 not in screen,
      'screen %r' % screen)
longcall = build('AB1CDE-15', dst='APZK1', info='>long call')
got, _, _ = run_tnc(channel(sine_wave(longcall), 'std', 0, 0, 0, 3))
check('screen: a 9-character call (the AX.25 maximum) after FRM, no line longer than 18 characters',
      got == [longcall[:-2]] and screen.get(3, '') == 'FRM AB1CDE-15' and screen.get(2, '').endswith('/3') and
      all(len(v) <= 18 for v in screen.values()), 'screen %r' % screen)

# ---- LOOP ----------------------------------------------------------------------
def tones_to_audio(tones, tend, dev=3000.0):
    """Discriminator output (Hz) of the BK4829 tone generator: a phase-continuous
    sine whose frequency follows the tone writes."""
    ev = [(t, 1200.0 if v == 12389 else 2200.0) for t, v in tones]
    out, ph, k, f = [], 0.0, 0, ev[0][1]
    t = ev[0][0]
    step = 48e6 / FS_SIM
    while t < tend:
        while k < len(ev) and ev[k][0] <= t:
            f = ev[k][1]; k += 1
        ph += f / FS_SIM
        out.append(dev * math.sin(2 * math.pi * ph))
        t += step
    return [0.0] * int(0.1 * FS_SIM) + out

got, tones, tx, _ = tx_case(kiss(body))
for mode, noise in (('std', 0), ('std', 1500), ('raw', 1500)):
    adc = channel(tones_to_audio(tones, tx[1]), mode, noise, 0, 0, 7)
    back, _, _ = run_tnc(adc)
    check('LOOP %s noise %d: the host frame comes back' % (mode, noise), back == [body],
          'got %d frames' % len(back))
got, tones, tx, _ = tx_case(b''.join(kiss(f[:-2]) for f in burst))
back, _, _ = run_tnc(channel(tones_to_audio(tones, tx[1]), 'std', 0, 0, 0, 7))
check('LOOP a burst of 4 frames on shared flags: all 4 come back', back == [f[:-2] for f in burst],
      'got %d frames' % len(back))

print('ALL PASS' if ok else 'SOME FAILED')
sys.exit(0 if ok else 1)
