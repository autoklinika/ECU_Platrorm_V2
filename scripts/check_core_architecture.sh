#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_DIR="$ROOT_DIR/src/core"
FAIL=0

fail_match() {
  local description="$1"
  local pattern="$2"
  shift 2

  if grep -RInE --include='*.h' --include='*.hpp' --include='*.cc' --include='*.cpp'       "$pattern" "$@"; then
    echo "FAIL: $description"
    FAIL=1
  fi
}

fail_match   "UDS must depend on IDiagnosticTransport, not ISO-TP"   'protocol/isotp|IsoTp'   "$CORE_DIR/include/ecu/core/protocol/uds"   "$CORE_DIR/src/uds_client.cpp"   "$CORE_DIR/src/uds_services.cpp"   "$CORE_DIR/src/uds_types.cpp"

fail_match   "Core must not import ECU-specific modules"   '#[[:space:]]*include[[:space:]]*[<"]ecu/(sac|ecu)/'   "$CORE_DIR"

fail_match   "Core must not import platform adapters"   '#[[:space:]]*include[[:space:]]*[<"]ecu/platform/'   "$CORE_DIR"

fail_match   "Core must not own worker threads or sleeps"   '#[[:space:]]*include[[:space:]]*<thread>|std::this_thread|sleep_for|sleep_until'   "$CORE_DIR"

fail_match   "Core must use injected monotonic time instead of wall clock"   'system_clock|high_resolution_clock'   "$CORE_DIR"

fail_match   "Core must not own filesystem implementation"   '#[[:space:]]*include[[:space:]]*<filesystem>|std::filesystem'   "$CORE_DIR"

if ! grep -q 'IDiagnosticTransport'     "$CORE_DIR/include/ecu/core/protocol/uds/uds_client.hpp"; then
  echo "FAIL: UdsClient does not expose IDiagnosticTransport boundary"
  FAIL=1
fi

if [[ "$FAIL" -ne 0 ]]; then
  echo "CORE_ARCHITECTURE_GATE=FAIL"
  exit 1
fi

echo "CORE_ARCHITECTURE_GATE=PASS"
