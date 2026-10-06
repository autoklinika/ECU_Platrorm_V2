#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/stage-l-debug"
RELEASE_DIR="$ROOT_DIR/build/stage-l-release"

echo "[ECU V2] Stage L — SAC controller validation"

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
echo "=== Direct SAC controller test ==="
"$DEBUG_DIR/tests/ecu_sac_controller_tests"

echo
echo "STAGE_L_SAC_CONTROLLER=PASS"
