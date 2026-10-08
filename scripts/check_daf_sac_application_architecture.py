#!/usr/bin/env python3
"""Static architecture gate for the first ECU app module (not its OS adapter)."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "src" / "applications" / "daf_sac"
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc"}
RULES = {
    "platform-specific-header": re.compile(
        r'#\s*include\s*[<"](?:linux/|sys/|windows\.h|Qt|Q[A-Z]|ecu/platform/)'
    ),
    "legacy-core": re.compile(r'#\s*include\s*[<"]ecu/core/'),
    "low-level-hardware": re.compile(
        r"\b(?:ICanDriver|try_send\s*\(|try_receive\s*\(|"
        r"GPIO|gpio|ioctl\s*\(|socket\s*\(|runuser|system\s*\()"
    ),
    "hidden-thread": re.compile(
        r"\b(?:std::thread|std::jthread|pthread_|"
        r"std::this_thread::sleep_for)"
    ),
    "heap-ownership": re.compile(
        r"\b(?:std::unique_ptr|std::shared_ptr|make_unique|make_shared|"
        r"new\s+|delete\s+)"
    ),
}
issues = []
checked = 0

for file in sorted(APP.rglob("*")):
    if not file.is_file() or file.suffix not in SOURCE_SUFFIXES:
        continue
    checked += 1
    source = file.read_text(encoding="utf-8")
    code = re.sub(
        r"//[^\n]*|/\*.*?\*/",
        lambda match: "\n" * match.group(0).count("\n"),
        source,
        flags=re.DOTALL,
    )
    for label, regex in RULES.items():
        for found in regex.finditer(code):
            line = code.count("\n", 0, found.start()) + 1
            issues.append(f"{file.relative_to(ROOT)}:{line}: {label}: {found.group(0)}")

cmake = (APP / "CMakeLists.txt").read_text(encoding="utf-8")
for dependency in ("ECU::bench", "ECU::dut_profile", "ECU::daf_sac_profile", "ECU::core_v2"):
    if dependency not in cmake:
        issues.append(f"missing required dependency {dependency}")

if re.search(r"ECU::core(?!_v2)\b", cmake):
    issues.append("forbidden legacy ECU::core dependency")

if checked < 2:
    issues.append("DAF SAC application source files missing")

if issues:
    print("DAF_SAC_APPLICATION_ARCHITECTURE=FAIL", file=sys.stderr)
    for item in issues:
        print(item, file=sys.stderr)
    sys.exit(1)

print(f"DAF_SAC_APPLICATION_SOURCES={checked}")
print("DAF_SAC_APPLICATION_HARDWARE_AND_GUI_DEPENDENCIES=NONE")
print("DAF_SAC_APPLICATION_LEGACY_CORE_DEPENDENCY=NONE")
print("DAF_SAC_APPLICATION_ARCHITECTURE=PASS")
