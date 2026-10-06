#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_DIR="$ROOT_DIR/src/core"

if [[ ! -d "$CORE_DIR" ]]; then
  echo "FAIL: missing Core directory: $CORE_DIR"
  exit 1
fi

mapfile -d '' CORE_FILES < <(
  find "$CORE_DIR" -type f \( \
    -name '*.h' -o -name '*.hpp' -o -name '*.hh' -o \
    -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
  \) -print0
)

if [[ "${#CORE_FILES[@]}" -eq 0 ]]; then
  echo "FAIL: no Core source files found"
  exit 1
fi

forbidden_patterns=(
  '#[[:space:]]*include[[:space:]]*[<"][[:space:]]*linux/'
  '#[[:space:]]*include[[:space:]]*[<"][[:space:]]*sys/socket\.h'
  '#[[:space:]]*include[[:space:]]*[<"][[:space:]]*sys/ioctl\.h'
  '#[[:space:]]*include[[:space:]]*[<"][[:space:]]*net/if\.h'
  '#[[:space:]]*include[[:space:]]*[<"][[:space:]]*libudev\.h'
  '#[[:space:]]*include[[:space:]]*[<"][[:space:]]*systemd/'
  'socketcan'
  'wiringpi'
  'bcm2835'
  'gpiochip'
  'spidev'
  '/dev/'
  '/sys/'
  '/proc/'
  'systemctl'
  'ip[[:space:]]+link'
)

FAIL=0

for pattern in "${forbidden_patterns[@]}"; do
  if grep -Ein -- "$pattern" "${CORE_FILES[@]}"; then
    echo "FAIL: forbidden OS/hardware dependency matched pattern: $pattern"
    FAIL=1
  fi
done

if [[ "$FAIL" -ne 0 ]]; then
  echo "CORE_PORTABILITY_GATE=FAIL"
  exit 1
fi

echo "Checked ${#CORE_FILES[@]} Core source files."
echo "CORE_PORTABILITY_GATE=PASS"
