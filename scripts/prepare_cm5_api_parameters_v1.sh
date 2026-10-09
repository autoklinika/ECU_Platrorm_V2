#!/usr/bin/env bash
# Nonroot: build/test the exact candidate revision; no CAN interaction.
set -Eeuo pipefail
repo=/home/ecu/ECU_API_PARAMS_V1
[[ "$EUID" -ne 0 && "$(hostname -s)" == ecu ]] || exit 77
[[ "$(git -C "$repo" branch --show-current)" == \
   api/v1-sac-parameters-readout-20261008 ]] || exit 2
[[ -z "$(git -C "$repo" status --porcelain)" ]] || exit 2
ip -details link show can0 | grep -q 'state DOWN' || exit 2
cd "$repo"
cmake -S . -B build/params-linux -G Ninja \
  -DECU_BUILD_APPLICATION_API=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/params-linux --target \
  ecu_api_http ecu_api_readout_fixture \
  ecu_daf_sac_core_v2_probe ecu_daf_sac_stage42_read_probe \
  ecu_api_daf_sac_projection_tests ecu_api_readout_wire_tests \
  ecu_application_api_tests -j 4
ctest --test-dir build/params-linux -R '^ecu[.]api[.]' --output-on-failure
revision="$(git rev-parse --short=12 HEAD)"
binary=build/params-linux/src/api/ecu_api_http
[[ -f "$binary" && -x "$binary" ]] || exit 2
# Embedded revision proves the C++ API HTTP release matches the clean commit.
# Git --short=12 uses at least 12 chars; long histories may require more.
# Never use grep -q on the output of strings under pipefail (SIGPIPE).
strings "$binary" | grep -Fx "$revision" >/dev/null || {
  echo 'SAC_PARAMETER_API_PREPARE=FAIL embedded revision mismatch' >&2
  exit 2
}
digest="$(sha256sum "$binary" | cut -d' ' -f1)"
stamp=build/params-linux/api_parameters_candidate.sha
printf '%s %s\n' "$revision" "$digest" > "$stamp"
chmod 0600 "$stamp"
ip -details link show can0 | grep -q 'state DOWN' || exit 2
echo "SAC_PARAMETER_API_PREPARE=PASS commit=$revision"
echo "SAC_PARAMETER_API_PREPARE_BINARY_SHA256=$digest"
echo "SAC_PARAMETER_API_PREPARE_CAN=UNCHANGED_DOWN"
