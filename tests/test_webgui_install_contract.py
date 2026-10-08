"""Cross-platform, no-root tests for the CM5 WebGUI deployment contract."""
from contextlib import contextmanager
from http.server import ThreadingHTTPServer
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Thread
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import importlib.util
import unittest

BASE = Path(__file__).resolve().parents[1]
DEPLOY = BASE / "deploy" / "webgui"


class SecurityContractTest(unittest.TestCase):
    def test_kiosk_is_not_an_operator_or_hardware_user(self):
        text = (DEPLOY / "ecu-kiosk-v1.service").read_text()
        self.assertIn("\nUser=ecu-kiosk\n", text)
        self.assertIn("\nGroup=ecu-kiosk\n", text)
        self.assertIn("\nNoNewPrivileges=yes\n", text)
        self.assertIn("\nCapabilityBoundingSet=\n", text)
        self.assertIn("\nExecStartPre=/usr/bin/python3 -I ", text)
        self.assertIn("\nSupplementaryGroups=video render\n", text)
        self.assertIn("\nProtectSystem=strict\n", text)
        self.assertIn("\nInaccessiblePaths=/home /root\n", text)
        self.assertIn("\nReadWritePaths=/var/lib/ecu-kiosk /run/user\n", text)
        self.assertNotIn("\nProtectHome=read-only\n", text)
        self.assertIn("\nStandardError=journal\n", text)
        self.assertIn("\nStartLimitBurst=2\n", text)
        self.assertIn("\nRestart=on-failure\n", text)
        self.assertIn("\nIPAddressDeny=any\n", text)
        self.assertIn("\nIPAddressAllow=localhost\n", text)
        allowed = next(x for x in text.splitlines()
                       if x.startswith("RestrictAddressFamilies=")).split("=", 1)[1]
        self.assertNotIn("AF_CAN", allowed)
        self.assertNotIn("AF_PACKET", allowed)
        for forbidden in ("Group=ecu\n", "SupplementaryGroups=input",
                          "SupplementaryGroups=sudo", "--no-sandbox"):
            self.assertNotIn(forbidden, text)

    def test_kiosk_uses_a_hardened_browser_and_local_url(self):
        launcher = (DEPLOY / "ecu-kiosk-v1-launcher").read_text()
        self.assertIn("/usr/bin/cage -- /usr/bin/chromium", launcher)
        self.assertIn("--ozone-platform=wayland", launcher)
        self.assertIn("http://127.0.0.1:8877/", launcher)
        self.assertNotIn("--no-sandbox", launcher)
        self.assertNotIn("--disable-web-security", launcher)
        self.assertNotIn("curl ", launcher)

    def test_dedicated_static_server_has_no_device_or_backend_authority(self):
        text = (DEPLOY / "ecu-webgui-static.service").read_text()
        for expected in ("DynamicUser=yes", "PrivateDevices=yes",
                         "ProtectHome=yes", "ProtectSystem=strict",
                         "NoNewPrivileges=yes", "IPAddressDeny=any",
                         "IPAddressAllow=localhost"):
            self.assertIn(expected, text)
        self.assertNotIn("/run/ecu-platform-v2-bench", text)

    def test_wayland_runtime_socket_preflight_is_required(self):
        script = (DEPLOY / "ecu-kiosk-security-preflight.py").read_text()
        self.assertIn("XDG_RUNTIME_DIR", script)
        self.assertIn("os.stat(runtime).st_uid", script)
        self.assertIn("probe.bind(probe_path)", script)
        self.assertIn("Wayland runtime socket creation denied", script)
        self.assertIn("socket.AF_CAN", script)

    def test_touch_is_targeted_to_one_usb_product(self):
        text = (DEPLOY / "91-ecu-kiosk-touch.rules").read_text()
        for expected in ('SUBSYSTEM=="input"', 'KERNEL=="event*"',
                         'ATTRS{idVendor}=="0712"', 'ATTRS{idProduct}=="0009"',
                         'GROUP:="ecu-kiosk"', 'MODE:="0660"'):
            self.assertIn(expected, text)
        self.assertNotIn('GROUP="input"', text)

    def test_headless_regression_probe_cannot_change_physical_kiosk(self):
        probe = (BASE / "scripts/probe_cm5_wayland_sandbox.sh").read_text()
        self.assertIn("--setenv=WLR_BACKENDS=headless", probe)
        self.assertIn("--setenv=WLR_RENDERER=pixman", probe)
        self.assertIn("ProtectHome=read-only", probe)
        self.assertIn("InaccessiblePaths=/home /root", probe)
        self.assertIn("status=6/ABRT", probe)
        self.assertIn("status=0/SUCCESS", probe)
        self.assertNotIn("sudo ", probe)
        self.assertNotIn("systemctl restart", probe)
        self.assertNotIn("can0", probe)

    def test_pam_session_journal_is_not_a_security_gate(self):
        install = (BASE / "scripts" / "install_cm5_webgui_v1.sh").read_text()
        unit = (DEPLOY / "ecu-kiosk-v1.service").read_text()
        self.assertIn("\nExecStartPre=/usr/bin/python3 -I ", unit)
        self.assertNotIn("ECU_WEBGUI_SECURITY_PREFLIGHT_LOG=FAIL", install)
        self.assertNotIn("journalctl -u ecu-kiosk.service --no-pager -n 80 | grep", install)
        self.assertIn("ECU_WEBGUI_BENCH_AGENT_ISOLATION=FAIL", install)
        self.assertIn("ECU_WEBGUI_PROJECT_ISOLATION=FAIL", install)
        self.assertIn("systemctl is-active --quiet ecu-kiosk.service", install)
        self.assertIn('pgrep -u ecu-kiosk -x chromium', install)

    def test_cursor_patch_is_a_css_only_kiosk_release(self):
        script = (BASE / "scripts/deploy_cursor_css_cm5.sh").read_text()
        self.assertIn('cursor: none !important;', script)
        self.assertIn('chmod 0755 "$staged"', script)
        self.assertIn('mv -Tf "$base/.next-cursor-$$" "$current"', script)
        self.assertIn("CURSOR_UPDATE=ROLLED_BACK", script)
        self.assertIn("systemctl restart ecu-kiosk.service", script)
        self.assertNotIn("systemctl restart ecu-webgui-static.service", script)
        self.assertNotIn("systemctl restart ecu-platform-v2-bench", script)

    def test_recovery_is_defined_before_restart(self):
        install = (BASE / "scripts" / "install_cm5_webgui_v1.sh").read_text()
        rollback = (DEPLOY / "rollback_kiosk.sh").read_text()
        self.assertIn("trap on_exit EXIT", install)
        self.assertIn("ECU_WEBGUI_PARTIAL_CUTOVER=DETECTED", install)
        self.assertIn("ECU_WEBGUI_PARTIAL_CUTOVER=RECOVERED", install)
        self.assertIn("ECU_WEBGUI_AUTO_ROLLBACK=START", install)
        self.assertLess(install.index("rollback_armed=1"),
                        install.index("systemctl restart ecu-kiosk.service"))
        for restored in ("ecu-kiosk.service", "ecu-kiosk.default",
                         "91-ecu-kiosk-touch.rules", "systemctl restart ecu-kiosk.service"):
            self.assertIn(restored, rollback)
        self.assertIn("chgrp input", rollback)
        self.assertNotIn("systemctl restart ecu-platform", install)
        self.assertNotIn("git push", install)


