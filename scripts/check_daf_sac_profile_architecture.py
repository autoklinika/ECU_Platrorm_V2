#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILE = ROOT / "src" / "dut_profiles" / "daf_sac"

FORBIDDEN = {
    "platform_include": re.compile(
        r'#\s*include\s*[<"](?:linux/|windows\.h|winsock|sys/socket|Qt|Q[A-Z])'
    ),
    "physical_can_driver": re.compile(
        r"\b(?:ICanDriver|ICanChannelArbiter|try_send\s*\(|try_receive\s*\()"
    ),
    "threading": re.compile(
        r"\b(?:std::thread|std::jthread|std::mutex|std::recursive_mutex|"
        r"std::condition_variable|pthread_|CreateThread\b)"
    ),
    "sleep": re.compile(
        r"\b(?:sleep\s*\(|usleep\s*\(|std::this_thread::sleep_)"
    ),
    "unbounded_container": re.compile(
        r"\bstd::(?:vector|deque|list|map|multimap|unordered_map|"
        r"unordered_set|set|multiset)\s*<"
    ),
    "heap_ownership": re.compile(
        r"\b(?:std::unique_ptr|std::shared_ptr|make_unique|make_shared)\b|"
        r"\bnew\s+|\bdelete\s+"
    ),
    "legacy_core_include": re.compile(r'#\s*include\s*[<"]ecu/core/'),
}

ALLOWED_SUFFIXES = {".hpp", ".h", ".cpp", ".cc", ".cxx"}

if not PROFILE.is_dir():
    print("DAF_SAC_PROFILE_ARCHITECTURE_GATE=FAIL missing profile", file=sys.stderr)
    sys.exit(1)

failures: list[str] = []
checked = 0

for path in sorted(PROFILE.rglob("*")):
    if not path.is_file() or path.suffix not in ALLOWED_SUFFIXES:
        continue
    checked += 1
    text = path.read_text(encoding="utf-8")
    relative = path.relative_to(ROOT)
    for name, pattern in FORBIDDEN.items():
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{relative}:{line}: forbidden {name}: {match.group(0)!r}"
            )

cmake = (PROFILE / "CMakeLists.txt").read_text(encoding="utf-8")
if "ECU::dut_profile" not in cmake or "ECU::core_v2" not in cmake:
    failures.append(
        "DAF SAC proof must depend on ECU::dut_profile and ECU::core_v2"
    )
if "ECU::core" in cmake.replace("ECU::core_v2", ""):
    failures.append("legacy ECU::core dependency is forbidden")

if failures:
    print("DAF_SAC_PROFILE_ARCHITECTURE_GATE=FAIL", file=sys.stderr)
    for failure in failures:
        print(failure, file=sys.stderr)
    sys.exit(1)

print(f"DAF_SAC_PROFILE_ARCHITECTURE_FILES={checked}")
print("DAF_SAC_PROFILE_DIRECT_PHYSICAL_CAN_ACCESS=NONE")
print("DAF_SAC_PROFILE_PLATFORM_DEPENDENCIES=NONE")
print("DAF_SAC_PROFILE_HIDDEN_THREADS_OR_SLEEP=NONE")
print("DAF_SAC_PROFILE_UNBOUNDED_RUNTIME_CONTAINERS=NONE")
print("DAF_SAC_PROFILE_HEAP_OWNERSHIP=NONE")
print("DAF_SAC_PROFILE_LEGACY_CORE_DEPENDENCY=NONE")
print("DAF_SAC_PROFILE_ARCHITECTURE_GATE=PASS")
