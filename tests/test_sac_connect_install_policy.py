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
        self.assertIn("http://127.0.0.1:8879", STATIC)
        self.assertIn("connect-src 'self'", STATIC)
        self.assertIn('def do_POST(self):', STATIC)
        self.assertIn('405', STATIC)

    def test_nonprivileged_deploy_refused(self):
        result = subprocess.run(['bash', str(ROOT/'scripts/deploy_cm5_sac_connect_v1.sh')],
                                capture_output=True,text=True,timeout=3)
        self.assertEqual(result.returncode,77)
        self.assertIn('interactive-ecu-operator-required',result.stdout)

if __name__ == '__main__': unittest.main()
