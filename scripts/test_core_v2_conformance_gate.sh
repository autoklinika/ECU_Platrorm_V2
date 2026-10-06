#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECKER="$ROOT_DIR/scripts/check_core_v2_conformance.py"
SOURCE="$ROOT_DIR/docs/CORE_V2_FOUNDATION_CONFORMANCE.json"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

python3 "$CHECKER" >/dev/null

expect_fail() {
  local fixture="$1"
  if ECU_CORE_V2_CONFORMANCE_MANIFEST="$fixture" python3 "$CHECKER" >/dev/null 2>&1; then
    echo "CORE_V2_CONFORMANCE_GATE_NEGATIVE_TESTS=FAIL: accepted $fixture" >&2
    exit 1
  fi
}

python3 - "$SOURCE" "$TMP_DIR/passenger.json" <<'PY'
import json, sys
src, dst = sys.argv[1:3]
data = json.load(open(src, encoding="utf-8"))
data["scope"]["domains"].append("passenger")
json.dump(data, open(dst, "w", encoding="utf-8"), indent=2)
PY
expect_fail "$TMP_DIR/passenger.json"

python3 - "$SOURCE" "$TMP_DIR/certified.json" <<'PY'
import json, sys
src, dst = sys.argv[1:3]
data = json.load(open(src, encoding="utf-8"))
data["certification_claimed"] = True
json.dump(data, open(dst, "w", encoding="utf-8"), indent=2)
PY
expect_fail "$TMP_DIR/certified.json"

python3 - "$SOURCE" "$TMP_DIR/no-test.json" <<'PY'
import json, sys
src, dst = sys.argv[1:3]
data = json.load(open(src, encoding="utf-8"))
for req in data["requirements"]:
    if req["claim_type"] == "implemented":
        req["test_refs"] = []
        break
json.dump(data, open(dst, "w", encoding="utf-8"), indent=2)
PY
expect_fail "$TMP_DIR/no-test.json"

python3 - "$SOURCE" "$TMP_DIR/missing-exclusion.json" <<'PY'
import json, sys
src, dst = sys.argv[1:3]
data = json.load(open(src, encoding="utf-8"))
data["scope"]["excluded_and_module_gated"] = data["scope"]["excluded_and_module_gated"][1:]
json.dump(data, open(dst, "w", encoding="utf-8"), indent=2)
PY
expect_fail "$TMP_DIR/missing-exclusion.json"

python3 - "$SOURCE" "$TMP_DIR/missing-marker.json" <<'PY'
import json, sys
src, dst = sys.argv[1:3]
data = json.load(open(src, encoding="utf-8"))
for req in data["requirements"]:
    if req["claim_type"] == "implemented":
        req["code_refs"][0]["markers"].append("__THIS_MARKER_MUST_NOT_EXIST__")
        break
json.dump(data, open(dst, "w", encoding="utf-8"), indent=2)
PY
expect_fail "$TMP_DIR/missing-marker.json"

echo "CORE_V2_CONFORMANCE_GATE_NEGATIVE_TESTS=PASS"
