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

    @unittest.skipUnless(sys.platform.startswith("linux"),
                         "Executable Bash gate classifier runs on Linux")
    def test_passive_500k_gate_is_observational_until_uds_identity(self):
        self.assertIn("controller-problem{rx-overflow}", PROBE)
        self.assertIn("SAC_500K_PASSIVE_RX_OVERFLOW_EVENTS=", PROBE)
        self.assertIn("SAC_500K_COMMUNICATION_PROOF=PASS uds-f190-f188-f192", PROBE)
        self.assertIn("SAC_PHYSICAL_PROBE=PASS", PROBE)
        self.assertIn("SAC_500K_IDENTIFY=FAIL", PROBE)

        # Execute the *actual* Bash decision block offline with synthetic
        # counters: no CAN access, no sudo, no physical DUT interaction.
        start = PROBE.index('if [[ "$PASSIVE_LINK_STATE" == *"state BUS-OFF"* ]]; then')
        end = PROBE.index('if [[ "$MODE" == passive ]]; then', start)
        decision = PROBE[start:end]
        cases = (
            # (rx_error_delta, error_frames, overflow_events, tx_delta,
            #  data_frames, bus_off, exit_status, diagnostic)
            (1, 1, 1, 0, 16265, False, 0,
             "WARNING rx-controller-overflow-awaiting-uds-identity"),
            (0, 0, 0, 0, 16265, False, 0,
             "RX_OBSERVED_NOT_COMMUNICATION_PROOF"),
            (0, 0, 0, 0, 0, False, 0, "INCONCLUSIVE_NO_BROADCAST"),
            (1, 1, 0, 0, 16265, False, 1,
             "FAIL physical-errors-or-unclassified-receive-errors"),
            (2, 1, 1, 0, 16265, False, 1,
             "FAIL physical-errors-or-unclassified-receive-errors"),
            (1, 2, 1, 0, 16265, False, 1,
             "FAIL physical-errors-or-unclassified-receive-errors"),
            (1, 1, 1, 1, 16265, False, 1, "FAIL unexpected-transmission"),
            (1, 1, 1, 0, 16265, True, 1, "FAIL passive-bus-off"),
        )
        for rx_err, frames, overflow, tx, data, bus_off, status, diagnostic in cases:
            with self.subTest(rx_err=rx_err, frames=frames,
                              overflow=overflow, tx=tx, data=data,
                              bus_off=bus_off):
                link_state = "BUS-OFF" if bus_off else "ERROR-ACTIVE"
                inputs = (
                    "set -Eeuo pipefail\nSUMMARY=/dev/null\n"
                    f"PASSIVE_LINK_STATE='can state {link_state}'\n"
                    f"ERR_BEFORE=0; ERR_AFTER={rx_err}; "
                    f"ERROR_FRAMES={frames}; RX_OVERFLOW_EVENTS={overflow}; "
                    f"TX_BEFORE=0; TX_AFTER={tx}; DATA_FRAMES={data}\n"
                )
                result = subprocess.run(["bash", "-c", inputs + decision],
                                        capture_output=True, text=True, timeout=3)
                self.assertEqual(result.returncode, status, result.stderr)
                self.assertIn("SAC_500K_PASSIVE=" + diagnostic, result.stdout)

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
