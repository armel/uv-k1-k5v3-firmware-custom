#!/usr/bin/env bash

set -euo pipefail

IDF_VERSION="5.5.6"
OUTPUT_NAME="tab5-k1viewer-esp32p4-rev1.bin"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-local"
DIST_DIR="${SCRIPT_DIR}/dist"
SDKCONFIG_PATH="${SCRIPT_DIR}/sdkconfig.local"

DEFAULT_CACHE_ROOT="${HOME}/Library/Caches/tab5-k1viewer"
CACHE_ROOT="${TAB5_BUILD_CACHE:-${DEFAULT_CACHE_ROOT}}"
IDF_DIR="${TAB5_IDF_PATH:-${CACHE_ROOT}/esp-idf-v${IDF_VERSION}}"
IDF_TOOLS_PATH="${TAB5_IDF_TOOLS_PATH:-${CACHE_ROOT}/espressif}"
INSTALL_MARKER="${IDF_TOOLS_PATH}/.tab5-esp32p4-v${IDF_VERSION}-installed"

die()
{
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 || die "missing command: $1"
}

set_sdkconfig_value()
{
    local key="$1"
    local value="$2"
    local temporary_path="${SDKCONFIG_PATH}.tmp"

    [[ -f "${SDKCONFIG_PATH}" ]] || return
    grep -qx "${key}=${value}" "${SDKCONFIG_PATH}" && return

    awk -v key="${key}" -v setting="${key}=${value}" '
        BEGIN { updated = 0 }
        $0 == "# " key " is not set" || index($0, key "=") == 1 {
            if (!updated) print setting
            updated = 1
            next
        }
        { print }
        END { if (!updated) print setting }
    ' "${SDKCONFIG_PATH}" > "${temporary_path}"
    mv "${temporary_path}" "${SDKCONFIG_PATH}"
}

