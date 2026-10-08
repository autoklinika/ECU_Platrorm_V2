#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/bench-stage2-debug"
RELEASE_DIR="$ROOT_DIR/build/bench-stage2-release"
GENERIC_DIR="$ROOT_DIR/build/bench-stage2-generic"
SAN_DIR="$ROOT_DIR/build/bench-stage2-sanitize"

echo "[ECU V2] Bench Runtime Stage 2 acceptance validation"

echo
echo "=== Bench architecture ==="
python3 "$ROOT_DIR/scripts/check_bench_architecture.py"

configure_bench() {
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
    "$@"
}

build_and_test_bench() {
  local type="$1"
  local dir="$2"
  shift 2

  configure_bench "$type" "$dir" "$@"
  cmake --build "$dir" --target ecu_bench_tests
  ctest --test-dir "$dir" \
    -R '^ecu\.bench\.' \
    --output-on-failure \
    --no-tests=error
}

echo
echo "=== Bench Debug ==="
build_and_test_bench Debug "$DEBUG_DIR"

echo
echo "=== Bench Release ==="
build_and_test_bench Release "$RELEASE_DIR"

echo
echo "=== Bench Generic non-Linux ==="
build_and_test_bench Release "$GENERIC_DIR" -DCMAKE_SYSTEM_NAME=Generic

echo
echo "=== Bench ASAN/UBSAN ==="
configure_bench Debug "$SAN_DIR" \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build "$SAN_DIR" --target ecu_bench_tests
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" \
  ctest --test-dir "$SAN_DIR" \
    -R '^ecu\.bench\.' \
    --output-on-failure \
    --no-tests=error

echo
echo "=== Frozen Core V2 regression ==="
bash "$ROOT_DIR/scripts/validate_core_v2_foundation.sh"

echo
echo "BENCH_RUNTIME_STAGE2_LOCAL_GATE=PASS"
