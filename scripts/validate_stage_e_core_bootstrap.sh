#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/stage-e"

echo "[ECU V2] Stage E — portable Core bootstrap validation"

echo
echo "=== Repository ==="
git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD
git -C "$ROOT_DIR" status --short

echo
echo "=== Portability gate self-test ==="
"$ROOT_DIR/scripts/selftest_core_portability_gate.sh"

echo
echo "=== Portability gate ==="
"$ROOT_DIR/scripts/check_core_portability.sh"

echo
echo "=== Configure ==="
rm -rf "$BUILD_DIR"
cmake \
  -S "$ROOT_DIR" \
  -B "$BUILD_DIR" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DECU_BUILD_TESTS=ON

echo
echo "=== Build ==="
cmake --build "$BUILD_DIR"

echo
echo "=== Tests ==="
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo
echo "=== Direct smoke ==="
"$BUILD_DIR/tests/ecu_core_smoke"

echo
echo "STAGE_E_CORE_BOOTSTRAP=PASS"
