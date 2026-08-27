#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------
# Usage:
#   ./compile-firmware.sh [Preset] [CMake options...]
# Examples:
#   ./compile-firmware.sh Fusion
#   ./compile-firmware.sh Transfer
#   ./compile-firmware.sh FieldOps
#   ./compile-firmware.sh Labs
#   ./compile-firmware.sh Fusion -DDEV=ON
#   ./compile-firmware.sh FieldOps -DENABLE_VOX=OFF
#   ./compile-firmware.sh All
# Default preset: "Fusion"
# ---------------------------------------------

IMAGE=uvk1-uvk5v3
RELEASE_PRESETS=(Fusion Transfer FieldOps Labs)
FLASH_LIMIT=$((118 * 1024))
RAM_LIMIT=$((16 * 1024))

PRESET=${1:-Fusion}
shift || true  # remove preset from arguments if present

# Any remaining args will be treated as CMake cache variables
EXTRA_ARGS=("$@")

# ---------------------------------------------
# Validate preset name
# ---------------------------------------------
if [[ ! "$PRESET" =~ ^(Custom|Fusion|Transfer|FieldOps|Labs|All)$ ]]; then
  echo "❌ Unknown preset: '$PRESET'"
  echo "Valid presets are: Custom, Fusion, Transfer, FieldOps, Labs, All"
  exit 1
fi

QUIET=0
if [[ "$PRESET" == "All" ]]; then
  QUIET=1
fi

# Remember whether the script itself was started from an interactive terminal.
# The build output is piped through tee below, so checking stdout later from
# run_preset_build() would always report a non-terminal.
INTERACTIVE=0
if [[ -t 1 ]]; then
  INTERACTIVE=1
fi

# ---------------------------------------------
# Build the Docker image (only needed once)
# ---------------------------------------------
if [[ "$(docker images -q "$IMAGE")" == "" ]]; then
  echo "Building Docker image..."
  docker build -q -t "$IMAGE" . >/dev/null
fi
export MSYS_NO_PATHCONV=1

RESULT_PRESETS=()
RESULT_FLASH_SIZES=()
RESULT_RAM_SIZES=()

run_preset_build() {
  local preset="$1"
  local -a docker_tty_args=()

  # Give Ninja a pseudo-terminal for an interactive single-preset build. This
  # lets it refresh its [current/total] progress on one line. Batch/redirected
  # builds keep plain line-oriented output suitable for logs and CI.
  if (( INTERACTIVE && ! QUIET )); then
    docker_tty_args=(-t)
  fi

  docker run --rm "${docker_tty_args[@]}" \
    -u "$(id -u):$(id -g)" \
    -v "$PWD":/src -w /src "$IMAGE" \
    bash -c 'which arm-none-eabi-gcc && arm-none-eabi-gcc --version &&
             cmake --fresh --preset "$1" "${@:2}" &&
             cmake --build --preset "$1" -j' \
    bash "$preset" "${EXTRA_ARGS[@]}"
}

build_preset() {
  local preset="$1"
  local preset_slug log_file bin_file flash_size ram_size status

  preset_slug="${preset,,}"
  log_file="$(mktemp)"
  bin_file=""

  find "build/${preset}" -maxdepth 1 -type f -name 'f4hwn.*' -delete 2>/dev/null || true

  if (( QUIET )); then
    printf "Building %-10s ... " "$preset"
    if run_preset_build "$preset" >"$log_file" 2>&1; then
      status=0
    else
      status=$?
    fi
  else
    echo ""
    echo "=== 🚀 Building preset: ${preset} ==="
    echo "---------------------------------------------"
    if (( INTERACTIVE )); then
      # Do not put a line-oriented filter after tee here: it would buffer
      # Ninja's carriage-return progress updates and defeat the TTY display.
      if run_preset_build "$preset" 2>&1 | tee "$log_file"; then
        status=0
      else
        status=$?
      fi
    else
      if run_preset_build "$preset" 2>&1 | tee "$log_file" | awk '
        /^Memory region[[:space:]]+Used Size[[:space:]]+Region Size/ { next }
        /^[[:space:]]+RAM:[[:space:]]/ { next }
        /^[[:space:]]+FLASH:[[:space:]]/ { next }
        { print; fflush() }
      '; then
        status=0
      else
        status=$?
      fi
    fi
  fi

  if (( status != 0 )); then
    if (( QUIET )); then
      printf "FAILED\n\n"
      cat "$log_file"
    else
      echo "Failed: ${preset}"
    fi
    rm -f -- "$log_file"
    return "$status"
  fi

  bin_file="build/${preset}/f4hwn.${preset_slug}.bin"
  if [[ -z "$bin_file" || ! -f "$bin_file" ]]; then
    if (( QUIET )); then
      printf "FAILED\n"
    fi
    echo "Expected binary not found: $bin_file"
    if (( QUIET )); then
      cat "$log_file"
    fi
    rm -f -- "$log_file"
    return 1
  fi

  flash_size="$(wc -c < "$bin_file")"
  ram_size="$(awk '$1 == "RAM:" {
    value = $2
    if ($3 == "KB") value *= 1024
    else if ($3 == "MB") value *= 1024 * 1024
    printf "%.0f\n", value
  }' "$log_file" | tail -n 1)"

  if [[ ! "$ram_size" =~ ^[0-9]+$ ]]; then
    if (( QUIET )); then
      printf "FAILED\n"
    fi
    echo "Could not read RAM usage from linker output"
    if (( QUIET )); then
      cat "$log_file"
    fi
    rm -f -- "$log_file"
    return 1
  fi

  RESULT_PRESETS+=("$preset")
  RESULT_FLASH_SIZES+=("$flash_size")
  RESULT_RAM_SIZES+=("$ram_size")

  if (( QUIET )); then
    printf "OK\n"
  else
    echo "✅ Done: ${preset}"
  fi
  rm -f -- "$log_file"
}

