#!/usr/bin/env bash
# Unprivileged, reproducible candidate staging for CM5 Application API V1.
# Does not require sudo and does not touch running systemd services or CAN.
set -Eeuo pipefail

if [[ ${EUID} -eq 0 ]]; then
  echo "ECU_API_PREPARE=DENIED run-as-nonroot" >&2
  exit 2
fi
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT"
if [[ "$(git branch --show-current)" != \
      api/v1-readonly-foundation-20261008 ||
      -n "$(git status --porcelain)" ]]; then
  echo "ECU_API_PREPARE=DENIED dirty-or-wrong-branch" >&2
  exit 2
fi
for service in ecu-kiosk.service ecu-webgui-static.service \
               ecu-platform-v2-bench-agent.service; do
  [[ "$(systemctl is-active "$service")" == active ]] || {
    echo "ECU_API_PREPARE=DENIED baseline-$service" >&2
    exit 3
  }
done
if ip -o link show can0 | grep -qE '(<|,)UP(,|>)'; then
  echo "ECU_API_PREPARE=DENIED can0-active" >&2
  exit 3
fi
REVISION="$(git rev-parse HEAD)"
BUILD=build/api-cm5-deploy
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DECU_BUILD_APPLICATION_API=ON -DECU_BUILD_TESTS=ON
nice -n 10 cmake --build "$BUILD" \
  --target ecu_api_http ecu_application_api_tests \
    ecu_api_daf_sac_projection_tests ecu_api_readout_wire_tests \
    ecu_api_readout_fixture --parallel 2
ctest --test-dir "$BUILD" -R '^ecu[.]api[.]' \
  --output-on-failure --no-tests=error
# Build additional operator-only probes in their own output tree.
# Compiling them never opens CAN or modifies the kernel.
PROBE_BUILD=build/api-readout-linux
cmake -S . -B "$PROBE_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \\n  -DECU_BUILD_APPLICATION_API=ON -DECU_BUILD_TESTS=ON\nnice -n 10 cmake --build "$PROBE_BUILD" \\n  --target ecu_daf_sac_core_v2_probe ecu_daf_sac_stage42_read_probe \\n  --parallel 2

BIN="$BUILD/src/api/ecu_api_http"
[[ -x "$BIN" ]] || exit 4
printf '%s\n' "$REVISION" > "$BUILD/api-v1-prepared.commit"
sha256sum "$BIN" | cut -d' ' -f1 > "$BUILD/api-v1-prepared.sha256"
[[ "$(git rev-parse HEAD)" == "$REVISION" && -z "$(git status --porcelain)" ]] || {
  rm -f "$BUILD/api-v1-prepared.commit" "$BUILD/api-v1-prepared.sha256"
  echo "ECU_API_PREPARE=DENIED repository-changed-during-build" >&2
  exit 4
}
echo "ECU_API_PREPARE=PASS commit=$(git rev-parse --short=12 HEAD)"
echo "ECU_API_PREPARE_BINARY_SHA256=$(cat "$BUILD/api-v1-prepared.sha256")"
echo "ECU_API_PREPARE_CAN=UNCHANGED_DOWN"
