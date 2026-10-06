#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/core-hardening-debug"
RELEASE_DIR="$ROOT_DIR/build/core-hardening-release"
SANITIZE_DIR="$ROOT_DIR/build/core-hardening-sanitize"

echo "[ECU V2] Core Hardening v1 validation"

echo
echo "=== Repository ==="
git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD
git -C "$ROOT_DIR" status --short

echo
echo "=== Standards baseline gate ==="
bash "$ROOT_DIR/scripts/check_core_standards.sh"

echo
echo "=== Architecture gate ==="
"$ROOT_DIR/scripts/check_core_architecture.sh"

echo
echo "=== Portability gate self-test ==="
"$ROOT_DIR/scripts/selftest_core_portability_gate.sh"

echo
echo "=== Core portability gate ==="
"$ROOT_DIR/scripts/check_core_portability.sh"

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
echo "=== Sanitizers ==="
rm -rf "$SANITIZE_DIR"
cmake   -S "$ROOT_DIR"   -B "$SANITIZE_DIR"   -G Ninja   -DCMAKE_BUILD_TYPE=Debug   -DECU_BUILD_TESTS=ON   -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'   -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'

cmake --build "$SANITIZE_DIR" --target   ecu_core_foundation_tests   ecu_j1939_core_tests   ecu_uds_core_tests

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1   "$SANITIZE_DIR/tests/ecu_core_foundation_tests"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1   "$SANITIZE_DIR/tests/ecu_j1939_core_tests"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1   "$SANITIZE_DIR/tests/ecu_uds_core_tests"

echo
echo "CORE_HARDENING_V1=PASS"