print_summary() {
  local i
  local flash_used flash_free flash_bp flash_pct
  local flash_used_kib100 flash_free_kib100 flash_used_kib flash_free_kib
  local ram_used ram_free ram_bp ram_pct
  local ram_used_kib100 ram_free_kib100 ram_used_kib ram_free_kib

  echo ""
  printf "Flash (limit: %d B / %d.00 KiB)\n" "$FLASH_LIMIT" "$((FLASH_LIMIT / 1024))"
  printf "%-10s | %10s | %10s | %10s | %10s | %7s\n" \
    "Preset" "Used (B)" "Used (KiB)" "Free (B)" "Free (KiB)" "Usage"
  printf "%-10s-+-%10s-+-%10s-+-%10s-+-%10s-+-%7s\n" \
    "----------" "----------" "----------" "----------" "----------" "-------"

  for i in "${!RESULT_PRESETS[@]}"; do
    flash_used="${RESULT_FLASH_SIZES[$i]}"
    flash_free=$((FLASH_LIMIT - flash_used))
    flash_bp=$(((flash_used * 10000 + FLASH_LIMIT / 2) / FLASH_LIMIT))
    flash_used_kib100=$(((flash_used * 100 + 512) / 1024))
    flash_free_kib100=$(((flash_free * 100 + 512) / 1024))
    printf -v flash_pct "%d.%02d%%" "$((flash_bp / 100))" "$((flash_bp % 100))"
    printf -v flash_used_kib "%d.%02d" "$((flash_used_kib100 / 100))" "$((flash_used_kib100 % 100))"
    printf -v flash_free_kib "%d.%02d" "$((flash_free_kib100 / 100))" "$((flash_free_kib100 % 100))"

    printf "%-10s | %10d | %10s | %10d | %10s | %7s\n" \
      "${RESULT_PRESETS[$i]}" "$flash_used" "$flash_used_kib" \
      "$flash_free" "$flash_free_kib" "$flash_pct"
  done

  echo ""
  printf "RAM (limit: %d B / %d.00 KiB)\n" "$RAM_LIMIT" "$((RAM_LIMIT / 1024))"
  printf "%-10s | %10s | %10s | %10s | %10s | %7s\n" \
    "Preset" "Used (B)" "Used (KiB)" "Free (B)" "Free (KiB)" "Usage"
  printf "%-10s-+-%10s-+-%10s-+-%10s-+-%10s-+-%7s\n" \
    "----------" "----------" "----------" "----------" "----------" "-------"

  for i in "${!RESULT_PRESETS[@]}"; do
    ram_used="${RESULT_RAM_SIZES[$i]}"
    ram_free=$((RAM_LIMIT - ram_used))
    ram_bp=$(((ram_used * 10000 + RAM_LIMIT / 2) / RAM_LIMIT))
    ram_used_kib100=$(((ram_used * 100 + 512) / 1024))
    ram_free_kib100=$(((ram_free * 100 + 512) / 1024))
    printf -v ram_pct "%d.%02d%%" "$((ram_bp / 100))" "$((ram_bp % 100))"
    printf -v ram_used_kib "%d.%02d" "$((ram_used_kib100 / 100))" "$((ram_used_kib100 % 100))"
    printf -v ram_free_kib "%d.%02d" "$((ram_free_kib100 / 100))" "$((ram_free_kib100 % 100))"

    printf "%-10s | %10d | %10s | %10d | %10s | %7s\n" \
      "${RESULT_PRESETS[$i]}" "$ram_used" "$ram_used_kib" \
      "$ram_free" "$ram_free_kib" "$ram_pct"
  done

  echo ""
  if (( ${#RESULT_PRESETS[@]} == 1 )); then
    echo "🎉 Preset ${RESULT_PRESETS[0]} built successfully!"
  else
    echo "🎉 All presets built successfully!"
  fi
}

# ---------------------------------------------
# Handle 'All' preset
# ---------------------------------------------
if [[ "$PRESET" == "All" ]]; then
  for p in "${RELEASE_PRESETS[@]}"; do
    build_preset "$p"
  done
else
  build_preset "$PRESET"
fi
print_summary
