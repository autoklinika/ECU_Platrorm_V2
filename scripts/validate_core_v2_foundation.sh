#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEBUG_DIR="$ROOT_DIR/build/core-v2-debug"
RELEASE_DIR="$ROOT_DIR/build/core-v2-release"
SAN_DIR="$ROOT_DIR/build/core-v2-sanitize"
GENERIC_DIR="$ROOT_DIR/build/core-v2-generic-os"

echo "[ECU V2] Core V2 TRUCK/AGRI/OHV foundation validation"

echo
echo "=== Standards / scoped conformance ==="
bash "$ROOT_DIR/scripts/check_core_standards.sh" --require-conformance

echo
echo "=== Architecture ==="
bash "$ROOT_DIR/scripts/check_core_v2_architecture.sh"
ECU_CORE_PORTABILITY_ROOT="$ROOT_DIR/src/core_v2"   bash "$ROOT_DIR/scripts/check_core_portability.sh"
bash "$ROOT_DIR/scripts/test_core_v2_gates.sh"

assert_isolated_graph() {
  local dir="$1"
  local graph="$dir/core-v2-targets.txt"
  cmake --build "$dir" --target help > "$graph"
  local status=0
  grep -En     'src/core/|src/ecu/sac|src/platform/linux/socketcan|ecu_core:|ecu_sac|ecu_platform_linux_socketcan'     "$graph" "$dir/build.ninja" "$dir/CMakeFiles/TargetDirectories.txt"     || status=$?
  if ((status != 1)); then
    echo "CORE_V2_ISOLATED_GRAPH=FAIL" >&2
    exit 1
  fi
  echo "CORE_V2_ISOLATED_GRAPH=PASS"
}

assert_runtime_independence() {
  local dir="$1"
  local library="$dir/src/core_v2/libecu_core_v2.a"

  if command -v nm >/dev/null 2>&1; then
    local external
    external="$(
      # Toolchains may inject a very small set of freestanding/compiler
      # runtime primitives without any explicit Core source dependency:
      # Clang may lower aggregate copy/zero to memcpy/memset and hardened GCC
      # may emit __stack_chk_fail for stack-protector checks. The architecture
      # scanner forbids explicit source use; only these exact unresolved
      # compiler-generated symbols are accepted here.
      nm -uC "$library" | awk '
        / U / &&
        $0 !~ / U ecu::core::v2::/ &&
        $0 !~ / U (memcpy|memset|__stack_chk_fail)$/ {print}
      '
    )"
    if [[ -n "$external" ]]; then
      echo "CORE_V2_EXTERNAL_RUNTIME_SYMBOLS=FAIL" >&2
      printf '%s
' "$external" >&2
      exit 1
    fi
    echo "CORE_V2_EXTERNAL_RUNTIME_SYMBOLS=PASS"
  fi

  if command -v readelf >/dev/null 2>&1; then
    local object
    while IFS= read -r -d '' object; do
      if readelf -SW "$object"           | grep -Eq '\.(init_array|fini_array|ctors|dtors)'; then
        echo "CORE_V2_DYNAMIC_STATIC_INIT=FAIL: $object" >&2
        exit 1
      fi
    done < <(
      find "$dir/src/core_v2/CMakeFiles/ecu_core_v2.dir"         -type f \( -name '*.o' -o -name '*.obj' \) -print0
    )
    echo "CORE_V2_DYNAMIC_STATIC_INIT=PASS"
  fi
}

configure_core_only() {
  local type="$1"
  local dir="$2"
  shift 2

  rm -rf "$dir"
  cmake -S "$ROOT_DIR" -B "$dir" -G Ninja     -DCMAKE_BUILD_TYPE="$type"     -DECU_BUILD_TESTS=ON     -DECU_BUILD_LEGACY_CORE=OFF     -DECU_BUILD_SAC_MODULE=OFF     -DECU_BUILD_LINUX_SOCKETCAN=OFF     -DECU_BUILD_CORE_V2=ON     "$@"
}

build_and_test() {
  local type="$1"
  local dir="$2"
  shift 2

  configure_core_only "$type" "$dir" "$@"
  assert_isolated_graph "$dir"
  cmake --build "$dir" --target ecu_core_v2_tests
  assert_runtime_independence "$dir"
  ctest --test-dir "$dir" -R '^ecu\.core_v2\.'     --output-on-failure --no-tests=error
}

echo
echo "=== Debug ==="
build_and_test Debug "$DEBUG_DIR"

echo
echo "=== Release ==="
build_and_test Release "$RELEASE_DIR"

echo
echo "=== Generic non-Linux CMake system ==="
build_and_test Release "$GENERIC_DIR" -DCMAKE_SYSTEM_NAME=Generic

echo
echo "=== Sanitizers ==="
configure_core_only Debug "$SAN_DIR"   -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer'   -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined -fno-sanitize=vptr'
assert_isolated_graph "$SAN_DIR"
cmake --build "$SAN_DIR" --target ecu_core_v2_tests
# ASAN/UBSAN intentionally add runtime symbols and initialization hooks.
# vptr sanitizer is excluded because deterministic Core is intentionally built
# with -fno-rtti; vptr instrumentation requires RTTI/typeinfo and is therefore
# not a valid sanitizer configuration for this ABI contract.
# Runtime-independence inspection applies to non-instrumented artifacts above.
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"   ctest --test-dir "$SAN_DIR" -R '^ecu\.core_v2\.'     --output-on-failure --no-tests=error

echo
echo "CORE_V2_FOUNDATION=PASS"
