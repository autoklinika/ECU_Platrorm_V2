#!/usr/bin/env python3
"""Static guardrails for the CM5 API-only service and deployment scripts.

Runs offline as an unprivileged CI user. Never installs anything.
"""
from pathlib import Path
import configparser
import re

ROOT = Path(__file__).resolve().parents[2]
UNIT = ROOT / "deploy/api/ecu-api-v1.service"
INSTALL = ROOT / "scripts/install_cm5_api_v1.sh"
ROLLBACK = ROOT / "scripts/rollback_cm5_api_v1.sh"
PREPARE = ROOT / "scripts/prepare_cm5_api_v1.sh"
PROBE = ROOT / "scripts/run_stage42_daf_sac_500k_read_gate.sh"


def must(condition, label):
    if not condition:
        raise AssertionError("ECU_API_DEPLOY_POLICY_FAIL=" + label)


def main():
    config = configparser.ConfigParser(interpolation=None)
    must(config.read(UNIT), "unit_found")
    service = config["Service"]
    required = {
        "User": "ecu-api",
        "Group": "ecu-api",
        "SupplementaryGroups": "ecu-api-read",
        "NoNewPrivileges": "yes",
        "CapabilityBoundingSet": "",
        "AmbientCapabilities": "",
        "PrivateDevices": "yes",
        "ProtectHome": "yes",
        "ProtectSystem": "strict",
        "RestrictAddressFamilies": "AF_INET AF_NETLINK",
        "SystemCallArchitectures": "native",
        "Restart": "on-failure",
    }
    for key, value in required.items():
        must(service.get(key) == value, "unit_" + key)
    start = service.get("ExecStart", "")
    must(start.startswith("/usr/local/libexec/ecu-platform-v2/ecu_api_http"),
         "only_explicit_api_binary")
    for required_arg in ("--port 8878", "--can-interface can0",
                         "--readout-owner ecu", "--readout-group ecu-api-read",
                         "--token-file /etc/ecu-platform-v2/api/token"):
        must(required_arg in start, "start_" + required_arg.split()[0])
    for forbidden in ("sudo ", "bash -c", "sh -c", "candump", "cansend",
                      "python3", "ecu_bench.py"):
        must(forbidden not in start, "no_execution_" + forbidden)

    installer = INSTALL.read_text()
    rollback = ROLLBACK.read_text()
    prep = PREPARE.read_text()
    probe = PROBE.read_text()
    must("interactive-root-terminal-required" in installer,
         "interactive_privilege_gate")
    must('SUDO_USER:-' in installer and 'expected-operator-ecu' in installer,
         "expected_operator_guard")
    must("REPO_GIT=(runuser -u ecu -- git" in installer,
         "root_does_not_bypass_git_ownership")
    must("CapabilityBoundingSet" not in installer,
         "no_capability_grants")
    must("systemctl start ecu-api-v1.service" in installer and
         "systemctl enable ecu-api-v1.service" in installer,
         "independent_staged_service")
    must("ECU_API_CM5_INSTALL_SMOKE" in
         (ROOT / "scripts/verify_cm5_api_v1.py").read_text(),
         "separate_http_smoke")
    must("api-v1-prepared.sha256" in installer and
         "api-v1-prepared.commit" in installer,
         "pinned_candidate_artifact")
    must("systemctl stop ecu-api-v1.service" in rollback and
         "systemctl disable ecu-api-v1.service" in rollback,
         "scoped_service_rollback")
    must(not re.search(r"systemctl\s+(stop|restart|disable|enable)\s+"
                       r"(ecu-kiosk|ecu-webgui|ecu-platform-v2-bench-agent)",
                       installer + rollback), "no_existing_service_mutation")
    must("PROBE_BUILD=build/api-readout-linux" in prep,
         "isolated_operator_probes")
    must("--target ecu_daf_sac_core_v2_probe ecu_daf_sac_stage42_read_probe" in prep,
         "both_operator_binaries_present")
    must(r"\n  --parallel" not in prep and
         r"\n  -DECU_BUILD_APPLICATION_API" not in prep,
         "no-escaped-shell_newline_artifact")
    must('"--publish-readout"' in probe and
         'PUBLISH_READOUT=0' in probe, "explicit_readout_optin")
    print("ECU_API_CM5_DEPLOY_POLICY=PASS")


if __name__ == "__main__":
    main()
