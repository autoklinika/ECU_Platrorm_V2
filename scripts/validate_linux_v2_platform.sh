#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "[ECU V2] Linux Core V2 platform validation"

python3 scripts/check_linux_v2_platform_architecture.py

configure_and_test() {
  local name="$1"
  local build_type="$2"
  local cxx="${3:-}"
  local dir="build/linux-v2-platform-${name}"

  rm -rf "$dir"
  if [[ -n "$cxx" ]]; then
    CXX="$cxx" cmake -S . -B "$dir" -G Ninja \
      -DCMAKE_BUILD_TYPE="$build_type" \
      -DECU_BUILD_TESTS=ON \
      -DECU_BUILD_LEGACY_CORE=OFF \
      -DECU_BUILD_SAC_MODULE=OFF \
      -DECU_BUILD_LINUX_SOCKETCAN=OFF \
      -DECU_BUILD_CORE_V2=ON \
      -DECU_BUILD_BENCH_RUNTIME=OFF \
      -DECU_BUILD_DUT_PROFILE=OFF \
      -DECU_BUILD_DAF_SAC_PROFILE=OFF \
      -DECU_BUILD_LINUX_V2_PLATFORM=ON
  else
    cmake -S . -B "$dir" -G Ninja \
      -DCMAKE_BUILD_TYPE="$build_type" \
      -DECU_BUILD_TESTS=ON \
      -DECU_BUILD_LEGACY_CORE=OFF \
      -DECU_BUILD_SAC_MODULE=OFF \
      -DECU_BUILD_LINUX_SOCKETCAN=OFF \
      -DECU_BUILD_CORE_V2=ON \
      -DECU_BUILD_BENCH_RUNTIME=OFF \
      -DECU_BUILD_DUT_PROFILE=OFF \
      -DECU_BUILD_DAF_SAC_PROFILE=OFF \
      -DECU_BUILD_LINUX_V2_PLATFORM=ON
  fi

  cmake --build "$dir" --target ecu_linux_v2_platform_tests_target
  ctest --test-dir "$dir" \
    -R '^ecu\.platform\.linux_v2$' \
    --output-on-failure \
    --no-tests=error
}

configure_and_test gcc-debug Debug
configure_and_test gcc-release Release

if command -v clang++ >/dev/null 2>&1; then
  configure_and_test clang-debug Debug clang++
fi

SAN_DIR="build/linux-v2-platform-sanitize"
rm -rf "$SAN_DIR"
cmake -S . -B "$SAN_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DECU_BUILD_TESTS=ON \
  -DECU_BUILD_LEGACY_CORE=OFF \
  -DECU_BUILD_SAC_MODULE=OFF \
  -DECU_BUILD_LINUX_SOCKETCAN=OFF \
  -DECU_BUILD_CORE_V2=ON \
  -DECU_BUILD_BENCH_RUNTIME=OFF \
  -DECU_BUILD_DUT_PROFILE=OFF \
  -DECU_BUILD_DAF_SAC_PROFILE=OFF \
  -DECU_BUILD_LINUX_V2_PLATFORM=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build "$SAN_DIR" --target ecu_linux_v2_platform_tests_target
ctest --test-dir "$SAN_DIR" \
  -R '^ecu\.platform\.linux_v2$' \
  --output-on-failure \
  --no-tests=error

echo "LINUX_V2_PLATFORM_LOCAL_GATE=PASS"
