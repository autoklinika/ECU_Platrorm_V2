"""Safety/portability gates for legacy-inspired read-only TESTS catalog."""
from pathlib import Path
import sys
import subprocess
import unittest

BASE = Path(__file__).resolve().parents[1]
DEPLOY = BASE / "scripts" / "deploy_cm5_webgui_test_catalog_v1.sh"
DOCUMENT = BASE / "docs" / "WEBGUI_TEST_CATALOG_DAF_SAC_LEGACY_REFERENCE.md"
UI = BASE / "webgui"


class CatalogDeploymentTests(unittest.TestCase):
    def test_scoped_operator_installer(self):
        shell = DEPLOY.read_text()
        self.assertIn('[[ "$EUID" -eq 0 && -t 0 ]]', shell)
        self.assertIn("webgui/test-catalog-daf-sac-v1", shell)
        self.assertIn("ECU_WebGUI_TESTS_SAC_V1", shell)
        self.assertIn("state DOWN", shell)
        self.assertIn("WEBGUI_TEST_CATALOG_ROLLBACK=START", shell)
        self.assertIn("ecu-webgui-test-catalog-rollback", shell)
        self.assertIn("trap rollback EXIT", shell)
        self.assertIn("mv -Tf", shell)
        self.assertIn("src/api-client.mjs", shell)
        for forbidden in ("systemctl restart ecu-api-v1",
                          "systemctl restart ecu-platform-v2-bench-agent",
                          "ip link set", "cansend", "systemctl restart can",
                          "token-file", "git push"):
            self.assertNotIn(forbidden, shell)

    @unittest.skipUnless(sys.platform.startswith("linux"),
                         "CM5 Bash root/TTY gate requires Linux")
    def test_installer_denies_unprivileged_nontty(self):
        result = subprocess.run(["bash", str(DEPLOY)], capture_output=True,
                                text=True, input="", timeout=5)
        self.assertEqual(result.returncode, 77)
        self.assertIn("interactive-root-terminal-required", result.stdout)

    def test_four_parameters_and_guarded_menu_are_only_presentation(self):
        # Explicit UTF-8 is required: Windows runners may default to
        # a locale encoding that corrupts the em dash placeholder.
        html = (UI / "index.html").read_text(encoding="utf-8")
        javascript = (UI / "src" / "app.mjs").read_text()
        for identifier in ["sac-pressure-1", "sac-pressure-2",
                           "sac-permanent-voltage", "sac-ignition-voltage"]:
            self.assertIn('id="' + identifier + '">—', html)
        for route in ["sac-dtc", "sac-activations", "sac-programming"]:
            self.assertIn('data-route="' + route + '"', html)
        self.assertIn("sac.restricted", html)
        self.assertIn("SAC_DTC_PROFILES", javascript)
        self.assertNotIn("startDTCRead(", javascript)
        self.assertNotIn("clearDTC(", javascript)
        self.assertNotIn("CockpitController", javascript)
        self.assertNotIn("SystemController", javascript)
        self.assertNotIn("navigator.serial", javascript)

    def test_legacy_source_is_reference_only(self):
        doc = DOCUMENT.read_text()
        self.assertIn("autoklinika/ecu_platform", doc)
        self.assertIn("src/QML/SACMenuPage.qml", doc)
        self.assertIn("not zero bar", doc)
        self.assertIn("No automatic deployment", doc)
        self.assertIn("No physical ECU diagnostic TX", doc)


if __name__ == "__main__":
    unittest.main()
