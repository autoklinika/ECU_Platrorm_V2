"""Cross-platform WebGUI V1-skin safety and operator deployment gates."""
from pathlib import Path
import subprocess
import sys
import unittest

BASE = Path(__file__).resolve().parents[1]
HTML = (BASE / "webgui/index.html").read_text(encoding="utf-8")
CSS = (BASE / "webgui/styles.css").read_text(encoding="utf-8")
APP = (BASE / "webgui/src/app.mjs").read_text(encoding="utf-8")
SCRIPT = (BASE / "scripts/deploy_cm5_webgui_v1_skin.sh").read_text(encoding="utf-8")


class IndustrialSkinSafety(unittest.TestCase):
    def test_exact_v1_palette_without_old_qml_button_colors(self):
        for color in ("--page-bg: #f1f3f6", "--panel-bg: #f7f8fa",
                      "--border: #b2bfcb", "--blue: #226da8"):
            self.assertIn(color, CSS)
        for removed in ("#59c8ff", "#2a84c9", "#7dd6ff",
                        "border-radius: 16px", "legacy-page",
                        "legacy-parameter-panel"):
            self.assertNotIn(removed, CSS.lower())
        self.assertIn(".catalog-page--selection .tile", CSS)
        self.assertIn(".sac-parameter-row", CSS)
        self.assertIn("cursor: none !important", CSS)

    def test_catalog_does_not_introduce_bus_or_dut_side_effects(self):
        for name in ("sac-pressure-1", "sac-pressure-2",
                     "sac-permanent-voltage", "sac-ignition-voltage"):
            self.assertIn('id="' + name + '">—</output>', HTML)
        for page in ("sac-dtc", "sac-activations", "sac-programming"):
            self.assertIn('data-route="' + page + '"', HTML)
        for forbidden in ("clearDTC(", "startDTCRead(", "CockpitController",
                          "SystemController", "navigator.serial", "navigator.usb"):
            self.assertNotIn(forbidden, APP)
        self.assertIn("sac.restricted", HTML)
        self.assertIn("noReadout", HTML)

    def test_scoped_install_is_operator_only(self):
        for expected in ('[[ "$EUID" -eq 0 && -t 0 ]]',
                         "webgui/test-catalog-v1-industrial-skin",
                         "ECU_WebGUI_V1_SKIN",
                         'git -C "$repo" status --porcelain',
                         "state DOWN", "ecu-api-v1.service",
                         "ecu-platform-v2-bench-agent.service",
                         "WEBGUI_V1_SKIN_ROLLBACK=START",
                         "ecu-webgui-v1-skin-rollback",
                         ".catalog-page--selection .tile {",
                         "mv -Tf"):
            self.assertIn(expected, SCRIPT, expected)
        for forbidden in ("systemctl restart ecu-api-v1",
                          "systemctl restart ecu-platform-v2-bench-agent",
                          "ip link set", "cansend", "sudoers", "git push"):
            self.assertNotIn(forbidden, SCRIPT)
        self.assertEqual(SCRIPT.count("systemctl restart ecu-kiosk.service"), 2)
        self.assertEqual(SCRIPT.count("systemctl restart ecu-webgui-static.service"), 2)

    @unittest.skipUnless(sys.platform.startswith("linux"),
                         "CM5 root/TTY runtime denial is Linux-only")
    def test_installer_rejects_unattended_execution(self):
        result = subprocess.run(["bash", str(BASE / "scripts/deploy_cm5_webgui_v1_skin.sh")],
                                input="", capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 77)
        self.assertIn("interactive-root-terminal-required", result.stdout)


if __name__ == "__main__":
    unittest.main()
