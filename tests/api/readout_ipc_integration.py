#!/usr/bin/env python3
"""Real process + filesystem HTTP boundary test; strictly synthetic DUT data."""
import grp
import http.client
import json
import os
from pathlib import Path
import pwd
import secrets
import socket
import subprocess
import sys
import tempfile
import time


def require(value, message):
    if not value:
        raise AssertionError(message)


def request(port, token, target, authenticated=True):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        headers = {"Authorization": "Bearer " + token} if authenticated else {}
        conn.request("GET", target, headers=headers)
        response = conn.getresponse()
        return response.status, json.loads(response.read())
    finally:
        conn.close()


def run(server, fixture):
    if os.getuid() == 0:
        raise RuntimeError("run tests as an unprivileged user, not root")
    with tempfile.TemporaryDirectory() as home:
        work = Path(home)
        data_dir = work / "readouts"
        data_dir.mkdir()
        os.chmod(data_dir, 0o2750)
        token_file = work / "auth"
        token = secrets.token_hex(32)
        token_file.write_text(token + "\n", encoding="ascii")
        os.chmod(token_file, 0o600)

        owner = pwd.getpwuid(os.getuid()).pw_name
        group = grp.getgrgid(os.getgid()).gr_name
        with socket.socket() as holder:
            holder.bind(("127.0.0.1", 0))
            port = holder.getsockname()[1]
        cmd = [server, "--token-file", str(token_file), "--port", str(port),
               "--readout-dir", str(data_dir),
               "--readout-owner", owner, "--readout-group", group]
        process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        try:
            for _ in range(60):
                require(process.poll() is None, "API terminated before test")
                try:
                    with socket.create_connection(("127.0.0.1", port), 0.1):
                        break
                except OSError:
                    time.sleep(0.04)
            else:
                raise AssertionError("API did not bind its loopback listener")
            url = "/api/v1/readouts/dtc/latest"
            status, response = request(port, token, url)
            require(status == 503 and
                    response["error"]["code"] == "backend_unavailable",
                    "missing producer fails closed")
            status, _ = request(port, token, url, authenticated=False)
            require(status == 401, "last readout still needs auth")
            require(subprocess.run([fixture, str(data_dir)],
                                   capture_output=True, timeout=4).returncode == 0,
                    "publisher writes owned and group readable artifact")
            file = data_dir / "dtc-latest.v1"
            require((file.stat().st_mode & 0o7777) == 0o640,
                    "published inode is mode 0640")
            status, response = request(port, token, url)
            require(status == 200, f"genuine file-backed API: {status} {response}")
            data = response["data"]
            require(data["source"] == "completed_application_operation" and
                    data["live"] is False and
                    data["completed_generation"] == 12 and
                    data["profile_id"] == 0xDAF00050 and
                    data["dtcs"]["status_availability_mask"] == 139 and
                    data["dtcs"]["requested_status_mask"] == 255 and
                    [x["code"] for x in data["dtcs"]["entries"]] ==
                    ["3A0002", "08F9E2"], "typed readout body from file")
            require(data["captured_at_unix_ms"] > 0, "timestamp present")
            status, _ = request(port, token, "/api/v1/dut/dtcs")
            require(status == 503, "historical readout never becomes live DTC")
            os.chmod(file, 0o666)
            status, _ = request(port, token, url)
            require(status == 502, "world-writable artifact must be refused")
            os.chmod(file, 0o640)
            raw = file.read_text(encoding="ascii")
            file.write_text(raw.replace("entry_count=2\n", "entry_count=5\n"))
            status, _ = request(port, token, url)
            require(status == 502, "corrupt record fails closed")
            file.write_text(raw.replace(
                "captured_at_unix_ms=" + str(data["captured_at_unix_ms"]),
                "captured_at_unix_ms=1"))
            status, response = request(port, token, url)
            require(status == 410 and
                    response["error"]["code"] == "readout_expired",
                    "historical data beyond 24h are expired")
            require(subprocess.run([fixture, str(data_dir)],
                                   capture_output=True, timeout=4).returncode == 0,
                    "publisher replaces existing file atomically")
            file.unlink()
            file.symlink_to("/etc/passwd")
            status, _ = request(port, token, url)
            require(status == 502, "symlink target may not be followed")
            require(subprocess.run([fixture, str(data_dir)],
                                   capture_output=True, timeout=4).returncode == 0,
                    "publisher replaces symlink without following it")
            os.chmod(data_dir, 0o2770)
            status, _ = request(port, token, url)
            require(status == 502, "group writable directory refused")
            os.chmod(data_dir, 0o2750)
            require(file.is_file() and not file.is_symlink(),
                    "atomic publication created a regular file")
            status, _ = request(port, token, url)
            require(status == 200, "valid data restored after rejected cases")
            print("ECU_API_READOUT_IPC=PASS")
        finally:
            process.terminate()
            try:
                process.communicate(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=4)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(2)
    run(sys.argv[1], sys.argv[2])
