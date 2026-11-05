#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# DEPRECATED: this script was renamed to compile-firmware.sh (paired with
# compile-app.sh). This thin wrapper forwards to it for backward compatibility.
# Please update your scripts, docs and habits to call compile-firmware.sh.
# ---------------------------------------------------------------------------
echo "note: 'compile-with-docker.sh' was renamed to 'compile-firmware.sh' - forwarding." >&2
exec "$(dirname "$0")/compile-firmware.sh" "$@"
