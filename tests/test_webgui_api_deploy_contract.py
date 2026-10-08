"""Static WebGUI API-only deployment security contracts."""
from pathlib import Path
import importlib.util
import sys
import subprocess
import unittest

BASE = Path(__file__).resolve().parents[1]
SCRIPT = BASE / "scripts" / "deploy_cm5_webgui_api_v1.sh"
SERVER = BASE / "deploy" / "webgui" / "static_server.py"
HTML = BASE / "webgui" / "index.html"


class ApiClientDeploymentContract(unittest.TestCase):
    def test_api_module_is_the_only_added_static_resource(self):
        spec = importlib.util.spec_from_file_location("ecu_static_api_v1", SERVER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertIn("/src/api-client.mjs", module.FILES)
        self.assertEqual(module.FILES["/src/api-client.mjs"], "src/api-client.mjs")
        self.assertIn("connect-src 'self' http://127.0.0.1:8878", module.CSP)
        self.assertNotIn("0.0.0.0", module.CSP)
        self.assertIn("frame-ancestors 'none'", module.CSP)
        self.assertNotIn("/run/", repr(module.FILES))

    def test_html_and_http_csp_do_not_expand_to_arbitrary_origins(self):
        html = HTML.read_text()
        server = SERVER.read_text()
        for text in (html, server):
            self.assertIn("http://127.0.0.1:8878", text)
            self.assertNotIn("connect-src *", text)
            self.assertNotIn("https://", text)
        self.assertNotIn("Authorization: Bearer ", html)
        self.assertIn('id="api-token" type="password"', html)
        self.assertIn('autocomplete="off"', html)

    def test_operator_gate_scope_and_recovery(self):
        text = SCRIPT.read_text()
        self.assertIn('[[ "$EUID" -eq 0 && -t 0 ]]', text)
        self.assertIn("webgui/api-v1-readonly-client-20261008", text)
        self.assertIn('git -C "$repo" status --porcelain', text)
        self.assertIn("ecu-api-v1.service", text)
        self.assertIn("ecu-platform-v2-bench-agent.service", text)
        self.assertIn("state DOWN", text)
        self.assertIn("trap rollback EXIT", text)
        self.assertIn("WEBGUI_API_CLIENT_ROLLBACK=START", text)
        self.assertIn("mv -Tf", text)
        self.assertIn("ecu-webgui-api-v1-rollback", text)
        for prohibited in ("systemctl restart ecu-api-v1",
                           "systemctl restart ecu-platform-v2-bench-agent",
                           "ip link set", "cansend", "sudoers",
                           "curl -H 'Authorization"):
            self.assertNotIn(prohibited, text)

    @unittest.skipUnless(sys.platform.startswith('linux'),
                         'CM5 root/TTY runtime gate is Linux-only')
    def test_deployer_denies_an_unauthorized_noninteractive_invocation(self):
        result = subprocess.run(["bash", str(SCRIPT)], input="",
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 77)
        self.assertIn("interactive-root-terminal-required", result.stdout)


if __name__ == "__main__":
    unittest.main()
