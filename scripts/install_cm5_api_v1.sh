#!/usr/bin/env bash
# The only privileged transition for standalone CM5 Application API V1.
# Never modifies, stops, or restarts the kiosk, Bench Agent, or CAN interface.
set -Eeuo pipefail

if [[ ${EUID} -ne 0 || ! -t 0 ]]; then
  echo "ECU_API_INSTALL=DENIED interactive-root-terminal-required" >&2
  exit 2
fi

if [[ "${SUDO_USER:-}" != ecu ]]; then
  echo "ECU_API_INSTALL=DENIED expected-operator-ecu" >&2
  exit 2
fi
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
# Inspect the worktree as its authorized non-root owner. Git intentionally
# refuses to trust a user-owned worktree when run as root.
REPO_GIT=(runuser -u ecu -- git -C "$ROOT")
API_BINARY="$ROOT/build/api-cm5-deploy/src/api/ecu_api_http"
PREPARED_REV="$ROOT/build/api-cm5-deploy/api-v1-prepared.commit"
PREPARED_HASH="$ROOT/build/api-cm5-deploy/api-v1-prepared.sha256"
SERVICE_SRC="$ROOT/deploy/api/ecu-api-v1.service"
SERVICE_DST=/etc/systemd/system/ecu-api-v1.service
BINARY_DST=/usr/local/libexec/ecu-platform-v2/ecu_api_http
ROLLBACK_SRC="$ROOT/scripts/rollback_cm5_api_v1.sh"
ROLLBACK_DST=/usr/local/sbin/ecu-api-v1-rollback
SMOKE="$ROOT/scripts/verify_cm5_api_v1.py"
INSTALL_STATE=/var/lib/ecu-platform-v2/api-install-state
MARKER="$INSTALL_STATE/installed-v1"
DATA_PARENT=/var/lib/ecu-platform-v2
DATA_DIR="$DATA_PARENT/api-readouts"
KEY_PARENT=/etc/ecu-platform-v2
KEY_DIR="$KEY_PARENT/api"
KEY_PATH="$KEY_DIR/token"
DEPLOY_STARTED=0

fail() {
  local rc=$?
  trap - ERR EXIT
  echo "ECU_API_INSTALL=FAIL exit_code=$rc" >&2
  if [[ "$DEPLOY_STARTED" == 1 && -f "$MARKER" ]]; then
    bash "$ROLLBACK_DST" --automatic || true
  fi
  exit "$rc"
}
trap fail ERR EXIT

for executable in getent git id install groupadd useradd \
    systemctl systemd-analyze openssl ss ip stat sha256sum runuser \
    python3 curl grep cut chmod chown mktemp mv uname; do
  command -v "$executable" >/dev/null || {
    echo "ECU_API_INSTALL=FAIL missing-tool=$executable" >&2
    exit 3
  }
done

if [[ "$(uname -m)" != aarch64 ]] || ! grep -q '^ID=debian$' /etc/os-release; then
  echo "ECU_API_INSTALL=DENIED not-verified-CM5-Debian" >&2
  exit 3
fi
if [[ "$("${REPO_GIT[@]}" branch --show-current)" != \
        api/v1-readonly-foundation-20261008 ]] ||
   [[ -n "$("${REPO_GIT[@]}" status --porcelain)" ]]; then
  echo "ECU_API_INSTALL=DENIED dirty-or-wrong-branch" >&2
  exit 3
fi
[[ -x "$API_BINARY" && -s "$PREPARED_REV" && -s "$PREPARED_HASH" ]] || {
  echo "ECU_API_INSTALL=DENIED prebuilt-tested-release-missing" >&2
  exit 3
}
REVISION="$("${REPO_GIT[@]}" rev-parse HEAD)"
SHORT_REV="$("${REPO_GIT[@]}" rev-parse --short=12 HEAD)"
if [[ "$(cat "$PREPARED_REV")" != "$REVISION" ]] ||
    [[ "$(sha256sum "$API_BINARY" | cut -d' ' -f1)" != "$(cat "$PREPARED_HASH")" ]]; then
  echo "ECU_API_INSTALL=DENIED prepared-artifact-revision-mismatch" >&2
  exit 3
fi

# Never overwrite existing or externally administered resources.
if [[ -e "$MARKER" || -e "$SERVICE_DST" || -e "$BINARY_DST" ||
      -e "$KEY_PATH" || -L "$SERVICE_DST" || -L "$BINARY_DST" ||
      -L "$DATA_DIR" || -L "$KEY_DIR" ]]; then
  echo "ECU_API_INSTALL=DENIED existing-release-or-unsafe-path" >&2
  exit 3
fi
for service in ecu-kiosk.service ecu-webgui-static.service \
               ecu-platform-v2-bench-agent.service; do
  [[ "$(systemctl is-active "$service")" == active ]] || {
    echo "ECU_API_INSTALL=DENIED baseline-service=$service" >&2
    exit 3
  }
done
[[ "$(curl --silent --output /dev/null --write-out '%{http_code}' \
         --max-time 2 http://127.0.0.1:8877/)" == 200 ]] || {
  echo "ECU_API_INSTALL=DENIED kiosk-webgui-http" >&2
  exit 3
}
if ip -o link show can0 | grep -qE '(<|,)UP(,|>)'; then
  echo "ECU_API_INSTALL=DENIED can0-active" >&2
  exit 3
