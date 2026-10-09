#!/usr/bin/env bash
# Unprivileged regression probe: no real DRM / CAN / kiosk service touched.
# Runs Cage's headless backend under two systemd mount policies.
set -euo pipefail

[[ "$(hostname -s)" == "ecu" ]] || exit 2
[[ "$(id -un)" == "ecu" ]] || exit 2
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
[[ -S "$XDG_RUNTIME_DIR/bus" ]] || { echo "User systemd bus unavailable"; exit 2; }

old_log="$(mktemp)"
new_log="$(mktemp)"
trap 'rm -f "$old_log" "$new_log"' EXIT

probe() {
  local unit="$1"
  local home_prop="$2"
  systemd-run --user --wait --pipe --collect \
    "--unit=$unit" \
    --property=NoNewPrivileges=yes \
    --property=RestrictSUIDSGID=yes \
    --property=ProtectSystem=strict \
    "--property=$home_prop" \
    --property=ReadWritePaths=/run/user \
    --property=PrivateTmp=yes \
    --setenv=WLR_BACKENDS=headless \
    --setenv=WLR_RENDERER=pixman \
    --setenv=WLR_LIBINPUT_NO_DEVICES=1 \
    /usr/bin/timeout 5s /usr/bin/cage -- /usr/bin/true
}

set +e
probe "ecu-wayland-old-policy-$$" "ProtectHome=read-only" >"$old_log" 2>&1
old_code=$?
set -e
if [[ "$old_code" -eq 0 ]] ||
   ! grep -q 'Unable to open Wayland socket' "$old_log" ||
   ! grep -q 'status=6/ABRT' "$old_log"; then
  cat "$old_log" >&2
  echo "ECU_WAYLAND_OLD_POLICY_REPRO=FAIL" >&2
  exit 1
fi
echo "ECU_WAYLAND_OLD_POLICY_REPRO=PASS (expected SIGABRT in headless Cage)"

if ! probe "ecu-wayland-new-policy-$$" "InaccessiblePaths=/home /root" >"$new_log" 2>&1 ||
   ! grep -q 'status=0/SUCCESS' "$new_log"; then
  cat "$new_log" >&2
  echo "ECU_WAYLAND_CORRECTED_POLICY=FAIL" >&2
  exit 1
fi
echo "ECU_WAYLAND_CORRECTED_POLICY=PASS (headless Cage exited normally)"
echo "ECU_WAYLAND_REAL_DRM_AND_TOUCH=NOT_TESTED"
