from pathlib import Path
import subprocess
import unittest
ROOT = Path(__file__).resolve().parents[1]
DEPLOY = (ROOT / "scripts/deploy_cm5_sac_connect_v1.sh").read_text()
UNIT = (ROOT / "deploy/sac_connect/ecu-sac-connect-v1.service").read_text()
SERVER = (ROOT / "deploy/sac_connect/sac_identify_server.py").read_text()
STATIC = (ROOT / "deploy/webgui/static_server.py").read_text()

class ScopedDeployment(unittest.TestCase):
    def test_privileged_entrypoint_requires_operator_and_exact_build(self):
        for item in ('SUDO_USER', 'interactive-ecu-operator-required',
                     'webgui/sac-connect-identify-20261009', 'ec1e2c3aa04e',
                     'api_parameters_candidate.sha', 'sha256sum', 'state DOWN',
                     'SAC_CONNECT_ROLLBACK=START', 'ecu-sac-connect-rollback',
                     'SAC_CONNECT_DTC=UNCHANGED', 'SAC_CONNECT_CAN=UNCHANGED_DOWN'):
            self.assertIn(item, DEPLOY)
        self.assertNotIn('systemctl restart ecu-api-v1', DEPLOY)
        self.assertNotIn('systemctl restart ecu-platform-v2-bench-agent', DEPLOY)
        self.assertNotIn('ip link set can0 up', DEPLOY)

    def test_service_privilege_boundary(self):
        for item in ('User=root', 'CapabilityBoundingSet=CAP_NET_ADMIN CAP_SETUID CAP_SETGID',
                     'NoNewPrivileges=true', 'ProtectHome=true', 'ProtectSystem=strict',
                     'RestrictAddressFamilies=AF_INET AF_NETLINK AF_CAN',
                     'ReadWritePaths=/var/lib/ecu-platform-v2/api-readouts'):
            self.assertIn(item, UNIT)
        self.assertIn('ThreadingHTTPServer((HOST, PORT), Handler)', SERVER)
        self.assertIn('hmac.compare_digest', SERVER)
        self.assertIn('if is_up()', SERVER)
        self.assertIn('ip("link", "set", "can0", "down")', SERVER)
        self.assertNotIn('shell=True', SERVER)

    def test_static_host_restricts_assets_and_csp(self):
        self.assertIn('"/src/sac-connect-flow.mjs"', STATIC)
        self.assertIn("http.client.HTTPConnection", STATIC)
        self.assertIn("LoadCredential", STATIC)
        self.assertIn("connect-src 'self'", STATIC)
        self.assertIn('def do_POST(self):', STATIC)
        self.assertIn('405', STATIC)

    def test_scoped_identity_hotfix_guard_and_rollback(self):
        hotfix = (ROOT / "scripts/upgrade_cm5_sac_identity_parser.sh").read_text()
        for expected in (
                "interactive-ecu-sudo-required",
                "webgui/sac-local-prototype-no-token-20261009",
                "expected_old_sha=",
                'git -C "$repo" status --porcelain',
                'stat -c %U:%G "$target"',
                'systemctl restart "$service"',
                'SAC_IDENTITY_HOTFIX=PASS parser-start-pass',
                'SAC_IDENTITY_ROLLBACK=PASS',
                'SAC_IDENTITY_HOTFIX_DTC=UNCHANGED',
                'SAC_IDENTITY_HOTFIX_CAN=UNCHANGED_DOWN',
                'SAC_IDENTITY_HOTFIX=REFUSED unexpected-installed-binary'):
            self.assertIn(expected, hotfix)
        for forbidden in ("systemctl restart ecu-api-v1",
                          "systemctl restart ecu-webgui-static",
                          "systemctl restart ecu-kiosk",
                          "cansend", "ip link set can0 up",
                          "clear_dtcs", "SAC_READ_STAGE=dtc"):
            self.assertNotIn(forbidden, hotfix)

    def test_identity_hotfix_http_ready_waits_without_uds(self):
        script = (ROOT / "scripts/upgrade_cm5_sac_identity_parser.sh").read_text()
        start = script.index("wait_for_adapter_unauthorized() {")
        end = script.index("\n\non_exit() {", start)
        wait_fn = script[start:end]
        # Execute the real Bash helper with stubbed HTTP responses. No CAN,
        # sudo or physical probe is used by these regression cases.
        for status, success in (("401", True), ("000", False)):
            with self.subTest(status=status):
                shell = (
                    "set -Eeuo pipefail\n" + wait_fn +
                    "\ncurl() { printf '%s' " + status + "; }\n" +
                    "sleep() { :; }\n" +
                    "wait_for_adapter_unauthorized\n"
                )
                result = subprocess.run(
                    ["bash", "-c", shell], capture_output=True,
                    text=True, timeout=4)
                self.assertEqual(result.returncode == 0, success)
                self.assertIn("SAC_IDENTITY_HTTP_READY=",
                              result.stdout + result.stderr)
        self.assertIn('stage="adapter-http-readiness"', script)
        self.assertIn("SAC_IDENTITY_HOTFIX=FAIL exit=$rc stage=$stage", script)

    def test_identity_hotfix_denies_nonroot(self):
        hotfix = ROOT / "scripts/upgrade_cm5_sac_identity_parser.sh"
        result = subprocess.run(["bash", str(hotfix)], capture_output=True,
                                text=True, timeout=3)
        self.assertEqual(result.returncode, 77)
        self.assertIn("interactive-ecu-sudo-required", result.stdout)

    def test_dual_bitrate_cutover_is_atomic_and_operator_only(self):
        script = (ROOT / "scripts/deploy_cm5_sac_dual_bitrate.sh").read_text()
        for marker in (
            "interactive-ecu-sudo-required",
            "expected_adapter=5e523611979fecf6468eb02c62b19304f408fb6df0f8451834e13848a2e2a8e7",
            "expected_gui=releases/e62d4d1d4d93",
            "state DOWN",
            "SAC_DUAL_BITRATE_DEPLOY=PASS",
            "SAC_DUAL_BITRATE_POLICY=250000_THEN_500000_UDS_ONLY",
            "SAC_DUAL_BITRATE_ROLLBACK=PASS",
            "SAC_DUAL_BITRATE_DTC=UNCHANGED",
            "http-readiness",
            "ecu-sac-connect-v1.service"):
            self.assertIn(marker, script)
        for forbidden in ("systemctl restart ecu-api-v1",
                          "systemctl restart ecu-platform-v2-bench-agent",
                          "cansend", "ip link set can0 up", "SAC_READ_STAGE=dtc"):
            self.assertNotIn(forbidden, script)
        result = subprocess.run(
            ["bash", str(ROOT / "scripts/deploy_cm5_sac_dual_bitrate.sh")],
            capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 77)
        self.assertIn("interactive-ecu-sudo-required", result.stdout)

    def test_nonprivileged_deploy_refused(self):
        result = subprocess.run(['bash', str(ROOT/'scripts/deploy_cm5_sac_connect_v1.sh')],
                                capture_output=True,text=True,timeout=3)
        self.assertEqual(result.returncode,77)
        self.assertIn('interactive-ecu-operator-required',result.stdout)

if __name__ == '__main__': unittest.main()
