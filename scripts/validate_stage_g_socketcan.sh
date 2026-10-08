#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/stage-g-debug"
RELEASE_DIR="$ROOT_DIR/build/stage-g-release"

echo "[ECU V2] Stage G1 — Linux SocketCAN adapter validation"

echo
echo "=== Repository ==="
git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD
git -C "$ROOT_DIR" status --short

echo
echo "=== Core portability guard ==="
"$ROOT_DIR/scripts/selftest_core_portability_gate.sh"
"$ROOT_DIR/scripts/check_core_portability.sh"

build_and_test() {
  local build_type="$1"
  local build_dir="$2"

  echo
  echo "=== Configure: $build_type ==="
  rm -rf "$build_dir"
  cmake \
    -S "$ROOT_DIR" \
    -B "$build_dir" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DECU_BUILD_TESTS=ON

  echo
  echo "=== Build: $build_type ==="
  cmake --build "$build_dir"

  echo
  echo "=== CTest: $build_type ==="
  ctest --test-dir "$build_dir" --output-on-failure
}

build_and_test Debug "$DEBUG_DIR"
build_and_test Release "$RELEASE_DIR"

echo
echo "=== Live read-only SocketCAN link probe ==="
"$DEBUG_DIR/tests/ecu_socketcan_link_probe" can0

echo
echo "STAGE_G1_SOCKETCAN=PASS"
echo "Physical bind/receive gate remains G2 and must run in listen-only mode."
