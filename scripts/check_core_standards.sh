#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOC="$ROOT_DIR/docs/CORE_STANDARDS_TRACEABILITY.md"
REQUIRE_CONFORMANCE=0

if [[ "${1:-}" == "--require-conformance" ]]; then
  REQUIRE_CONFORMANCE=1
elif [[ -n "${1:-}" ]]; then
  echo "Usage: $0 [--require-conformance]" >&2
  exit 2
fi

if [[ ! -f "$DOC" ]]; then
  echo "CORE_STANDARDS_BASELINE_GATE=FAIL"
  echo "Missing: $DOC" >&2
  exit 1
fi

required=(
  "ISO 11898-1:2024"
  "ISO 11898-2:2026"
  "ISO 15765-2:2024"
  "ISO 14229-1:2026"
  "ISO 14229-2:2021"
  "ISO 14229-3:2022"
  "ISO 13400-2:2025"
  "ISO 13400-3:2016"
  "ISO 14229-5:2022"
  "SAE J1939/21_202205"
  "SAE J1939-22_202209"
  "SAE J1939/81_202504"
  "SAE J1939-73_202609"
  "ISO/SAE 21434:2021"
  "ISO 24089:2023"
  "ISO 26262:2018"
  "UN Regulation No. 155"
  "UN Regulation No. 156"
)

for item in "${required[@]}"; do
  if ! grep -Fq "$item" "$DOC"; then
    echo "CORE_STANDARDS_BASELINE_GATE=FAIL"
    echo "Missing standards baseline entry: $item" >&2
    exit 1
  fi
done

if ! grep -Fq "CORE_STANDARDS_BASELINE=PASS" "$DOC"; then
  echo "CORE_STANDARDS_BASELINE_GATE=FAIL"
  echo "Baseline is not marked PASS" >&2
  exit 1
fi

echo "CORE_STANDARDS_BASELINE_GATE=PASS"

if grep -Fq "CORE_STANDARDS_CONFORMANCE=PASS" "$DOC"; then
  echo "CORE_STANDARDS_CONFORMANCE_GATE=PASS"
  exit 0
fi

echo "CORE_STANDARDS_CONFORMANCE_GATE=BLOCKED"
if [[ "$REQUIRE_CONFORMANCE" -eq 1 ]]; then
  echo "Formal standards conformance is not closed." >&2
  exit 1
fi
