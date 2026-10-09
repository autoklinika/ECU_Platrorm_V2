"""Offline contract for the complete, commit-consistent ECU WebGUI release."""
from pathlib import Path
import subprocess
import unittest
import runpy

ROOT=Path(__file__).resolve().parents[1]
SCRIPT=ROOT/'scripts/deploy_cm5_sac_monitor_timer_hotfix.sh'
TEXT=SCRIPT.read_text()
STATIC=ROOT/'deploy/webgui/static_server.py'

class FullReleasePolicy(unittest.TestCase):
    def test_requires_interactive_sudo(self):
        r=subprocess.run(['bash',str(SCRIPT)],capture_output=True,text=True,timeout=3)
        self.assertEqual(r.returncode,77)
        self.assertIn('interactive-ecu-sudo-required',r.stderr)

    def test_filelist_exactly_matches_kiosk_host_allowlist(self):
        files=set(runpy.run_path(str(STATIC))['FILES'].values())
        self.assertEqual(len(files),10)
        for name in files:
            self.assertIn(name,TEXT)
        self.assertIn('set(server[\'FILES\'].values()) == expected',TEXT)
        self.assertIn('sha256sum --check --status',TEXT)

    def test_kiosk_stopped_before_installation(self):
        stop=TEXT.index('systemctl stop ecu-kiosk.service')
        wait=TEXT.index('ECU_WEBGUI_QUIESCE=PASS')
        install=TEXT.index('phase=stage',wait)
        restart=TEXT.index('systemctl start ecu-kiosk.service',install)
        self.assertTrue(stop < wait < install < restart)
        self.assertIn('down >= 4',TEXT)
        self.assertIn('state DOWN',TEXT)

    def test_single_release_and_atomic_symlink_rollback(self):
        for literal in ('old_release=releases/008b673f3d8b',
                        'git -C "$repo" status --porcelain',
                        'ECU_WEBGUI_ATOMIC_ROLLBACK=PASS',
                        'mv -Tf "$base/.new-atomic-$$" "$base/current"',
                        'phase=prestart-smoke',
                        'ECU_WEBGUI_ATOMIC=PASS'):
            self.assertIn(literal,TEXT)

    def test_no_backend_change_and_dtc_is_preserved(self):
        for protected in ('sha256sum "$dtc"','sha256sum "$adapter"',
                          'sha256sum "$probe"','ECU_WEBGUI_DTC=UNCHANGED'):
            self.assertIn(protected,TEXT)
        for forbidden in ('systemctl restart ecu-api-v1',
                          'systemctl restart ecu-sac-connect-v1',
                          'cansend','ClearDiagnosticInformation','ip link set can0 up'):
            self.assertNotIn(forbidden,TEXT)

    def test_shell_syntax(self):
        subprocess.run(['bash','-n',str(SCRIPT)],check=True,timeout=3)

if __name__=="__main__": unittest.main()
