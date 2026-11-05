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
#         decoded by the C demodulator: what the host sent comes back.
#
#   python3 test_tnc.py           (needs cc / clang)
import os, shutil, struct, subprocess, sys, tempfile, math
HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
APPS = os.path.dirname(APP)
sys.path.insert(0, os.path.join(APPS, 'aprsrx', 'test'))
sys.dont_write_bytecode = True
from ax25 import test_frames, crc16, build
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

def run_tnc(adc, host=b'', key2_call=None, failw=None):
    a = os.path.join(tmp, 'adc.bin'); hb = os.path.join(tmp, 'host.bin')
    open(a, 'wb').write(struct.pack('<%dH' % len(adc), *adc))
    open(hb, 'wb').write(host)
    args = [exe, os.path.join(tmp, 'kisstnc_assets.bin'), a, hb] + ([key2_call] if key2_call is not None else [])
    env = dict(os.environ, FAILW=str(failw)) if failw else None
    out = subprocess.run(args, env=env,
                         capture_output=True, text=True, check=True).stdout.splitlines()
    blob = b''.join(bytes(int(x, 16) for x in l.split()[1:]) for l in out if l.startswith('OUT'))
    frames = []
    for part in blob.split(bytes([FEND])):
        if part:
            assert part[0] == 0, 'KISS command byte'
            frames.append(part[1:].replace(b'\xdb\xdc', b'\xc0').replace(b'\xdb\xdd', b'\xdb'))
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
fr = test_frames()
for mode in ('std', 'raw'):
    for noise in (0, 1500, 3000):
        adc, want = [], []
        for seed, name in enumerate(('pos', 'digi', 'badfcs', 'msg', 'long', 'status')):
            a = channel(sine_wave(fr[name]), mode, noise, 0, 0, seed + 1)
            adc += a
            want += [g[:-2] for g in run(a)]          # Python reference, FCS stripped
        got, _, _ = run_tnc(adc)
        check('RX %s noise %4d Hz: %d/%d frames as the reference' % (mode, noise, len(got), len(want)),
              got == want, 'got %s' % [g[:12].hex() for g in got])
check('RX never passes the bad-FCS frame', all(g != fr['badfcs'][:-2] for g in got))

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

def tx_case(host_bytes, idle_s=2.0):
    adc = [2048] * int(9600 * idle_s)
    got, tones, tx = run_tnc(adc, host_bytes)
    starts = tx[0::2]
    t0 = starts[0] if starts else 0
    # the first write (mark, before the clock starts) is not a bit
    first = tones[0][0]
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

got, tones, tx, _ = tx_case(kiss(bytes([30]), cmd=1) + kiss(body))
t_start = tones[0][0]
changes = {(t - t_start) // CYC_PER_BIT - 1 for t, _ in tones[1:]}
nbits = (tx[1] - t_start) // CYC_PER_BIT
frames, lead = hdlc_decode([0 if k in changes else 1 for k in range(nbits)])
check('TX KISS TXDELAY 30 (300 ms) -> 45 flags', lead == 45, 'lead %d' % lead)

# ---- key 2: test frame to the host ---------------------------------------------
def addr7(call, last):
    return bytes(ord(c) << 1 for c in call.ljust(6)[:6]) + bytes([0x60 | last])
for call, shown in (('N0CAL', 'N0CAL'), ('', 'NOCALL'), ('N0CALLX', 'N0CALL')):
    got, _, _ = run_tnc([2048] * 9600, key2_call=call)
    want = addr7('APZK1', 0) + addr7(shown, 1) + b'\x03\xf0>UV-K1 KISS TNC test'
    check('key 2 test frame, callsign %-8r -> %s' % (call, shown), got == [want],
          'got %s' % [g.hex() for g in got])

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

print('ALL PASS' if ok else 'SOME FAILED')
sys.exit(0 if ok else 1)
