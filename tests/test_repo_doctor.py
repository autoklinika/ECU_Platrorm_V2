"""Portable, completely offline regression tests for the ECU V2 repo doctor."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase, main, mock

FILE = Path(__file__).resolve().parents[1] / "scripts/ecu_repo_doctor.py"
spec = importlib.util.spec_from_file_location("ecu_repo_doctor", FILE)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)


class RepoDoctorSafety(TestCase):
    def test_worktree_porcelain_is_parsed_without_changing_git(self):
        porcelain = (
            "worktree /tmp/ecu1\n"
            "HEAD 1234567890abcdef1234567890abcdef12345678\n"
            "branch refs/heads/main\n\n"
            "worktree /tmp/ecu2\n"
            "HEAD abcdefabcdefabcdefabcdefabcdefabcdefabcd\n"
            "branch refs/heads/webgui/prototype\n\n"
        )
        with mock.patch.object(module, "git", return_value=porcelain) as get:
            trees = module.worktrees(Path("/tmp"))
        self.assertEqual(len(trees), 2)
        self.assertEqual(trees[0]["branch"], "main")
        self.assertEqual(trees[1]["branch"], "webgui/prototype")
        get.assert_called_once_with(Path("/tmp"), "worktree", "list", "--porcelain")

    def test_pr_dependency_chains_reconstruct_three_stacks(self):
        def pr(num, base, head):
            return {"number": num, "headRefName": head, "baseRefName": base,
                    "title": str(num), "isDraft": True}
        prs = [
            pr(26, "webgui/identify", "webgui/latest"),
            pr(19, "main", "webgui/home"),
            pr(20, "main", "api/base"),
            pr(21, "webgui/home", "webgui/identify"),
            pr(24, "api/base", "api/params"),
            pr(13, "dut-profile/proof", "app/sac"),
            pr(14, "app/sac", "app/read"),
        ]
        actual = [[p["number"] for p in chain] for chain in module.pr_chains(prs)]
        self.assertEqual(actual, [[13,14],[19,21,26],[20,24]])

    def test_incomplete_or_malformed_github_payload_fails_closed(self):
        with self.assertRaises(ValueError):
            module.parse_prs(json.dumps({"hello":"world"}))
        with self.assertRaises(ValueError):
            module.parse_prs('[{"number":1}]')
        self.assertEqual(module.parse_prs('[]'), [])

    def test_unavailable_github_is_optional_not_a_blocker(self):
        with mock.patch.object(module.shutil, "which", return_value=None):
            prs, note = module.load_prs(Path("/tmp"))
        self.assertIsNone(prs)
        self.assertIn("offline", note)

    def test_offline_check_does_not_execute_anything_without_suites(self):
        with TemporaryDirectory() as tmp:
            with mock.patch.object(module, "cmd") as command:
                code = module.check_offline(Path(tmp), "all")
            self.assertEqual(code, 2)
            command.assert_not_called()

    def test_cli_rejects_actions_that_modify_production_or_hardware(self):
        source = FILE.read_text(encoding="utf-8")
        for forbidden in ('"merge"', '"reset"', '"push"', '"cherry-pick"',
                          '"worktree", "remove"', '"sudo"', '"cansend"',
                          '"ip", "link", "set"'):
            self.assertNotIn(forbidden, source)
        self.assertEqual(module.SAC_PROTECTED, ("main", "production"))

    def test_json_snapshot_shape_without_any_github_access(self):
        with mock.patch.object(module, "snapshot", return_value={
            "origin":"autoklinika/ECU_Platrorm_V2", "main_local":"29666b5",
            "repo_branch":"audit", "checkouts":[], "dirty_count":0,
            "git_worktrees":0,"checkout_count":0
        }) as take:
            self.assertEqual(module.main(["status", "--json"]), 0)
            take.assert_called_once()

    def test_cm5_snapshot_refuses_nonlinux(self):
        with mock.patch.object(module.sys, "platform", "win32"):
            self.assertEqual(module.cm5_snapshot(Path("/"))["reason"], "non-linux")


if __name__ == "__main__":
    main()
