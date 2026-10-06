#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/stage-h-debug"
RELEASE_DIR="$ROOT_DIR/build/stage-h-release"

echo "[ECU V2] Stage H — Core ISO-TP validation"

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
echo "=== Direct ISO-TP test ==="
"$DEBUG_DIR/tests/ecu_isotp_core_tests"

echo
echo "STAGE_H_ISOTP=PASS"
