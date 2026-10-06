#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE="${ECU_CORE_V2_SCAN_ROOT:-$ROOT_DIR/src/core_v2}"
if ! python3 "$ROOT_DIR/scripts/check_core_v2_architecture.py" "$CORE"; then
  echo 'CORE_V2_ARCHITECTURE_GATE=FAIL'
  exit 1
fi
