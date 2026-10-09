"""Unprivileged, multi-OS safety gates for API SAC parameter readout upgrade."""
from pathlib import Path
import sys
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
PREP = (ROOT / "scripts/prepare_cm5_api_parameters_v1.sh").read_text()
UPGRADE = (ROOT / "scripts/upgrade_cm5_api_sac_parameters_v1.sh").read_text()
SMOKE = (ROOT / "scripts/check_cm5_sac_parameters_upgrade.py").read_text()
PROBE = (ROOT / "scripts/run_stage42_daf_sac_500k_read_gate.sh").read_text()
STAGE42 = (ROOT / "tests/daf_sac_stage42_read_probe.cpp").read_text()
ROUTER = (ROOT / "src/api/src/router.cpp").read_text()

class UpgradeSafety(unittest.TestCase):
    def test_readout_uses_existing_probe_and_native_app_snapshot(self):
        for item in ('if [[ "$PUBLISH_READOUT" == 1 ]]',
                     'PROBE_ARGS+=("$READOUT_DIR")',
                     "READ_MODES=(parameters dtc)",
                     "READ_MODES=(parameters)",
                     'for mode in "$' + '{READ_MODES[@]}"; do',
                     "SAC_500K_READ_GATE=PASS identity-parameters-only-no-DTC"):
            self.assertIn(item, PROBE)
        for item in ("capture_daf_sac_completed_parameters",
                     "publish_linux_sac_parameters", "application.voltage()",
                     "application.pressure()", "resources.active_count()",
                     "completed-parameters-export"):
            self.assertIn(item, STAGE42)
        self.assertIn("/api/v1/readouts/daf-sac/parameters/latest", ROUTER)
        self.assertIn('"$MODE" == parameters )', PROBE)
        self.assertIn('api_parameters_candidate.sha', PROBE)
        self.assertIn('stale-candidate-binaries', PROBE)
        self.assertIn('build/params-linux', PROBE)
        self.assertIn('if [[ "$MODE" == parameters ]]; then', PROBE)
        for text in (STAGE42, ROUTER, PROBE):
            self.assertNotIn("cansend ", text)
            self.assertNotIn("systemctl restart ecu-platform", text)

    def test_passive_rx_overflow_remains_fail_closed_and_distinguishable(self):
        self.assertIn("controller-problem{rx-overflow}", PROBE)
        self.assertIn("SAC_500K_PASSIVE_RX_OVERFLOW_EVENTS=", PROBE)
        self.assertIn("SAC_500K_PASSIVE=FAIL rx-controller-overflow", PROBE)
        self.assertIn("SAC_500K_PASSIVE=FAIL unexpected-transmission", PROBE)
        self.assertIn("physical-errors-or-unclassified-receive-errors", PROBE)
        self.assertIn("RX_OVERFLOW_EVENTS == ERROR_FRAMES", PROBE)
        self.assertIn("ERR_AFTER - ERR_BEFORE == RX_OVERFLOW_EVENTS", PROBE)
        guard_start = PROBE.index('if (( ERR_AFTER > ERR_BEFORE || TX_AFTER > TX_BEFORE || ERROR_FRAMES > 0 ))')
        active_start = PROBE.index('# 2. User explicitly selected a 500k ECU.')
        guard = PROBE[guard_start:active_start]
        self.assertIn("exit 1", guard)
        self.assertLess(guard.index("SAC_500K_PASSIVE=FAIL rx-controller-overflow"),
                        guard.index("exit 1"))
        self.assertNotIn("SAC_500K_PASSIVE=RX_OBSERVED", guard.split("exit 1")[0])

    def test_upgrade_restores_existing_service_and_credentials(self):
        for required in ('interactive-operator-root-required',
                         'SUDO_USER', 'git -C "$repo" status --porcelain',
                         'ecu-api-v1.service', 'state DOWN',
                         'api_parameters_candidate.sha', 'expected_digest',
                         'sha256sum "$candidate"', 'SAC_API_UPGRADE_AUTO_ROLLBACK',
                         'SAC_API_UPGRADE_DTC_EVIDENCE=UNCHANGED',
                         'ecu-api-parameters-rollback'):
            self.assertIn(required, UPGRADE)
        for forbidden in ('useradd ', 'groupadd ', 'openssl rand',
                          'systemctl restart ecu-kiosk.service',
                          'systemctl restart ecu-webgui-static.service',
                          'systemctl restart ecu-platform-v2-bench-agent.service',
                          'ip link set', 'cansend'):
            self.assertNotIn(forbidden, UPGRADE)
        self.assertIn('api_request', SMOKE)
        self.assertIn('PRESERVED_REAL_DTC_READOUT', SMOKE)
        self.assertIn('PARAMETER_BEARER_REQUIRED', SMOKE)
        self.assertIn('PARAMETER_HISTORICAL_ONLY', SMOKE)
        self.assertIn('status in (400, 405)', SMOKE)
        self.assertIn('for attempt in {1..40}', UPGRADE)
        self.assertIn('SAC_API_UPGRADE_ROLLBACK=HTTP_NOT_READY', UPGRADE)

        self.assertNotIn('print(token)', SMOKE)
        self.assertIn('grep -Fx "$revision" >/dev/null', PREP)
        self.assertIn('grep -Fx "$expected_sha" >/dev/null', UPGRADE)
        self.assertIn('{12,40}', UPGRADE)
        self.assertNotIn('grep -Fxq "$revision"', PREP)
        self.assertIn('ctest --test-dir build/params-linux', PREP)
        self.assertIn("SAC_PARAMETER_API_PREPARE=PASS", PREP)

    @unittest.skipUnless(sys.platform.startswith("linux"),
                         "CM5 interactive root safety gate requires Linux")
    def test_nonroot_cannot_upgrade_api(self):
        result = subprocess.run(
            ["bash", str(ROOT / "scripts/upgrade_cm5_api_sac_parameters_v1.sh")],
            capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 77)
        self.assertIn("interactive-operator-root-required", result.stdout)

if __name__ == "__main__":
    unittest.main()
