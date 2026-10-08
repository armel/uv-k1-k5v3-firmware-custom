#!/usr/bin/env python3
"""Decode every t001frames.py frame through the full chain (gen406.py audio ->
host_dec406) and compare with the values the specification gives: BCH, 15-hex
ID, position and fine/coarse. Prints one line per frame, then the totals.

  t001check.py [--fast] [host_dec406 binary]   (built from ../dec406.c if omitted)
  --fast: parse the frame bits directly (host_dec406 -x), no audio chain
Exit status 1 when a frame does not match.
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from t001frames import catalogue, fmt_pos


def main():
    args = sys.argv[1:]
    fast = "--fast" in args
    args = [a for a in args if a != "--fast"]
    tmp = tempfile.mkdtemp()
    host = args[0] if args else os.path.join(tmp, "host_dec406")
    if not args:
        subprocess.check_call([os.environ.get("CC", "clang"), "-O2", "-o", host,
                               os.path.join(HERE, "host_dec406.c"), os.path.join(HERE, "..", "dec406.c")])
    bad = 0
    cat = catalogue()
    for name, desc, frame, hid, pos, fine in cat:
        if fast:
            out = subprocess.run([host, "-x", frame], capture_output=True, text=True).stdout
        else:
            u16 = os.path.join(tmp, "s.u16")
            subprocess.check_call([sys.executable, os.path.join(HERE, "gen406.py"), "--frame", frame, "--out", u16])
            out = subprocess.run([host, u16], capture_output=True, text=True).stdout
        got = {}
        for line in out.splitlines():
            if ":" in line:
                k, v = line.split(":", 1)
                got.setdefault(k.strip(), v.strip())
        want_pos = "none" if pos is None else fmt_pos(pos) + ("" if fine else " (coarse)")
        errs = []
        if got.get("BCH") != "ok / ok":
            errs.append("BCH %s" % got.get("BCH"))
        gid = got.get("15-hex ID", "").split(" ")[0]
        if gid != hid:
            errs.append("ID %s (want %s)" % (gid or "-", hid))
        if got.get("position") != want_pos:
            errs.append("position %s (want %s)" % (got.get("position", "-"), want_pos))
        bad += bool(errs)
        print("  %s %-20s %s" % ("✅" if not errs else "❌", name, "; ".join(errs)))
    print("  %d frames, %d match, %d differ" % (len(cat), len(cat) - bad, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
