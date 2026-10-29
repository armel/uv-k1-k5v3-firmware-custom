#!/usr/bin/env bash

set -euo pipefail

ARDUINO_CLI_VERSION="1.5.1"
ESP32_CORE_VERSION="3.3.12"
M5CARDPUTER_VERSION="1.1.1"
M5UNIFIED_VERSION="0.2.28"
M5GFX_VERSION="0.2.35"
OUTPUT_NAME="cardputer-k1viewer-esp32s3.bin"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="${SCRIPT_DIR}/dist"
DEFAULT_CACHE_ROOT="${HOME}/Library/Caches/cardputer-k1viewer"
CACHE_ROOT="${CARDPUTER_BUILD_CACHE:-${DEFAULT_CACHE_ROOT}}"
CLI_DIR="${CACHE_ROOT}/arduino-cli-${ARDUINO_CLI_VERSION}"
CLI_BIN="${CLI_DIR}/arduino-cli"
ARDUINO_DATA_DIR="${CACHE_ROOT}/arduino-data"
ARDUINO_DOWNLOADS_DIR="${CACHE_ROOT}/downloads"
ARDUINO_USER_DIR="${CACHE_ROOT}/user"
ARDUINO_CONFIG="${CACHE_ROOT}/arduino-cli.yaml"
TEMP_DIR=""
BUILD_ROOT=""

cleanup()
{
    if [[ -n "${TEMP_DIR}" ]]; then
        rm -rf "${TEMP_DIR}"
    fi
    if [[ -n "${BUILD_ROOT}" ]]; then
        rm -rf "${BUILD_ROOT}"
    fi
}

trap cleanup EXIT

die()
{
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 || die "missing command: $1"
}

show_help()
{
    printf '%s\n' \
        'Usage: ./build-local.sh' \
        '' \
        'Builds the M5Stack Cardputer K1Viewer merged image.' \
        '' \
        'The first run installs the pinned Arduino CLI, ESP32 core, and' \
        'M5Stack libraries under:' \
        '  ~/Library/Caches/cardputer-k1viewer' \
        '' \
        'Optional environment variable:' \
        '  CARDPUTER_BUILD_CACHE  Override the build tool cache directory.' \
        '' \
        'Output:' \
        '  dist/cardputer-k1viewer-esp32s3.bin' \
        '' \
        'Flash the merged image at address 0x0.'
}

case "${1:-}" in
    "")
        ;;
    -h|--help)
        show_help
        exit 0
        ;;
    *)
        die "unknown option: $1"
        ;;
esac

require_command curl
require_command tar
require_command awk
require_command shasum

if [[ ! -x "${CLI_BIN}" ]]; then
    case "$(uname -s)/$(uname -m)" in
        Darwin/arm64)
            PLATFORM="macOS_ARM64"
            ;;
        Darwin/x86_64)
            PLATFORM="macOS_64bit"
            ;;
        Linux/aarch64|Linux/arm64)
            PLATFORM="Linux_ARM64"
            ;;
        Linux/x86_64)
            PLATFORM="Linux_64bit"
            ;;
        *)
            die "unsupported host: $(uname -s)/$(uname -m)"
            ;;
    esac

    ARCHIVE="arduino-cli_${ARDUINO_CLI_VERSION}_${PLATFORM}.tar.gz"
    RELEASE_URL="https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}"
    TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cardputer-cli.XXXXXX")"

    printf 'Installing Arduino CLI %s...\n' "${ARDUINO_CLI_VERSION}"
    curl -fL "${RELEASE_URL}/${ARCHIVE}" -o "${TEMP_DIR}/${ARCHIVE}"
    curl -fL "${RELEASE_URL}/${ARDUINO_CLI_VERSION}-checksums.txt" \
        -o "${TEMP_DIR}/checksums.txt"

    EXPECTED_SHA="$(awk -v name="${ARCHIVE}" '$2 == name { print $1 }' \
        "${TEMP_DIR}/checksums.txt")"
    [[ -n "${EXPECTED_SHA}" ]] || die "checksum not found for ${ARCHIVE}"
    ACTUAL_SHA="$(shasum -a 256 "${TEMP_DIR}/${ARCHIVE}" | awk '{ print $1 }')"
    [[ "${ACTUAL_SHA}" == "${EXPECTED_SHA}" ]] || \
        die "Arduino CLI checksum mismatch"

    mkdir -p "${CLI_DIR}"
    tar -xzf "${TEMP_DIR}/${ARCHIVE}" -C "${CLI_DIR}"
fi

mkdir -p "${ARDUINO_DATA_DIR}" "${ARDUINO_DOWNLOADS_DIR}" \
    "${ARDUINO_USER_DIR}" "${DIST_DIR}"

printf '%s\n' \
    'board_manager:' \
    '  additional_urls:' \
    '    - https://espressif.github.io/arduino-esp32/package_esp32_index.json' \
    'directories:' \
    "  data: ${ARDUINO_DATA_DIR}" \
    "  downloads: ${ARDUINO_DOWNLOADS_DIR}" \
    "  user: ${ARDUINO_USER_DIR}" > "${ARDUINO_CONFIG}"

printf 'Preparing Arduino dependencies...\n'
"${CLI_BIN}" core update-index --config-file "${ARDUINO_CONFIG}"
"${CLI_BIN}" core install "esp32:esp32@${ESP32_CORE_VERSION}" \
    --config-file "${ARDUINO_CONFIG}"
"${CLI_BIN}" lib install "M5Cardputer@${M5CARDPUTER_VERSION}" \
    --config-file "${ARDUINO_CONFIG}"
"${CLI_BIN}" lib install "M5Unified@${M5UNIFIED_VERSION}" \
    --config-file "${ARDUINO_CONFIG}"
"${CLI_BIN}" lib install "M5GFX@${M5GFX_VERSION}" \
    --config-file "${ARDUINO_CONFIG}"

BUILD_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/cardputer-k1viewer.XXXXXX")"

FQBN="esp32:esp32:m5stack_cardputer:USBMode=default,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=disabled,FlashMode=qio,CPUFreq=240"

printf 'Building Cardputer K1Viewer...\n'
"${CLI_BIN}" compile \
    --config-file "${ARDUINO_CONFIG}" \
    --fqbn "${FQBN}" \
    --build-path "${BUILD_ROOT}/build" \
    --output-dir "${BUILD_ROOT}/output" \
    "${SCRIPT_DIR}"

MERGED_BIN="$(find "${BUILD_ROOT}/output" -maxdepth 1 -type f \
    -name '*.merged.bin' -print -quit)"
[[ -n "${MERGED_BIN}" && -f "${MERGED_BIN}" ]] || \
    die "merged ESP32-S3 image not found"

OUTPUT_BIN="${DIST_DIR}/${OUTPUT_NAME}"
cp "${MERGED_BIN}" "${OUTPUT_BIN}"
(
    cd "${DIST_DIR}"
    shasum -a 256 "${OUTPUT_NAME}" > "${OUTPUT_NAME}.sha256"
)

printf '\nBuild complete.\n'
printf 'Image: %s\n' "${OUTPUT_BIN}"
printf 'SHA-256: '
awk '{ print $1 }' "${OUTPUT_BIN}.sha256"
printf 'Flash address: 0x0\n'

