#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Build overlay-app blobs (.app), the counterpart of compile-firmware.sh.
#
# Usage:
#   ./compile-app.sh                # build every non-test app
#   ./compile-app.sh All            # build every non-test app
#   ./compile-app.sh breakout       # build one app
#   ./compile-app.sh fm foxhunt     # build several apps
#
# Each app is compiled in the uvk1-uvk5v3 Docker image, linked at the pinned
# overlay VMA, packed into build/Apps/<Name>.app (named after the app's human
# label, spaces stripped), and reported against the 4 KiB overlay budget. Apps
# are auto-discovered from App/apps/<name>/build.sh.
# ---------------------------------------------------------------------------
set -euo pipefail

IMAGE=uvk1-uvk5v3
APPS_DIR=App/apps
OUT_DIR=build/Apps
OVERLAY_MAX=4096
APP_VMA=${APP_VMA:-0x20000280}      # pinned overlay VMA (Core/py32f071xb.ld)
ALL_EXCLUDED_APPS=(handoffa handoffb vfotest) # API test apps remain explicit targets

cd "$(dirname "$0")"

# Prevent Git Bash/MSYS from rewriting container paths as Windows paths.
export MSYS_NO_PATHCONV=1

# --- discover apps (any App/apps/<name>/build.sh) ---
ALL_APPS=()
for d in "$APPS_DIR"/*/; do
    [ -f "${d}build.sh" ] && ALL_APPS+=("$(basename "$d")")
done
if [ "${#ALL_APPS[@]}" -eq 0 ]; then
    echo "❌ No apps found under $APPS_DIR/*/build.sh"; exit 1
fi

# --- select targets ---
if [ "$#" -eq 0 ] || [[ "${1:-}" == [Aa][Ll][Ll] ]]; then
    TARGETS=()
    for app in "${ALL_APPS[@]}"; do
        excluded=0
        for test_app in "${ALL_EXCLUDED_APPS[@]}"; do
            [ "$app" = "$test_app" ] && excluded=1
        done
        [ "$excluded" -eq 0 ] && TARGETS+=("$app")
    done
else
    TARGETS=()
    for want in "$@"; do
        ok=0
        for a in "${ALL_APPS[@]}"; do [ "$a" = "$want" ] && ok=1; done
        if [ "$ok" -eq 0 ]; then
            echo "❌ Unknown app '$want'. Available: ${ALL_APPS[*]} (or All)"; exit 1
        fi
        TARGETS+=("$want")
    done
fi

command -v docker >/dev/null 2>&1 || { echo "❌ docker not found"; exit 1; }
if [[ "$(docker images -q "$IMAGE")" == "" ]]; then
    echo "🐳 Building Docker image..."; docker build -q -t "$IMAGE" . >/dev/null
fi
mkdir -p "$OUT_DIR"

# Give the container a pseudo-TTY when we are interactive, so build.sh's
# single-line [k/3] progress refreshes in place (same trick as the firmware
# build gives Ninja). Batch/redirected runs stay plain and line-oriented.
TTY_ARG=""; [[ -t 1 ]] && TTY_ARG="-t"

echo
echo "🚀 Building overlay apps"
printf '   VMA %s · budget %d B / %d.00 KiB · out %s/\n\n' \
    "$APP_VMA" "$OVERLAY_MAX" "$((OVERLAY_MAX / 1024))" "$OUT_DIR"

files=(); sizes=(); vmas=(); states=(); asset_sizes=()
fail=0

read_u32le() { od -An -tx1 -j"$2" -N4 "$1" | awk 'NF{printf "0x%s%s%s%s",$4,$3,$2,$1}'; }
read_u16le() { od -An -tx1 -j"$2" -N2 "$1" | awk 'NF{printf "0x%s%s",$2,$1}'; }

