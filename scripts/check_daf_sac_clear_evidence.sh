#!/usr/bin/env bash
set -euo pipefail

# Read-only policy check. Must finish BEFORE can0 is enabled.
# Treat an interrupted erase as unresolved even if its process never had
# a chance to append CLEAR_OUTCOME=UNKNOWN. Accept resolution only when
# BOTH positive UDS 0x54 and the post-clear DTC inventory were persisted.
if [[ "$#" -ne 1 || ! -d "${1:-}" || -L "${1:-}" ]]; then
  echo "DTC_CLEAR_EVIDENCE_GUARD=ERROR invalid-directory" >&2
  exit 4
fi

EVIDENCE_DIR="$1"
shopt -s nullglob
for record in "$EVIDENCE_DIR"/sac-dtc-*.txt; do
  if [[ -L "$record" || ! -f "$record" || ! -r "$record" ]]; then
    echo "DTC_CLEAR_BLOCKED=INVALID_PREVIOUS_EVIDENCE" >&2
    echo "Evidence record: $record" >&2
    exit 4
  fi
  if grep -q '^CLEAR_OUTCOME=UNKNOWN' "$record"; then
    echo "DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNKNOWN" >&2
    echo "Unresolved record: $record" >&2
    exit 4
  fi
  if grep -Eq '^(CLEAR_INTENT=|CLEAR_ATTEMPT=)' "$record"; then
    if ! grep -qx 'CLEAR_UDS_54_ACK=YES' "$record" ||
       ! grep -qx 'POST_CLEAR_VERIFICATION=READ_COMPLETED' "$record"; then
      echo "DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNVERIFIED" >&2
      echo "Unresolved record: $record" >&2
      exit 4
    fi
  fi
done
echo "DTC_CLEAR_EVIDENCE_GUARD=PASS"
