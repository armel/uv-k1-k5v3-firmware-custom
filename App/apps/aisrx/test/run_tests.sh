#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
python3 -B "$HERE/ais.py"
python3 -B "$HERE/model_rx.py"
python3 -B "$HERE/flipper_ais.py" "$HERE/flipper"