for app in "${TARGETS[@]}"; do
    # Existing Windows clones may still contain CRLF app scripts even after
    # .gitattributes is updated, so normalize the script before Bash reads it.
    if docker run --rm ${TTY_ARG:+"$TTY_ARG"} -u "$(id -u):$(id -g)" \
            -v "$PWD":/work -w "/work/$APPS_DIR/$app" \
            -e PATH="/opt/toolchain/bin:/usr/bin:/bin" -e APP_VMA="$APP_VMA" \
            "$IMAGE" bash -c "set -eo pipefail; rm -f -- \"\$1\"; sed 's/\r$//' ./build.sh | bash" \
            bash "$app.map"; then
        appfile=$(ls -1 "$APPS_DIR/$app"/*.app 2>/dev/null | head -1)
        if [ -n "$appfile" ] && [ -f "$appfile" ]; then
            cp -f "$appfile" "$OUT_DIR/"
            base=$(basename "$appfile")
            code=$(( $(read_u32le "$appfile" 8) ))       # app_header_t.code_size
            assets=$(( $(read_u16le "$appfile" 60) ))    # app_header_t.asset_size
            vma=$(read_u32le "$appfile" 52)
            state="✅ OK"; [ "$code" -gt "$OVERLAY_MAX" ] && { state="🚨 OVERFLOW (+$((code - OVERLAY_MAX)) B)"; fail=1; }
            [ "$assets" -gt 0 ] && state="$state (+$assets B assets)"
        else
            base="$app.app"; code=-1; assets=0; vma="--"; state="❌ NO BLOB"; fail=1
        fi
    else
        base="$app.app"; code=-1; assets=0; vma="--"; state="❌ BUILD FAIL"; fail=1
        # The linker still emits a map when the overlay size assertion fails.
        # The container removes the previous map before building, so a compile
        # error cannot be mistaken for an overflow from an earlier attempt.
        mapfile="$APPS_DIR/$app/$app.map"
        if [ -f "$mapfile" ]; then
            map_size=$(awk '$1 == ".app" && $2 ~ /^0x[0-9a-fA-F]+$/ && $3 ~ /^0x[0-9a-fA-F]+$/ { print $3; exit }' "$mapfile")
            if [[ "$map_size" =~ ^0x[0-9a-fA-F]+$ ]] && [ "$((map_size))" -gt "$OVERLAY_MAX" ]; then
                state="🚨 OVERFLOW (+$((map_size - OVERLAY_MAX)) B; $((map_size)) / $OVERLAY_MAX B)"
            fi
        fi
    fi
    files+=("$base"); sizes+=("$code"); vmas+=("$vma"); states+=("$state"); asset_sizes+=("$assets")
done

# --- memory report (styled like the firmware Flash/RAM tables) ---
echo
printf '📦 Overlay apps (budget: %d B / %d.00 KiB)\n' "$OVERLAY_MAX" "$((OVERLAY_MAX / 1024))"
printf '%-16s | %9s | %9s | %9s | %9s | %7s | %s\n' \
    "File" "Code (B)" "Code(KiB)" "Free (B)" "Free(KiB)" "Usage" "Status"
printf '%-16s-+-%9s-+-%9s-+-%9s-+-%9s-+-%7s-+-%s\n' \
    "----------------" "---------" "---------" "---------" "---------" "-------" "----------"
for i in "${!files[@]}"; do
    c=${sizes[$i]}
    if [ "$c" -ge 0 ]; then
        free=$(( OVERLAY_MAX - c ))
        bp=$(( (c * 10000 + OVERLAY_MAX / 2) / OVERLAY_MAX ))
        ck100=$(( (c * 100 + 512) / 1024 ));  fk100=$(( (free * 100 + 512) / 1024 ))
        printf -v pct   '%d.%02d%%' "$((bp/100))" "$((bp%100))"
        printf -v ckib  '%d.%02d'   "$((ck100/100))" "$((ck100%100))"
        printf -v fkib  '%d.%02d'   "$((fk100/100))" "$((fk100%100))"
        printf '%-16s | %9d | %9s | %9d | %9s | %7s | %s\n' \
            "${files[$i]}" "$c" "$ckib" "$free" "$fkib" "$pct" "${states[$i]}"
    else
        printf '%-16s | %9s | %9s | %9s | %9s | %7s | %s\n' \
            "${files[$i]}" "-" "-" "-" "-" "-" "${states[$i]}"
    fi
done

# --- aggregate successful builds once for the table and final summary ---
tot_code=0; tot_free=0; tot_assets=0; built=0
for i in "${!files[@]}"; do
    c=${sizes[$i]}
    [ "$c" -ge 0 ] || continue
    tot_code=$(( tot_code + c )); tot_free=$(( tot_free + OVERLAY_MAX - c ))
    tot_assets=$(( tot_assets + ${asset_sizes[$i]} )); built=$(( built + 1 ))
done

# --- totals row (only meaningful with more than one app) ---
if [ "${#files[@]}" -gt 1 ]; then
    printf '%-16s-+-%9s-+-%9s-+-%9s-+-%9s-+-%7s-+-%s\n' \
        "----------------" "---------" "---------" "---------" "---------" "-------" "----------"
    if [ "$built" -gt 0 ]; then
        # Usage = average fill of the built apps (total code / built x budget).
        bp=$(( (tot_code * 10000 + built * OVERLAY_MAX / 2) / (built * OVERLAY_MAX) ))
        ck100=$(( (tot_code * 100 + 512) / 1024 ));  fk100=$(( (tot_free * 100 + 512) / 1024 ))
        printf -v pct   '%d.%02d%%' "$((bp/100))" "$((bp%100))"
        printf -v ckib  '%d.%02d'   "$((ck100/100))" "$((ck100%100))"
        printf -v fkib  '%d.%02d'   "$((fk100/100))" "$((fk100%100))"
        tstate="$built/${#files[@]} apps, avg usage"
        [ "$tot_assets" -gt 0 ] && tstate="$tstate (+$tot_assets B assets)"
        printf '%-16s | %9d | %9s | %9d | %9s | %7s | %s\n' \
            "TOTAL" "$tot_code" "$ckib" "$tot_free" "$fkib" "$pct" "$tstate"
    else
        printf '%-16s | %9s | %9s | %9s | %9s | %7s | %s\n' \
            "TOTAL" "-" "-" "-" "-" "-" "0/${#files[@]} apps"
    fi
fi

echo
if [ "$fail" -eq 0 ]; then
    if [ "${#files[@]}" -eq 1 ]; then
        echo "🎉 App ${files[0]} built successfully  ->  $OUT_DIR/"
    else
        echo "🎉 All ${#files[@]} apps built successfully  ->  $OUT_DIR/"
    fi
else
    echo "⚠️  Some apps failed or overflowed — see the table above."
fi

echo
if [ "$built" -gt 0 ]; then
    super_total=$(( tot_code + tot_assets ))
    apps_k100=$(( (tot_code * 100 + 512) / 1024 ))
    assets_k100=$(( (tot_assets * 100 + 512) / 1024 ))
    super_k100=$(( (super_total * 100 + 512) / 1024 ))
    printf '✨ Super total (%d/%d apps)\n' "$built" "${#files[@]}"
    printf '   Apps          %9d B  (%d.%02d KiB)\n' \
        "$tot_code" "$((apps_k100/100))" "$((apps_k100%100))"
    printf ' + Assets        %9d B  (%d.%02d KiB)\n' \
        "$tot_assets" "$((assets_k100/100))" "$((assets_k100%100))"
    printf ' = Apps + assets %9d B  (%d.%02d KiB)\n' \
        "$super_total" "$((super_k100/100))" "$((super_k100%100))"
else
    printf '✨ Super total unavailable (0/%d apps built)\n' "${#files[@]}"
fi
exit $fail
