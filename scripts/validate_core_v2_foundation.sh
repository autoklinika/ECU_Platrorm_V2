#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/core-v2-debug"
RELEASE_DIR="$ROOT_DIR/build/core-v2-release"
SAN_DIR="$ROOT_DIR/build/core-v2-sanitize"

echo "[ECU V2] Core V2 TRUCK/AGRI/OHV foundation validation"

echo
echo "=== Architecture ==="
bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh"
ECU_CORE_PORTABILITY_ROOT="$ROOT_DIR/src/core_v2" bash "$ROOT_DIR/scripts/check_core_portability.sh"
bash "$ROOT_DIR/scripts/test_core_v2_gates.sh"

assert_isolated_graph() {
  local dir="$1"
  local graph="$dir/core-v2-targets.txt"
  cmake --build "$dir" --target help > "$graph"
  local status=0
  grep -En 'src/core/|src/ecu/sac|src/platform/linux/socketcan|ecu_core:|ecu_sac|ecu_platform_linux_socketcan' \
    "$graph" "$dir/build.ninja" "$dir/CMakeFiles/TargetDirectories.txt" || status=$?
  if ((status != 1)); then
    echo "CORE_V2_ISOLATED_GRAPH=FAIL" >&2
    exit 1
  fi
  echo "CORE_V2_ISOLATED_GRAPH=PASS"
}

build_and_test() {
  local type="$1"
  local dir="$2"
  rm -rf "$dir"
  cmake -S "$ROOT_DIR" -B "$dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$type" \
    -DECU_BUILD_TESTS=ON \
    -DECU_BUILD_LEGACY_CORE=OFF \
    -DECU_BUILD_SAC_MODULE=OFF \
    -DECU_BUILD_LINUX_SOCKETCAN=OFF \
    -DECU_BUILD_CORE_V2=ON
  assert_isolated_graph "$dir"
  cmake --build "$dir" --target ecu_core_v2_foundation_tests
  ctest --test-dir "$dir" -R '^ecu\.core_v2\.' --output-on-failure --no-tests=error
}

echo
echo "=== Debug ==="
build_and_test Debug "$DEBUG_DIR"

echo
echo "=== Release ==="
build_and_test Release "$RELEASE_DIR"

echo
echo "=== Sanitizers ==="
rm -rf "$SAN_DIR"
cmake -S "$ROOT_DIR" -B "$SAN_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DECU_BUILD_TESTS=ON \
  -DECU_BUILD_LEGACY_CORE=OFF \
  -DECU_BUILD_SAC_MODULE=OFF \
  -DECU_BUILD_LINUX_SOCKETCAN=OFF \
  -DECU_BUILD_CORE_V2=ON \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
assert_isolated_graph "$SAN_DIR"
cmake --build "$SAN_DIR" --target ecu_core_v2_foundation_tests
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" \
  ctest --test-dir "$SAN_DIR" -R '^ecu\.core_v2\.' --output-on-failure --no-tests=error

echo
echo "CORE_V2_FOUNDATION=PASS"
