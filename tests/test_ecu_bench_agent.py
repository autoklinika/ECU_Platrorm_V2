"""No-hardware tests of permanently installed privileged bench boundary."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import socket
import stat
import tempfile
import types
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "deploy/ecu_bench_agent/agent_server.py"
SPEC = importlib.util.spec_from_file_location("ecu_v2_bench_agent", MODULE)
assert SPEC is not None and SPEC.loader is not None
agent = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(agent)
OPERATOR = types.SimpleNamespace(pw_uid=os.getuid(), pw_gid=os.getgid())


class BenchAgentTests(unittest.TestCase):

    def test_systemd_owns_group_and_no_cap_chown_needed(self):
        template = (ROOT / 'deploy/ecu_bench_agent/ecu-platform-v2-bench-agent.service.in').read_text()
        self.assertIn('User=root', template)
        self.assertIn('Group=@OPERATOR@', template)
        self.assertIn('RuntimeDirectoryMode=0750', template)
        self.assertIn('CapabilityBoundingSet=CAP_NET_ADMIN CAP_SETUID CAP_SETGID', template)
        self.assertNotIn('CAP_CHOWN', template)
        source = MODULE.read_text()
        self.assertNotIn('os.chown(', source)

    def test_systemd_runtime_directory_and_socket_permissions(self):
        folder = types.SimpleNamespace(st_mode=stat.S_IFDIR | 0o750, st_uid=0, st_gid=1000)
        self.assertTrue(agent.valid_runtime_directory(folder, 1000, 1000))
        self.assertFalse(agent.valid_runtime_directory(folder, 0, 1000))
        self.assertFalse(agent.valid_runtime_directory(folder, 1000, 0))
        self.assertFalse(agent.valid_runtime_directory(
            types.SimpleNamespace(st_mode=stat.S_IFDIR | 0o770, st_uid=0, st_gid=1000),
            1000, 1000,
        ))
        sock = types.SimpleNamespace(st_mode=stat.S_IFSOCK | 0o660, st_uid=0, st_gid=1000)
        self.assertTrue(agent.valid_server_socket(sock, 1000))
        self.assertFalse(agent.valid_server_socket(sock, 2000))
        self.assertFalse(agent.valid_server_socket(
            types.SimpleNamespace(st_mode=stat.S_IFSOCK | 0o666, st_uid=0, st_gid=1000),
            1000,
        ))

    def test_allowlist_rejects_destructive_and_shell_operations(self):
        for name in (
            "sac.clear_dtc", "clear", "erase", "shell", "sudo",
            "can.up", "can.down", "ecu.flash", "diagnostic_session",
        ):
            with self.subTest(name=name):
                result = agent.dispatch({"operation": name}, OPERATOR.pw_uid, OPERATOR)
                self.assertEqual(result["status"], "denied")
        self.assertEqual(
            agent.dispatch({"operation": "sac.read_dtc"}, OPERATOR.pw_uid + 1, OPERATOR)["status"],
            "denied",
        )
        self.assertEqual(agent.dispatch(
            {"operation": "sac.read_dtc", "command": "rm -rf /"},
            OPERATOR.pw_uid, OPERATOR,
        )["status"], "denied")
        self.assertEqual(
            agent.dispatch({"operation": "status", "extra": True}, OPERATOR.pw_uid, OPERATOR)["status"],
            "denied",
        )

    def test_can_up_never_interrupts_current_owner(self):
        with mock.patch.object(agent, "valid_installed_probe"), mock.patch.object(
            agent, "can_info", return_value={"flags": ["UP"]},
        ), mock.patch.object(agent, "run_ip") as ip_cmd:
            result = agent.dispatch({"operation": "sac.read_dtc"}, OPERATOR.pw_uid, OPERATOR)
            self.assertEqual(result["status"], "busy")
            ip_cmd.assert_not_called()

    def test_read_probe_runs_as_operator_and_cleans_down(self):
        read_result = types.SimpleNamespace(
            returncode=0,
            stdout="SAC_STAGE42_READ_PHYSICAL=PASS\nSAC_DTC_COUNT=0\n",
            stderr="",
        )
        with mock.patch.object(agent, "valid_installed_probe"), mock.patch.object(
            agent, "can_info", return_value={"flags": []},
        ), mock.patch.object(agent, "run_ip") as ip_cmd, mock.patch.object(
            agent.subprocess, "run", return_value=read_result,
        ) as probe:
            result = agent.dispatch(
                {"operation": "sac.read_dtc"}, OPERATOR.pw_uid, OPERATOR,
            )
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["can0_cleanup"], "DOWN")
        self.assertEqual(result["mode"], "dtc")
        self.assertIn("SAC_DTC_COUNT=0", result["output"])
        self.assertEqual(ip_cmd.call_count, 4)
        self.assertEqual(ip_cmd.call_args.args, ("down",))
        self.assertEqual(probe.call_args.args[0], [str(agent.PROBE_PATH), "can0", "dtc"])
        self.assertEqual(probe.call_args.kwargs["user"], OPERATOR.pw_uid)
        self.assertEqual(probe.call_args.kwargs["group"], OPERATOR.pw_gid)
        self.assertEqual(probe.call_args.kwargs["extra_groups"], [])

    def test_bad_probe_never_activates_can(self):
        with mock.patch.object(
            agent, "valid_installed_probe", side_effect=RuntimeError("invalid"),
        ), mock.patch.object(agent, "run_ip") as ip_cmd:
            result = agent.dispatch(
                {"operation": "sac.read_parameters"}, OPERATOR.pw_uid, OPERATOR,
            )
            self.assertEqual(result["status"], "failed")
            ip_cmd.assert_not_called()

    def test_failure_and_timeout_clean_up_without_retry(self):
        for behavior in (
            types.SimpleNamespace(returncode=1, stdout="bad\n", stderr=""),
            subprocess_timeout(),
        ):
            with self.subTest(kind=type(behavior).__name__), mock.patch.object(
                agent, "valid_installed_probe"
            ), mock.patch.object(
                agent, "can_info", return_value={"flags": []}
            ), mock.patch.object(
                agent, "run_ip"
            ) as ip_cmd, mock.patch.object(
                agent.subprocess, "run",
                side_effect=behavior if isinstance(behavior, BaseException) else None,
                return_value=None if isinstance(behavior, BaseException) else behavior,
            ) as probe:
                result = agent.execute_read("sac.read_parameters", OPERATOR)
                self.assertEqual(result["status"], "failed")
                self.assertEqual(result["can0_cleanup"], "DOWN")
                self.assertEqual(probe.call_count, 1)
                self.assertEqual(ip_cmd.call_args.args, ("down",))

    def test_cleanup_failure_is_critical(self):
        with mock.patch.object(agent, "valid_installed_probe"), mock.patch.object(
            agent, "can_info", return_value={"flags": []}
        ), mock.patch.object(
            agent, "run_ip", side_effect=[None, None, None, OSError("down error")],
        ), mock.patch.object(
            agent.subprocess, "run",
            return_value=types.SimpleNamespace(
                returncode=0, stdout="SAC_STAGE42_READ_PHYSICAL=PASS\n", stderr=""
            ),
        ), mock.patch.object(agent.logging, "exception"):
            result = agent.execute_read("sac.read_dtc", OPERATOR)
            self.assertEqual(result["status"], "critical")
            self.assertEqual(result["can0_cleanup"], "FAILED")

    def test_unix_socket_parser_rejects_extra_parameters_and_truncation(self):
        client, server = socket.socketpair()
        with client, server:
            client.sendall(b'{"operation":"status"}\n')
            self.assertEqual(agent.read_message(server), {"operation": "status"})
        client, server = socket.socketpair()
        with client, server:
            client.sendall(b'{"operation":')
            client.shutdown(socket.SHUT_WR)
            with self.assertRaises(ValueError):
                agent.read_message(server)


def subprocess_timeout():
    import subprocess
    return subprocess.TimeoutExpired(["probe"], 14)


if __name__ == "__main__":
    unittest.main()
