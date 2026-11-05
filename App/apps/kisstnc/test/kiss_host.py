#!/usr/bin/env python3
# Stand-in for APRSdroid on a computer: opens the radio's USB serial port,
# prints every frame the KISS TNC app sends (frames heard on the air, and the
# test frame of key 2), decoded, and optionally sends KISS data frames as
# APRSdroid does. No dependency (termios), macOS and Linux.
#
#   python3 kiss_host.py                     # listen only, first /dev/cu.usbmodem* or /dev/ttyACM*
#   python3 kiss_host.py --listen 600        # listen 10 minutes
#   python3 kiss_host.py --call F4XXX-7 --count 1 --info '>test'
#
# --count > 0 TRANSMITS on the radio's VFO: a licensed callsign, a free APRS
# frequency (144.800 MHz) or a dummy load.
import argparse, glob, os, select, sys, termios, time

FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD

def addr(call, last):
    name, _, ssid = call.partition('-')
    c = name.upper().ljust(6)[:6].encode()
    return bytes(x << 1 for x in c) + bytes([0x60 | (int(ssid or 0) << 1) | last])

def frame(src, info):
    """AX.25 UI frame as APRSdroid builds it (javAPRSlib toAX25Frame), no FCS."""
    return addr('APDR16', 0) + addr(src, 0) + addr('WIDE1-1', 1) + b'\x03\xf0' + info

def kiss(f):
    e = f.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc')
    return bytes([FEND, 0]) + e + bytes([FEND])

def decode(f):
    calls, i = [], 0
    while i + 7 <= len(f):
        c = bytes(x >> 1 for x in f[i:i + 6]).decode('ascii', 'replace').strip()
        ssid = (f[i + 6] >> 1) & 15
        calls.append(c + (f'-{ssid}' if ssid else ''))
        i += 7
        if f[i - 1] & 1:
            break
    if len(calls) < 2:
        return f'?? {f.hex()}'
    info = f[i + 2:].decode('ascii', 'replace')
    path = ',' + ','.join(calls[2:]) if calls[2:] else ''
    return f'{calls[1]}>{calls[0]}{path}:{info}'

def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)   # opening raises DTR
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0                                        # raw: no iflag/oflag/lflag
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

def reopen(path, timeout=10.0):
    """The radio left the USB bus (RF on the cable at key-up re-enumerates it):
    wait for its port to come back and reopen it."""
    print('USB link lost (the radio left the bus, usually RF at key-up): waiting for it')
    end = time.time() + timeout
    while time.time() < end:
        time.sleep(0.2)
        if os.path.exists(path):
            try:
                fd = open_port(path)
                print(f'reopened {path}')
                return fd
            except OSError:
                pass
    sys.exit(f'{path} did not come back within {timeout:.0f} s')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port', nargs='?')
    ap.add_argument('--call', default='N0CALL')
    ap.add_argument('--count', type=int, default=0, help='frames to send (ON AIR), 1 s apart; 0 = listen only')
    ap.add_argument('--info', default='>kiss_host.py test', help='APRS info field of the frames sent')
    ap.add_argument('--listen', type=float, default=60, help='seconds to listen after the last frame')
    a = ap.parse_args()
    if a.count and a.call.upper().startswith('N0CALL'):
        sys.exit('--count transmits on the air: pass your callsign with --call')
    port = a.port or (sorted(glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*')) or [None])[0]
    if not port:
        sys.exit('no USB serial port found: plug the radio, then pass the port')
    fd = open_port(port)
    print(f'opened {port}')
    buf, inframe, esc = bytearray(), False, False
    sent, next_tx = 0, time.time() + 0.5
    deadline = None if a.count else time.time() + a.listen
    while deadline is None or time.time() < deadline:
        if sent < a.count and time.time() >= next_tx:
            sent += 1
            f = frame(a.call, a.info.encode('latin-1'))
            try:
                os.write(fd, kiss(f))
            except OSError:
                os.close(fd); fd = reopen(port); os.write(fd, kiss(f))
            print(f'TX  {decode(f)}')
            next_tx = time.time() + 1
            if sent == a.count:
                deadline = time.time() + a.listen
        try:
            if not select.select([fd], [], [], 0.05)[0]:
                continue
            data = os.read(fd, 512)
        except OSError:
            os.close(fd); fd = reopen(port); inframe = False
            continue
        for b in data:
            if b == FEND:
                if inframe and len(buf) > 1 and buf[0] & 0x0F == 0:
                    print(f'RX  {decode(bytes(buf[1:]))}')
                buf.clear(); inframe, esc = True, False
            elif not inframe:
                continue
            elif esc:
                buf.append(FEND if b == TFEND else FESC if b == TFESC else b); esc = False
            elif b == FESC:
                esc = True
            else:
                buf.append(b)
    os.close(fd)

if __name__ == '__main__':
    main()
