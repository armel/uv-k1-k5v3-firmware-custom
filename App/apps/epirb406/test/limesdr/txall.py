#!/usr/bin/env python3
"""Transmit every EPIRB 406 test frame of a folder, one burst each, with a
pause of no carrier after each burst. The radio is opened and calibrated once.

Before each burst, prints what the EPIRB 406 app should show: the 15-hex ID
and position from the README.md index written by t001lime.py (or the screen
rows of an expected.json, when one is present).

  txall.py [dir] [--freq 433.65e6] [--delay 3] [--only 'sl_*'] [--loop]

Default dir: t001/psk (run t001lime.py first). Ctrl-C stops cleanly.
"""
import argparse
import fnmatch
import glob
import json
import os
import re
import sys
import time

from limetx import LimeTx, check_freq, load_iq

HERE = os.path.dirname(os.path.abspath(__file__))


def expected_screens(folder):
    """{name: [screen rows]} from an optional expected.json, here or one level up."""
    for path in (os.path.join(folder, "expected.json"), os.path.join(folder, "..", "expected.json")):
        if os.path.exists(path):
            with open(path) as fh:
                return {k: v.get("screen") for k, v in json.load(fh).items() if v.get("screen")}
    return {}


def expected_values(folder):
    """{name: (description, 15-hex ID, position)} from the README table one level up or here."""
    out = {}
    for readme in (os.path.join(folder, "README.md"), os.path.join(folder, "..", "README.md")):
        if os.path.exists(readme):
            for line in open(readme):
                m = re.match(r"\|\s*`([^`]+)`\s*\|(.*)\|\s*`([0-9A-F]{15})`\s*\|(.*)\|", line)
                if m:
                    out[m.group(1)] = (m.group(2).strip(), m.group(3), m.group(4).strip())
            break
    return out


def show(i, total, elapsed, name, expect, screens):
    """Print the frame about to go out and what the app should then show."""
    desc, hid, pos = expect.get(name, ("", "?", "?"))
    print(f"[{i:2}/{total}] {elapsed:6.1f}s  {name}" + (f"  ({desc})" if desc else ""))
    if name in screens:
        rows = screens[name]
        w = max(len(r) for r in rows)
        print("          expected on the K5:")
        print("          +" + "-" * (w + 2) + "+")
        for r in rows:
            print(f"          | {r:<{w}} |")
        print("          +" + "-" * (w + 2) + "+", flush=True)
    else:
        print(f"          expected: ID {hid}  pos {pos}", flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("dir", nargs="?", default=os.path.join(HERE, "t001", "psk"))
    p.add_argument("--freq", type=float, default=433.65e6, help="RF frequency in Hz (default 433.65e6)")
    p.add_argument("--rate", type=float, default=1e6, help="file sample rate (default 1e6)")
    p.add_argument("--format", default="cs16", help="file format (default cs16)")
    p.add_argument("--gain", type=float, default=30, help="TX gain in dB (default 30)")
    p.add_argument("--delay", type=float, default=3.0, help="seconds of no carrier after each burst (default 3)")
    p.add_argument("--only", default="*", help="glob on the frame name, e.g. 'sl_*' or 'eltdt_*'")
    p.add_argument("--loop", action="store_true", help="start over at the end until Ctrl-C")
    p.add_argument("--dry-run", action="store_true", help="print the sequence, do not open the radio")
    a = p.parse_args()

    check_freq(a.freq)
    files = [f for f in sorted(glob.glob(os.path.join(a.dir, f"*.{a.format}")))
             if fnmatch.fnmatch(os.path.splitext(os.path.basename(f))[0], a.only)]
    if not files:
        sys.exit(f"no *.{a.format} files matching '{a.only}' in {a.dir}")
    expect = expected_values(a.dir)
    screens = expected_screens(a.dir)
    bursts = [(os.path.splitext(os.path.basename(f))[0], load_iq(f, a.format)) for f in files]
    total = sum(len(iq) / a.rate + a.delay for _, iq in bursts)

    if a.dry_run:
        for i, (name, _) in enumerate(bursts, 1):
            show(i, len(bursts), 0.0, name, expect, screens)
        return
    tx = LimeTx(a.freq, a.rate, a.gain)
    print(tx.describe())
    print(f"frames   : {len(bursts)} from {a.dir}, {a.delay:g} s after each, ~{total / 60:.1f} min per pass\n")
    t_start = time.time()
    try:
        n = 0
        while True:
            for i, (name, iq) in enumerate(bursts, 1):
                # writes run ahead of the air by the driver buffer only, so wait for
                # this burst's air time before printing the next line
                lag = tx.n / a.rate - (time.time() - tx.t0)
                if lag > 0:
                    time.sleep(lag)
                show(i, len(bursts), time.time() - t_start, name, expect, screens)
                tx.play(iq)
                tx.silence(a.delay)
                n += 1
            if not a.loop:
                break
        tx.wait()
        print(f"\ndone: {n} bursts")
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        tx.close()


if __name__ == "__main__":
    main()
