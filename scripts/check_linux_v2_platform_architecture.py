#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "src" / "platform" / "linux" / "v2"

FORBIDDEN = {
    "legacy_core": re.compile(r'#\s*include\s*[<"]ecu/core/'),
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
}

ALLOWED_SUFFIXES = {".hpp", ".h", ".cpp", ".cc", ".cxx"}

if not PLATFORM.is_dir():
    print("LINUX_V2_PLATFORM_ARCHITECTURE_GATE=FAIL missing platform", file=sys.stderr)
    sys.exit(1)

failures: list[str] = []
checked = 0

for path in sorted(PLATFORM.rglob("*")):
    if not path.is_file() or path.suffix not in ALLOWED_SUFFIXES:
        continue
    checked += 1
    text_value = path.read_text(encoding="utf-8")
    relative = path.relative_to(ROOT)
    for name, pattern in FORBIDDEN.items():
        for match in pattern.finditer(text_value):
            line = text_value.count("\n", 0, match.start()) + 1
            failures.append(
                f"{relative}:{line}: forbidden {name}: {match.group(0)!r}"
            )

cmake = (PLATFORM / "CMakeLists.txt").read_text(encoding="utf-8")
if "ECU::core_v2" not in cmake:
    failures.append("Linux V2 platform must depend on ECU::core_v2")
if "ECU::core" in cmake.replace("ECU::core_v2", ""):
    failures.append("legacy ECU::core dependency is forbidden")

if failures:
    print("LINUX_V2_PLATFORM_ARCHITECTURE_GATE=FAIL", file=sys.stderr)
    for failure in failures:
        print(failure, file=sys.stderr)
    sys.exit(1)

print(f"LINUX_V2_PLATFORM_ARCHITECTURE_FILES={checked}")
print("LINUX_V2_PLATFORM_LEGACY_CORE_DEPENDENCY=NONE")
print("LINUX_V2_PLATFORM_HIDDEN_THREADS_OR_SLEEP=NONE")
print("LINUX_V2_PLATFORM_UNBOUNDED_RUNTIME_CONTAINERS=NONE")
print("LINUX_V2_PLATFORM_HEAP_OWNERSHIP=NONE")
print("LINUX_V2_PLATFORM_ARCHITECTURE_GATE=PASS")