fi
TX_BEFORE="$(cat /sys/class/net/can0/statistics/tx_packets)"
# Bind test before any identity/filesystem changes.
python3 - <<'PY'
import socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
    try:
        listener.bind(("127.0.0.1", 8878))
    except OSError as exc:
        raise SystemExit("ECU_API_INSTALL=DENIED port-8878-busy: " + str(exc))
print("ECU_API_INSTALL_PORT_8878=AVAILABLE")
PY

if getent passwd ecu-api >/dev/null; then
  echo "ECU_API_INSTALL=DENIED preexisting-ecu-api-account-requires-review" >&2
  exit 3
fi
for group in ecu-api ecu-api-read; do
  if getent group "$group" >/dev/null; then
    echo "ECU_API_INSTALL=DENIED preexisting-group-$group-requires-review" >&2
    exit 3
  fi
done
for path in "$DATA_PARENT" "$DATA_DIR" "$KEY_PARENT" "$KEY_DIR" "$INSTALL_STATE"; do
  [[ ! -e "$path" && ! -L "$path" ]] || {
    echo "ECU_API_INSTALL=DENIED path-already-exists=$path" >&2
    exit 3
  }
done
echo "ECU_API_INSTALL_PREFLIGHT=PASS commit=$SHORT_REV"
umask 077

groupadd --system ecu-api
groupadd --system ecu-api-read
useradd --system --gid ecu-api --groups ecu-api-read \
  --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin ecu-api

API_GROUPS="$(id -nG ecu-api)"
[[ " $API_GROUPS " == *" ecu-api "* &&
   " $API_GROUPS " == *" ecu-api-read "* ]] || exit 5
for assigned_group in $API_GROUPS; do
  case "$assigned_group" in
    ecu-api|ecu-api-read) ;;
    *) echo "ECU_API_INSTALL=FAIL forbidden-group=$assigned_group" >&2; exit 5 ;;
  esac
done
[[ "$(id -u ecu-api)" != 0 ]] || exit 5

install -d -o root -g root -m 0755 "$DATA_PARENT"
install -d -o ecu -g ecu-api-read -m 2750 "$DATA_DIR"
chmod 2750 "$DATA_DIR"
install -d -o root -g root -m 0755 "$KEY_PARENT"
install -d -o root -g ecu-api -m 0750 "$KEY_DIR"
install -d -o root -g root -m 0700 "$INSTALL_STATE"
[[ "$(stat -c '%a %U:%G' "$DATA_DIR")" == '2750 ecu:ecu-api-read' &&
   "$(stat -c '%a %U:%G' "$KEY_DIR")" == '750 root:ecu-api' ]] || {
  echo "ECU_API_INSTALL=FAIL readout-or-credential-directory-modes" >&2
  exit 5
}

# CSPRNG-generated token. Never log it or store it in Git/JS/HTML.
TOKEN_TMP="$(mktemp "$KEY_DIR/.token.XXXXXXXX")"
openssl rand -hex 32 > "$TOKEN_TMP"
chown root:ecu-api "$TOKEN_TMP"
chmod 0640 "$TOKEN_TMP"
mv -T -- "$TOKEN_TMP" "$KEY_PATH"

if [[ -e /usr/local/libexec/ecu-platform-v2 ]]; then
  [[ ! -L /usr/local/libexec/ecu-platform-v2 &&
     "$(stat -c '%a %U:%G' /usr/local/libexec/ecu-platform-v2)" ==         '755 root:root' ]] || {
    echo "ECU_API_INSTALL=DENIED shared-libexec-directory-policy" >&2
    exit 5
  }
else
  install -d -o root -g root -m 0755 /usr/local/libexec/ecu-platform-v2
fi
# Prepare recovery before modifying the active systemd configuration.
install -o root -g root -m 0755 "$ROLLBACK_SRC" "$ROLLBACK_DST"
printf '%s\n' "$REVISION" > "$MARKER"
chmod 0600 "$MARKER"
DEPLOY_STARTED=1
install -o root -g root -m 0755 "$API_BINARY" "$BINARY_DST"
install -o root -g root -m 0644 "$SERVICE_SRC" "$SERVICE_DST"

systemctl daemon-reload
systemd-analyze verify "$SERVICE_DST"
systemctl start ecu-api-v1.service

python3 -I "$SMOKE" --expected-revision "$SHORT_REV"

# Enable only once the independent smoke has passed.
systemctl enable ecu-api-v1.service
[[ "$(systemctl is-active ecu-api-v1.service)" == active ]]
[[ "$(cat /sys/class/net/can0/statistics/tx_packets)" == "$TX_BEFORE" ]]
if ip -o link show can0 | grep -qE '(<|,)UP(,|>)'; then
  echo "ECU_API_INSTALL=FAIL unexpected-CAN-link-change" >&2
  exit 5
fi
trap - ERR EXIT
echo "ECU_API_INSTALL=PASS commit=$SHORT_REV"
echo "ECU_API_ABOUT_URL=http://127.0.0.1:8878/api/v1/about"
echo "ECU_API_ROLLBACK=sudo /usr/local/sbin/ecu-api-v1-rollback"
echo "ECU_API_CAN_UNCHANGED=PASS"