@contextmanager
def static_server():
    # Test only the pure HTTP handler. No privileged directories or sockets.
    spec = importlib.util.spec_from_file_location("webgui_static_host",
                                                   DEPLOY / "static_server.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with TemporaryDirectory() as temporary:
        module.ROOT = Path(temporary)
        (module.ROOT / "index.html").write_text("<title>Ecu Bench Platform</title>")
        (module.ROOT / "styles.css").write_text("body { color: black; }")
        server = ThreadingHTTPServer(("127.0.0.1", 0), module.Handler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield f"http://127.0.0.1:{server.server_port}"
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


class StaticHttpContractTest(unittest.TestCase):
    def test_home_and_css_are_local_read_only_and_hardened(self):
        with static_server() as base:
            with urlopen(base + "/", timeout=3) as response:
                self.assertEqual(response.status, 200)
                self.assertIn(b"Ecu Bench Platform", response.read())
                self.assertIn("frame-ancestors 'none'",
                              response.headers["Content-Security-Policy"])
                self.assertEqual(response.headers["X-Content-Type-Options"],
                                 "nosniff")
                self.assertEqual(response.headers["Cache-Control"], "no-store")
            with urlopen(Request(base + "/styles.css", method="HEAD"), timeout=3) as response:
                self.assertEqual(response.status, 200)
                self.assertTrue(response.headers["Content-Type"].startswith("text/css"))
            with self.assertRaises(HTTPError) as blocked:
                urlopen(Request(base + "/", method="POST", data=b"command"), timeout=3)
            self.assertEqual(blocked.exception.code, 405)
            with self.assertRaises(HTTPError) as blocked:
                urlopen(base + "/private-settings", timeout=3)
            self.assertEqual(blocked.exception.code, 404)
            with self.assertRaises(HTTPError) as blocked:
                urlopen(base + "/src/", timeout=3)
            self.assertEqual(blocked.exception.code, 404)


if __name__ == "__main__":
    unittest.main()
