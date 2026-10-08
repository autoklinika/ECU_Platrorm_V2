#!/usr/bin/env python3
"""Must pass inside the sandboxed kiosk service before starting Chromium."""
import grp
import os
import pwd
import socket
import sys

errors = []
if os.geteuid() == 0:
    errors.append("running as root")
if os.geteuid() != pwd.getpwnam("ecu-kiosk").pw_uid:
    errors.append("wrong kiosk user")
groups = {grp.getgrgid(gid).gr_name for gid in os.getgroups()}
groups.add(grp.getgrgid(os.getegid()).gr_name)
for group in ("ecu", "sudo", "adm", "dialout", "spi", "i2c", "gpio", "input"):
    if group in groups:
        errors.append("forbidden group: " + group)
if os.access("/home/ecu", os.R_OK | os.X_OK):
    errors.append("operator home accessible")
if os.access("/run/ecu-platform-v2-bench/request.sock", os.R_OK | os.W_OK):
    errors.append("privileged Bench-agent socket accessible")
# Probe the exact AF_UNIX bind that Cage needs for its Wayland socket.
# XDG_RUNTIME_DIR is /run/user/<UID> and must remain writable through
# the service mount namespace; a broken ProtectHome=read-only policy
# would otherwise crash Cage with SIGABRT during cleanup.
runtime = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.geteuid()}")
expected_runtime = f"/run/user/{os.geteuid()}"
if runtime != expected_runtime:
    errors.append("unexpected XDG_RUNTIME_DIR: " + runtime)
else:
    probe_path = os.path.join(runtime, f".ecu-kiosk-preflight-{os.getpid()}.sock")
    try:
        if os.stat(runtime).st_uid != os.geteuid():
            errors.append("runtime directory owned by another user")
        else:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
                probe.bind(probe_path)
    except OSError as exc:
        errors.append("Wayland runtime socket creation denied: " + str(exc))
    finally:
        try:
            os.unlink(probe_path)
        except FileNotFoundError:
            pass
        except OSError as exc:
            errors.append("cannot clean runtime probe: " + str(exc))

try:
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
except (OSError, AttributeError):
    pass
else:
    s.close()
    errors.append("raw SocketCAN socket unexpectedly allowed")
try:
    with open("/proc/self/status", encoding="utf-8") as handle:
        status = handle.read()
    if "NoNewPrivs:\t1" not in status:
        errors.append("NoNewPrivileges is not active")
    for line in status.splitlines():
        if line.startswith("CapEff:") and int(line.split()[1], 16) != 0:
            errors.append("effective Linux capabilities are not empty")
except OSError as exc:
    errors.append("cannot inspect process security: " + str(exc))

if errors:
    print("ECU_KIOSK_SECURITY_PREFLIGHT=FAIL " + " | ".join(errors), file=sys.stderr)
    sys.exit(1)
print("ECU_KIOSK_SECURITY_PREFLIGHT=PASS", flush=True)
