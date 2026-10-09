"""Offline cutover policy for the SAC screen-scoped GUI release.

Never opens CAN, changes systemd units or needs root. Regression for the
previous smoke-gate race caused by an in-flight Parameters screen read.
"""
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/deploy_cm5_sac_monitor_timer_hotfix.sh"
TEXT = SCRIPT.read_text(encoding="utf-8")

class SacTimerInstallRegression(unittest.TestCase):
    def test_only_interactive_operator_can_modify_system(self):
        result = subprocess.run(["bash", str(SCRIPT)], capture_output=True,
                                text=True, timeout=3)
        self.assertEqual(result.returncode,77)
        self.assertIn("interactive-ecu-sudo-required",
                      result.stdout + result.stderr)

    def test_operator_stops_kiosk_before_cutover_and_waits_for_can_idle(self):
        q = TEXT.index("phase=quiesce-kiosk")
        stop = TEXT.index("systemctl stop ecu-kiosk.service", q)
        idle = TEXT.index("SAC_TIMER_QUIESCE=PASS kiosk-stopped-can0-idle", stop)
        cutover = TEXT.index("phase=cutover", idle)
        link = TEXT.index('mv -Tf "$base/.timer-hotfix-$$" "$base/current"', cutover)
        start = TEXT.index("systemctl restart ecu-kiosk.service", link)
        smoke = TEXT.index("phase=smoke", start)
        self.assertLess(q, stop)
        self.assertLess(stop, idle)
        self.assertLess(idle, cutover)
        self.assertLess(cutover, link)
        self.assertLess(link, start)
        self.assertLess(start, smoke)
        self.assertIn("stable_down >= 4", TEXT)
        self.assertIn('state DOWN', TEXT)

    def test_rollback_has_bounded_idle_wait_and_old_revision(self):
        self.assertIn("old_release=releases/e804cdbd67e4",TEXT)
        self.assertIn('SAC_TIMER_ROLLBACK=PASS', TEXT)
        self.assertIn('rollback_down >= 4', TEXT)
        self.assertIn('rollback_down < 4', TEXT)
        self.assertIn('SAC_TIMER_SMOKE_CAN=PASS', TEXT)

    def test_no_probe_or_backend_change(self):
        self.assertNotIn("systemctl restart ecu-sac-connect-v1", TEXT)
        self.assertNotIn("systemctl restart ecu-api-v1", TEXT)
        self.assertNotIn("cansend", TEXT)
        self.assertNotIn("ClearDiagnosticInformation", TEXT)
        self.assertNotIn("ip link set can0 up", TEXT)
        self.assertIn('sha256sum "$dtc"', TEXT)
        self.assertIn('sha256sum "$adapter"', TEXT)

    def test_staged_release_is_only_reused_after_exact_validation(self):
        for marker in (
            "SAC_TIMER_PREFLIGHT=FAIL unsafe-existing-release",
            "SAC_TIMER_PREFLIGHT=FAIL existing-release-mismatch=",
            "SAC_TIMER_PREFLIGHT=FAIL staged-asset-mismatch=",
            'cmp -s "$repo/webgui/$asset" "$release/$asset"',
            "if ((staged == 0)); then"
        ):
            self.assertIn(marker,TEXT)

    def test_smoke_failures_have_actionable_gates(self):
        for reason in ("kiosk-http-not-ready", "release-link",
                       "asset-digest=", "dtc-changed", "adapter-changed", "api-restarted",
                       "sac-adapter-restarted", "inactive-service=",
                       "can0-not-idle"):
            self.assertIn(reason,TEXT)
        subprocess.run(["bash","-n",str(SCRIPT)],check=True,timeout=3)

if __name__ == "__main__":
    unittest.main()
