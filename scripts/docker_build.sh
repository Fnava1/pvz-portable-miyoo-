#!/usr/bin/env bash
set -euo pipefail

# If running directly inside the toolchain container
if [ -f /.dockerenv ]; then
  echo "Running directly inside Miyoo Mini toolchain container..."
  source /root/setup-env.sh 2>/dev/null || true
  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/miyoomini.toolchain.cmake "$@"
  cmake --build build
  exit 0
fi

# Locate repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Prevent path mangling on Windows MSYS / Git Bash
export MSYS_NO_PATHCONV=1

# Format host path for Docker volume mount
DOCKER_MOUNT="${WORKSPACE_DIR}"
if command -v cygpath >/dev/null 2>&1; then
  DOCKER_MOUNT="$(cygpath -m "${WORKSPACE_DIR}")"
fi

echo "=========================================================="
echo " Building PvZ_Miyoo for Miyoo Mini Plus via Docker"
echo " Image: aemiii91/miyoomini-toolchain:latest"
echo " Mount: ${DOCKER_MOUNT} -> /root/workspace"
echo "=========================================================="

docker run --rm \
  -v "${DOCKER_MOUNT}:/root/workspace" \
  -w /root/workspace \
  aemiii91/miyoomini-toolchain:latest \
  bash -c "source /root/setup-env.sh 2>/dev/null || true; cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/miyoomini.toolchain.cmake && cmake --build build"

echo "Build complete."
