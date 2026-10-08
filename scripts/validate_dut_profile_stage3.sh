#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/dut-profile-stage3-debug"
RELEASE_DIR="$ROOT_DIR/build/dut-profile-stage3-release"
GENERIC_DIR="$ROOT_DIR/build/dut-profile-stage3-generic"
SAN_DIR="$ROOT_DIR/build/dut-profile-stage3-sanitize"

echo "[ECU V2] DUT Profile Stage 3 foundation validation"

echo
echo "=== DUT Profile architecture ==="
python3 "$ROOT_DIR/scripts/check_dut_profile_architecture.py"
python3 "$ROOT_DIR/scripts/check_daf_sac_profile_architecture.py"

configure_profile() {
  local type="$1"
  local dir="$2"
  shift 2

  rm -rf "$dir"
  cmake -S "$ROOT_DIR" -B "$dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$type" \
    -DECU_BUILD_TESTS=ON \
    -DECU_BUILD_LEGACY_CORE=OFF \
    -DECU_BUILD_SAC_MODULE=OFF \
    -DECU_BUILD_LINUX_SOCKETCAN=OFF \
    -DECU_BUILD_CORE_V2=ON \
    -DECU_BUILD_BENCH_RUNTIME=ON \
    -DECU_BUILD_DUT_PROFILE=ON \
    -DECU_BUILD_DAF_SAC_PROFILE=ON \
    "$@"
}

build_and_test_profile() {
  local type="$1"
  local dir="$2"
  shift 2

  configure_profile "$type" "$dir" "$@"
  cmake --build "$dir" --target ecu_dut_profile_tests ecu_daf_sac_profile_tests_target
  ctest --test-dir "$dir" \
    -R '^ecu\.dut_profile\.' \
    --output-on-failure \
    --no-tests=error
}

echo
echo "=== DUT Profile Debug ==="
build_and_test_profile Debug "$DEBUG_DIR"

echo
echo "=== DUT Profile Release ==="
build_and_test_profile Release "$RELEASE_DIR"

echo
echo "=== DUT Profile Generic non-Linux ==="
build_and_test_profile Release "$GENERIC_DIR" -DCMAKE_SYSTEM_NAME=Generic

echo
echo "=== DUT Profile ASAN/UBSAN ==="
configure_profile Debug "$SAN_DIR" \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build "$SAN_DIR" --target ecu_dut_profile_tests ecu_daf_sac_profile_tests_target
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" \
  ctest --test-dir "$SAN_DIR" \
    -R '^ecu\.dut_profile\.' \
    --output-on-failure \
    --no-tests=error

echo
echo "=== Frozen Stage 2 + Core regression ==="
bash "$ROOT_DIR/scripts/validate_bench_runtime_stage2.sh"

echo
echo "DUT_PROFILE_STAGE3_LOCAL_GATE=PASS"
