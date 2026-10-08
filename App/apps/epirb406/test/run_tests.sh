#!/usr/bin/env bash
# Host tests for dec406: synthesize PA4-like ADC audio with gen406.py for a set of
# channel conditions, decode with host_dec406, check the decoded fields.
# Needs python3 + numpy and a C compiler. Run from anywhere.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
CC="${CC:-clang}"

"$CC" -O2 -Wall -Wextra -Werror -o "$TMP/host_dec406" "$HERE/host_dec406.c" "$HERE/../dec406.c" || exit 1

LONG=$(python3 "$HERE/frame406.py")
SELF=$(python3 "$HERE/frame406.py" --selftest)
SHORT=$(python3 "$HERE/frame406.py" --short)
FLIP=$(python3 "$HERE/frame406.py" --flip 50)
ELTDT=$(python3 "$HERE/frame406.py" --eltdt)
ELT3LD=$(python3 "$HERE/frame406.py" --eltdt --3ld)

REF_LONG=("long (144 bits), normal frame" "country   : 227" "Std test" "0x123456"
          "position  : 49.27111, 0.78222" "external, homing yes" "BCH       : ok / ok"
          "15-hex ID : 1C7C2468ACFFBFF" "frames    : 1")

pass=0; fail=0
run() {   # run <name> <frame> "<gen args>" <expected...>
    local name="$1" frame="$2" args="$3"; shift 3
    local out
    # shellcheck disable=SC2086
    python3 "$HERE/gen406.py" --frame "$frame" --out "$TMP/s.u16" $args
    out="$("$TMP/host_dec406" "$TMP/s.u16")"
    for want in "$@"; do
        if ! grep -qF -- "$want" <<<"$out"; then
            printf '  ❌ %-28s missing: %s\n' "$name" "$want"
            fail=$((fail + 1)); return
        fi
    done
    printf '  ✅ %s\n' "$name"; pass=$((pass + 1))
}

echo "dec406 host tests"
run "clean"                  "$LONG"  ""                          "${REF_LONG[@]}"
run "CNR 15 dB"              "$LONG"  "--cnr 15"                  "${REF_LONG[@]}"
run "CNR 12 dB"              "$LONG"  "--cnr 12"                  "${REF_LONG[@]}"
run "offset +5 kHz"          "$LONG"  "--foff 5000"               "${REF_LONG[@]}"
run "offset -5 kHz"          "$LONG"  "--foff -5000"              "${REF_LONG[@]}"
run "inverted chain"         "$LONG"  "--invert"                  "${REF_LONG[@]}" "inverted"
run "clock +3000 ppm"        "$LONG"  "--clock-ppm 3000"          "${REF_LONG[@]}"
run "clock -3000 ppm"        "$LONG"  "--clock-ppm -3000"         "${REF_LONG[@]}"
run "rise time 50 us"        "$LONG"  "--rise-us 50"              "${REF_LONG[@]}"
run "rise time 250 us"       "$LONG"  "--rise-us 250"             "${REF_LONG[@]}"
run "audio LPF 3 kHz"        "$LONG"  "--audio-lpf 3000"          "${REF_LONG[@]}"
run "AC coupling 150 Hz"     "$LONG"  "--hpf 150"                 "${REF_LONG[@]}"
run "combined worst case"    "$LONG"  "--cnr 15 --foff 3000 --invert --clock-ppm 2000 --audio-lpf 3500" "${REF_LONG[@]}"
run "self-test frame"        "$SELF"  ""                          "self-test frame" "15-hex ID : 1C7C2468ACFFBFF" "BCH       : ok / ok"
run "short message"          "$SHORT" ""                          "short (112 bits)" "BCH       : ok / ok" "15-hex ID : 1C7C2468ACFFBFF" "(coarse)"
run "ELT(DT) location"       "$ELTDT" ""                          "long (144 bits), normal frame" "ELT-DT" "position  : 49.27111, 0.78222" "BCH       : ok / ok" "15-hex ID : 1C72091A2B3FDFF" "frames    : 1"
run "ELT(DT) with 3LD"    "$ELT3LD" ""                         "ELT-DT" "position  : 49.50000, 1.00000 (coarse)" "BCH       : ok / ok" "15-hex ID : 1C72091A2B3FDFF"
run "corrupted bit 50"       "$FLIP"  ""                          "BCH       : FAIL / ok"
run "3 bursts in a row"      "$LONG"  "--bursts 3"                "frames    : 3"
# Measured PA4 level (~150 LSB peak) at low CNR and with bit-rate error, several
# noise seeds each: a DC-tracker rounding bias once passed the single-seed cases
# above but failed BCH-2 on the radio.
for seed in 1 2 3 4; do
run "CNR 12 dB, seed $seed"      "$LONG"  "--cnr 12 --seed $seed"               "${REF_LONG[@]}"
run "clock +2%, seed $seed"      "$LONG"  "--cnr 15 --clock-ppm 20000 --seed $seed"  "${REF_LONG[@]}"
run "clock -2%, seed $seed"      "$LONG"  "--cnr 15 --clock-ppm -20000 --seed $seed" "${REF_LONG[@]}"
done

echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
