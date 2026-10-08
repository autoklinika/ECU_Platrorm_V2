#!/usr/bin/env python3
"""Fail-closed check that a *fresh default* CMake build selects V2, not V1.

Run AFTER configuring without -DECU_BUILD_* overrides. Portable Python: no
Linux, GitHub Actions, shell, application or hardware dependencies.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

REQUIRED = {
    "ECU_BUILD_TESTS": "ON",
    "ECU_BUILD_LEGACY_CORE": "OFF",
    "ECU_BUILD_SAC_MODULE": "OFF",
    "ECU_BUILD_LINUX_SOCKETCAN": "OFF",
    "ECU_BUILD_CORE_V2": "ON",
    "ECU_BUILD_BENCH_RUNTIME": "ON",
    "ECU_BUILD_DUT_PROFILE": "ON",
    "ECU_BUILD_DAF_SAC_PROFILE": "ON",
    "ECU_BUILD_DAF_SAC_APPLICATION": "ON",
}

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--system", choices=("Linux", "Windows", "Generic"),
                        required=True)
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    cache = build_dir / "CMakeCache.txt"
    try:
        text = cache.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"V2_DEFAULT_BUILD_GATE=FAIL cache missing: {exc}", file=sys.stderr)
        return 1
    options = {}
    for line in text.splitlines():
        if line.startswith(("//", "#")) or "=" not in line or ":" not in line:
            continue
        key_with_type, value = line.split("=", 1)
        key, _type = key_with_type.split(":", 1)
        options[key] = value

    expected = {
        **REQUIRED,
        "ECU_BUILD_LINUX_V2_PLATFORM":
            "ON" if args.system == "Linux" else "OFF",
    }
    issues = []
    for key, value in expected.items():
        observed = options.get(key, "<MISSING>")
        if observed != value:
            issues.append(f"{key}: expected {value}, got {observed}")
    root = Path(__file__).resolve().parents[1]
    if Path(options.get("CMAKE_HOME_DIRECTORY", "/__missing__")).resolve() != root:
        issues.append("CMake project source directory mismatch")
    if args.system != "Linux":
        # On non-Linux, no Linux SocketCAN or Linux V2 implementation may be
        # part of the generated build targets.
        cmake_targets = build_dir / "CMakeFiles" / "TargetDirectories.txt"
        if cmake_targets.exists():
            sources = cmake_targets.read_text(encoding="utf-8")
            if "/src/platform/linux/" in sources.replace("\\", "/"):
                issues.append("Linux platform target leaked into non-Linux graph")
    if issues:
        print("V2_DEFAULT_BUILD_GATE=FAIL", file=sys.stderr)
        for issue in issues:
            print(f"V2_DEFAULT_BUILD_ISSUE={issue}", file=sys.stderr)
        return 1
    print("V2_DEFAULT_BUILD_GATE=PASS")
    print(f"V2_DEFAULT_BUILD_SYSTEM={args.system}")
    for key, value in expected.items():
        print(f"{key}={value}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
