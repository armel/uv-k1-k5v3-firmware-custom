#!/usr/bin/env bash
# Standard compile-app.sh entry; the portable builder also supports Windows.
set -euo pipefail
cd "$(dirname "$0")"
exec python3 build.py
