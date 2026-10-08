#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/stage-j-debug"
RELEASE_DIR="$ROOT_DIR/build/stage-j-release"

echo "[ECU V2] Stage J — SAC module validation"

echo
echo "=== Repository ==="
git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD
git -C "$ROOT_DIR" status --short

echo
echo "=== Core portability guard ==="
"$ROOT_DIR/scripts/selftest_core_portability_gate.sh"
"$ROOT_DIR/scripts/check_core_portability.sh"

echo
echo "=== SAC module portability guard ==="
ECU_CORE_PORTABILITY_ROOT="$ROOT_DIR/src/ecu/sac"   "$ROOT_DIR/scripts/check_core_portability.sh"

build_and_test() {
  local build_type="$1"
  local build_dir="$2"

  echo
  echo "=== Configure: $build_type ==="
  rm -rf "$build_dir"
  cmake     -S "$ROOT_DIR"     -B "$build_dir"     -G Ninja     -DCMAKE_BUILD_TYPE="$build_type"     -DECU_BUILD_TESTS=ON

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
echo "=== Direct SAC module test ==="
"$DEBUG_DIR/tests/ecu_sac_module_tests"

echo
echo "STAGE_J_SAC=PASS"
