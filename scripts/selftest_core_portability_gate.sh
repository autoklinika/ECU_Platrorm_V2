#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECKER="$ROOT_DIR/scripts/check_core_portability.sh"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/good" "$TMP_DIR/bad"

cat >"$TMP_DIR/good/good.cpp" <<'EOF'
#include <cstdint>
std::uint32_t portable_value() { return 42U; }
EOF

GOOD_OUTPUT="$(ECU_CORE_PORTABILITY_ROOT="$TMP_DIR/good" "$CHECKER" 2>&1)"
printf '%s\n' "$GOOD_OUTPUT"

if ! grep -q 'CORE_PORTABILITY_GATE=PASS' <<<"$GOOD_OUTPUT"; then
  echo "CORE_PORTABILITY_SELFTEST=FAIL_GOOD_FIXTURE"
  exit 1
fi

cat >"$TMP_DIR/bad/bad.cpp" <<'EOF'
#include <linux/can.h>
int forbidden_linux_dependency() { return 0; }
EOF

set +e
BAD_OUTPUT="$(ECU_CORE_PORTABILITY_ROOT="$TMP_DIR/bad" "$CHECKER" 2>&1)"
BAD_RC=$?
set -e
printf '%s\n' "$BAD_OUTPUT"

if [[ "$BAD_RC" -eq 0 ]]; then
  echo "CORE_PORTABILITY_SELFTEST=FAIL_BAD_FIXTURE_ACCEPTED"
  exit 1
fi

if ! grep -q 'CORE_PORTABILITY_GATE=FAIL' <<<"$BAD_OUTPUT"; then
  echo "CORE_PORTABILITY_SELFTEST=FAIL_BAD_FIXTURE_MARKER"
  exit 1
fi

echo "CORE_PORTABILITY_SELFTEST=PASS"