show_help()
{
    cat <<'EOF'
Usage: ./build-local.sh

Builds the M5Stack Tab5 K1Viewer image for ESP32-P4 revision 1.x.

The first run installs ESP-IDF 5.5.6 and the ESP32-P4 toolchain in:
  ~/Library/Caches/tab5-k1viewer

Optional environment variables:
  TAB5_BUILD_CACHE      Override the build tool cache directory.
  TAB5_IDF_PATH         Use an existing ESP-IDF 5.5.6 checkout.
  TAB5_IDF_TOOLS_PATH   Use an existing ESP-IDF tools directory.

Output:
  dist/tab5-k1viewer-esp32p4-rev1.bin

Flash the merged image at address 0x0.
EOF
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

require_command git
require_command python3
require_command shasum
require_command awk

if [[ ! -f "${IDF_DIR}/tools/idf.py" ]]; then
    if [[ -n "${TAB5_IDF_PATH:-}" ]]; then
        die "TAB5_IDF_PATH does not contain a valid ESP-IDF installation: ${IDF_DIR}"
    fi
    if [[ -e "${IDF_DIR}" ]]; then
        die "incomplete ESP-IDF installation in ${IDF_DIR}"
    fi

    printf 'Installing ESP-IDF v%s in %s...\n' "${IDF_VERSION}" "${IDF_DIR}"
    mkdir -p "$(dirname "${IDF_DIR}")"
    git clone --branch "v${IDF_VERSION}" --depth 1 --recursive \
        --shallow-submodules https://github.com/espressif/esp-idf.git "${IDF_DIR}"
fi

if [[ ! -f "${INSTALL_MARKER}" ]]; then
    printf 'Installing the ESP32-P4 toolchain...\n'
    mkdir -p "${IDF_TOOLS_PATH}"
    IDF_TOOLS_PATH="${IDF_TOOLS_PATH}" "${IDF_DIR}/install.sh" esp32p4
fi

export IDF_TOOLS_PATH
source "${IDF_DIR}/export.sh" >/dev/null

if [[ -z "${IDF_PYTHON_ENV_PATH:-}" || \
      ! -x "${IDF_PYTHON_ENV_PATH}/bin/python" ]]; then
    die "the ESP-IDF Python environment was not initialized"
fi
PYTHON_BIN="${IDF_PYTHON_ENV_PATH}/bin/python"

if ! command -v riscv32-esp-elf-gcc >/dev/null 2>&1; then
    printf 'Installing the required ESP32-P4 tools...\n'
    "${PYTHON_BIN}" "${IDF_DIR}/tools/idf_tools.py" install \
        --targets esp32p4 required
    source "${IDF_DIR}/export.sh" >/dev/null
fi

MISSING_HOST_TOOLS=()
command -v cmake >/dev/null 2>&1 || MISSING_HOST_TOOLS+=(cmake)
command -v ninja >/dev/null 2>&1 || MISSING_HOST_TOOLS+=(ninja)

if [[ ${#MISSING_HOST_TOOLS[@]} -gt 0 ]]; then
    printf 'Installing missing build tools: %s...\n' \
        "${MISSING_HOST_TOOLS[*]}"
    "${PYTHON_BIN}" "${IDF_DIR}/tools/idf_tools.py" install \
        "${MISSING_HOST_TOOLS[@]}"
    source "${IDF_DIR}/export.sh" >/dev/null
fi

require_command riscv32-esp-elf-gcc
require_command cmake
require_command ninja
touch "${INSTALL_MARKER}"

IDF_VERSION_OUTPUT="$("${PYTHON_BIN}" "${IDF_DIR}/tools/idf.py" --version)"
case "${IDF_VERSION_OUTPUT}" in
    *"v${IDF_VERSION}"*)
        ;;
    *)
        die "ESP-IDF v${IDF_VERSION} required, detected version: ${IDF_VERSION_OUTPUT}"
        ;;
esac

set_sdkconfig_value CONFIG_LV_FONT_UNSCII_16 y

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]] &&
   { ! grep -Fqx "CMAKE_PROJECT_NAME:STATIC=tab5_k1viewer" \
         "${BUILD_DIR}/CMakeCache.txt" ||
     ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=${SCRIPT_DIR}" \
         "${BUILD_DIR}/CMakeCache.txt"; }; then
    printf 'Removing stale build cache after project rename...\n'
    rm -rf -- "${BUILD_DIR}"
fi

if [[ -f "${SDKCONFIG_PATH}" ]]; then
    grep -qx 'CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y' "${SDKCONFIG_PATH}" || \
        die "sdkconfig.local does not target ESP32-P4 v1.x"
    grep -qx 'CONFIG_ESP32P4_REV_MIN_100=y' "${SDKCONFIG_PATH}" || \
        die "sdkconfig.local does not target minimum revision v1.0"
    grep -qx 'CONFIG_LV_FONT_UNSCII_16=y' "${SDKCONFIG_PATH}" || \
        die "sdkconfig.local does not enable the UNSCII 16 font"
fi

printf 'Building the Tab5 client for ESP32-P4 v1.x...\n'
"${PYTHON_BIN}" "${IDF_DIR}/tools/idf.py" \
    -B "${BUILD_DIR}" \
    -D "IDF_TARGET=esp32p4" \
    -D "SDKCONFIG=${SDKCONFIG_PATH}" \
    build

grep -qx 'CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y' "${SDKCONFIG_PATH}" || \
    die "the generated configuration does not target ESP32-P4 v1.x"
grep -qx 'CONFIG_ESP32P4_REV_MIN_100=y' "${SDKCONFIG_PATH}" || \
    die "the generated configuration does not target minimum revision v1.0"
grep -qx 'CONFIG_LV_FONT_UNSCII_16=y' "${SDKCONFIG_PATH}" || \
    die "the generated configuration does not enable the UNSCII 16 font"

BOOTLOADER_BIN="${BUILD_DIR}/bootloader/bootloader.bin"
PARTITION_BIN="${BUILD_DIR}/partition_table/partition-table.bin"
APPLICATION_BIN="${BUILD_DIR}/tab5_k1viewer.bin"
OUTPUT_BIN="${DIST_DIR}/${OUTPUT_NAME}"

[[ -f "${BOOTLOADER_BIN}" ]] || die "bootloader not found"
[[ -f "${PARTITION_BIN}" ]] || die "partition table not found"
[[ -f "${APPLICATION_BIN}" ]] || die "application not found"

mkdir -p "${DIST_DIR}"
"${PYTHON_BIN}" -m esptool --chip esp32p4 merge_bin \
    -o "${OUTPUT_BIN}" \
    --flash_mode dio \
    --flash_freq 80m \
    --flash_size 16MB \
    0x2000 "${BOOTLOADER_BIN}" \
    0x8000 "${PARTITION_BIN}" \
    0x10000 "${APPLICATION_BIN}"

validate_image_revision()
{
    local image_path="$1"
    local image_name="$2"
    local image_info

    image_info="$("${PYTHON_BIN}" -m esptool image_info --version 2 "${image_path}")"
    printf '%s\n' "${image_info}" | \
        grep -Fq 'Minimal chip revision: v1.0' || \
        die "${image_name}: incorrect minimum chip revision"
    printf '%s\n' "${image_info}" | \
        grep -Fq 'Maximal chip revision: v1.99' || \
        die "${image_name}: incorrect maximum chip revision"
}

validate_image_revision "${BOOTLOADER_BIN}" "bootloader"
validate_image_revision "${APPLICATION_BIN}" "application"

(
    cd "${DIST_DIR}"
    shasum -a 256 "${OUTPUT_NAME}" > "${OUTPUT_NAME}.sha256"
)

printf '\nBuild complete.\n'
printf 'Image: %s\n' "${OUTPUT_BIN}"
printf 'SHA-256: '
cut -d ' ' -f 1 "${OUTPUT_BIN}.sha256"
printf 'Flash address: 0x0\n'
