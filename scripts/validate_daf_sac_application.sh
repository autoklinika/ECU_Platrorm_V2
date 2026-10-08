#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "[ECU V2] DAF SAC application Stage 4.0 + 4.2 gate"

python3 "$ROOT_DIR/scripts/check_dut_profile_architecture.py"
python3 "$ROOT_DIR/scripts/check_daf_sac_profile_architecture.py"
python3 "$ROOT_DIR/scripts/check_daf_sac_application_architecture.py"
bash -n "$ROOT_DIR/scripts/run_stage4_daf_sac_bench_gate.sh"
bash -n "$ROOT_DIR/scripts/run_stage42_daf_sac_read_gate.sh"
bash -n "$ROOT_DIR/scripts/run_stage42_daf_sac_500k_read_gate.sh"
bash -n "$ROOT_DIR/scripts/run_stage42_daf_sac_clear_gate.sh"
bash -n "$ROOT_DIR/scripts/check_daf_sac_clear_evidence.sh"
bash -n "$ROOT_DIR/scripts/run_stage42_daf_sac_controlled_retest.sh"
bash -n "$ROOT_DIR/scripts/install_ecu_bench_agent.sh"
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s "$ROOT_DIR/tests" -p "test_ecu_bench_agent.py" -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s "$ROOT_DIR/tests" -p "test_daf_sac_clear_evidence_guard.py" -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s "$ROOT_DIR/tests" -p "test_daf_sac_controlled_retest.py" -v

if refusal_output="$(bash "$ROOT_DIR/scripts/run_stage42_daf_sac_controlled_retest.sh" </dev/null 2>&1)"; then
  echo "ERROR: unattended controlled retest was not rejected"
  exit 1
fi
case "$refusal_output" in
  *"live local operator TTY"*) echo "DTC_CONTROLLED_RETEST_NONINTERACTIVE_DENIAL=PASS" ;;
  *) echo "ERROR: controlled retest failed for unexpected reason: $refusal_output"; exit 1 ;;
esac

if refusal_output="$(bash "$ROOT_DIR/scripts/run_stage42_daf_sac_500k_read_gate.sh" all </dev/null 2>&1)"; then
  echo "ERROR: unattended SAC 500k physical test was not rejected"
  exit 1
fi
case "$refusal_output" in
  *"interactive-sudo-terminal-required"*) echo "SAC_500K_NONINTERACTIVE_DENIAL=PASS" ;;
  *) echo "ERROR: 500k proof failed for unexpected reason: $refusal_output"; exit 1 ;;
esac

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

if [[ "$(uname -s)" == "Linux" ]]; then
  echo "=== DAF SAC clear operator: Linux-only, no real CAN transmissions ==="
  linux_build="$ROOT_DIR/build/daf-sac-app-linux"
  cmake -S "$ROOT_DIR" -B "$linux_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug "${common[@]}" \
    -DECU_BUILD_LINUX_V2_PLATFORM=ON
  cmake --build "$linux_build" --target \
    ecu_daf_sac_clear_operator_tests \
    ecu_daf_sac_stage42_clear_probe
  ctest --test-dir "$linux_build" \
    -R '^ecu\.sac\.clear_operator$' \
    --output-on-failure --no-tests=error

  if refusal_output="$("$linux_build/tests/ecu_daf_sac_stage42_clear_probe" \
       can0 clear-dtc /tmp </dev/null 2>&1)"; then
    echo "ERROR: noninteractive DTC clear runner was not rejected"
    exit 1
  fi
  case "$refusal_output" in
    *"interactive operator TTY"*) echo "DTC_CLEAR_NONINTERACTIVE_DENIAL=PASS" ;;
    *) echo "ERROR: DTC clear CLI failed for unexpected reason: $refusal_output"; exit 1 ;;
  esac
fi

echo "DAF_SAC_APPLICATION_STAGE4_LOCAL_GATE=PASS"
echo "DAF_SAC_APPLICATION_STAGE42_LOCAL_GATE=PASS"
echo "DAF_SAC_DTC_CLEAR_CLI_SOFTWARE_GATE=PASS"
