#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "[ECU V2] DAF SAC application Stage 4.0 gate"

python3 "$ROOT_DIR/scripts/check_dut_profile_architecture.py"
python3 "$ROOT_DIR/scripts/check_daf_sac_profile_architecture.py"
python3 "$ROOT_DIR/scripts/check_daf_sac_application_architecture.py"
bash -n "$ROOT_DIR/scripts/run_stage4_daf_sac_bench_gate.sh"

common=(
  -DECU_BUILD_TESTS=ON
  -DECU_BUILD_LEGACY_CORE=OFF
  -DECU_BUILD_SAC_MODULE=OFF
  -DECU_BUILD_LINUX_SOCKETCAN=OFF
  -DECU_BUILD_CORE_V2=ON
  -DECU_BUILD_BENCH_RUNTIME=ON
  -DECU_BUILD_DUT_PROFILE=ON
  -DECU_BUILD_DAF_SAC_PROFILE=ON
  -DECU_BUILD_DAF_SAC_APPLICATION=ON
  -DECU_BUILD_LINUX_V2_PLATFORM=OFF
)

test_configuration() {
  local name="$1"
  local type="$2"
  shift 2
  local build_dir="$ROOT_DIR/build/daf-sac-app-$name"
  echo "=== DAF SAC application $name ==="
  cmake -S "$ROOT_DIR" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$type" "${common[@]}" "$@"
  cmake --build "$build_dir" --target \
    ecu_daf_sac_application_tests \
    ecu_daf_sac_profile_tests \
    ecu_dut_profile_tests \
    ecu_bench_tests
  ctest --test-dir "$build_dir" \
    -R '^ecu\.(applications\.daf_sac|dut_profile\.|bench\.)' \
    --output-on-failure --no-tests=error
}

test_configuration stage4-debug Debug
test_configuration stage4-release Release
test_configuration stage4-generic Release -DCMAKE_SYSTEM_NAME=Generic
test_configuration stage4-sanitize Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'

echo "DAF_SAC_APPLICATION_STAGE4_LOCAL_GATE=PASS"
