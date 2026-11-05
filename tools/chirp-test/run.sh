#!/usr/bin/env bash
# Run CHIRP's own driver test suite against our module.
#
# Catches the class of bug an ad-hoc harness cannot: driver registration,
# detection wiring, memory and settings round-trips through CHIRP's real code.
# A stub fake-serial harness passed happily while every download failed with
# "Internal driver error" - this would not have.
#
# Usage:  tools/chirp-test/run.sh [path/to/chirp/checkout] [extra pytest args]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
CHIRP="${1:-$HOME/code/chirp}"
[[ $# -gt 0 ]] && shift || true

export NR7Y_MODULE="${NR7Y_MODULE:-$REPO/chirp/brass.k1-k5v3.chirp.py}"

if [[ ! -f "$CHIRP/tests/test_drivers.py" ]]; then
  echo "No CHIRP checkout at $CHIRP (pass the path as the first argument)" >&2
  exit 1
fi

IDENT='Quansheng_UV-K1_&_UV-K5_V3_F4HWN_Fusion_Brass'
IMG="$CHIRP/tests/images/$IDENT.img"
# The suite hardcodes tests/images/ as the search path, so the image has to be
# placed inside the CHIRP checkout. Always clean it up, including on failure.
cleanup() { rm -f "$IMG"; }
trap cleanup EXIT

cd "$CHIRP"
PYTHONPATH="$CHIRP" python3 "$HERE/make_image.py" "$IMG"

echo
echo "=== CHIRP driver suite vs $(basename "$NR7Y_MODULE") ==="
PYTHONPATH="$HERE:$CHIRP" CHIRP_TESTIMG="$IDENT.img" \
  python3 -m pytest tests/test_drivers.py -q --no-header -p nr7y_plugin "$@"
