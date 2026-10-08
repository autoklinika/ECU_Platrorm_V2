#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/core-hardening-debug"
RELEASE_DIR="$ROOT_DIR/build/core-hardening-release"
CORE_ONLY_DIR="$ROOT_DIR/build/core-hardening-core-only"
SANITIZE_DIR="$ROOT_DIR/build/core-hardening-sanitize"

echo "[ECU V2] Core Hardening validation"

echo
echo "=== Repository ==="
git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD
git -C "$ROOT_DIR" status --short

echo
echo "=== Standards/conformance gate ==="
bash "$ROOT_DIR/scripts/check_core_standards.sh" --require-conformance

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
  shift 2

  echo
  echo "=== Configure: $build_type ==="
  rm -rf "$build_dir"
  cmake     -S "$ROOT_DIR"     -B "$build_dir"     -G Ninja     -DCMAKE_BUILD_TYPE="$build_type"     -DECU_BUILD_TESTS=ON     -DECU_BUILD_LEGACY_CORE=ON     -DECU_BUILD_SAC_MODULE=ON     -DECU_BUILD_LINUX_SOCKETCAN=ON     -DECU_BUILD_DUT_PROFILE=OFF     -DECU_BUILD_DAF_SAC_PROFILE=OFF     -DECU_BUILD_DAF_SAC_APPLICATION=OFF     -DECU_BUILD_LINUX_V2_PLATFORM=OFF     "$@"

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
echo "=== Core-only graph ==="
build_and_test   Debug   "$CORE_ONLY_DIR"   -DECU_BUILD_SAC_MODULE=OFF   -DECU_BUILD_LINUX_SOCKETCAN=OFF

if [[ -d "$CORE_ONLY_DIR/src/ecu/sac" ]]; then
  echo "CORE_ONLY_GRAPH=FAIL SAC target present"
  exit 1
fi

if [[ -d "$CORE_ONLY_DIR/src/platform/linux/socketcan" ]]; then
  echo "CORE_ONLY_GRAPH=FAIL SocketCAN target present"
  exit 1
fi

echo "CORE_ONLY_GRAPH=PASS"

echo
echo "=== Sanitizers ==="
rm -rf "$SANITIZE_DIR"
cmake   -S "$ROOT_DIR"   -B "$SANITIZE_DIR"   -G Ninja   -DCMAKE_BUILD_TYPE=Debug   -DECU_BUILD_TESTS=ON   -DECU_BUILD_LEGACY_CORE=ON   -DECU_BUILD_DUT_PROFILE=OFF   -DECU_BUILD_DAF_SAC_PROFILE=OFF   -DECU_BUILD_DAF_SAC_APPLICATION=OFF   -DECU_BUILD_LINUX_V2_PLATFORM=OFF   -DECU_BUILD_SAC_MODULE=OFF   -DECU_BUILD_LINUX_SOCKETCAN=OFF   -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'   -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'

cmake --build "$SANITIZE_DIR" --target   ecu_core_foundation_tests   ecu_core_runtime_registry_tests   ecu_core_observability_simulation_tests   ecu_j1939_core_tests   ecu_uds_core_tests

for test in   ecu_core_foundation_tests   ecu_core_runtime_registry_tests   ecu_core_observability_simulation_tests   ecu_j1939_core_tests   ecu_uds_core_tests
do
  ASAN_OPTIONS=detect_leaks=1   UBSAN_OPTIONS=halt_on_error=1     "$SANITIZE_DIR/tests/$test"
done

echo
echo "CORE_HARDENING=PASS"
