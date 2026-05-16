#!/usr/bin/env bash
set -euo pipefail

# GOGUFW public build helper.
# Usage:
#   ./compile-with-docker.sh [extra CMake options...]
#
# The only supported public build target is the GGFW firmware build,
# implemented through the upstream Fusion feature set.

IMAGE=uvk1-uvk5v3
PRESET=Fusion
EXTRA_ARGS=()

# Backward-compatible aliases: allow ./compile-with-docker.sh Fusion or GGFW,
# but do not expose the old multi-edition preset list.
if [[ $# -gt 0 && ( "$1" == "Fusion" || "$1" == "GGFW" || "$1" == "gogufw" ) ]]; then
  shift
fi
EXTRA_ARGS=("$@")

if [[ "$(docker images -q $IMAGE)" == "" ]]; then
  echo "Building Docker image..."
  docker build -t "$IMAGE" .
fi

rm -rf build/Fusion build/GGFW build/gogufw
export MSYS_NO_PATHCONV=1

echo ""
echo "=== 🚀 Building GOGUFW 0.3.3 ==="
echo "---------------------------------------------"
docker run --rm \
  -u $(id -u):$(id -g) \
  -v "$PWD":/src -w /src "$IMAGE" \
  bash -c "which arm-none-eabi-gcc && arm-none-eabi-gcc --version && \
           cmake --preset ${PRESET} ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} && \
           cmake --build --preset ${PRESET} -j"
echo "✅ Done: GOGUFW 0.3.3"
