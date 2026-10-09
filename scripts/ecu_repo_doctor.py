#!/usr/bin/env python3
"""ECU Platform V2 repository/workflow diagnostics (read-only by design).

No checkout, fetch, reset, branch deletion, CAN access, service writes or
release/production actions. Python 3.10+, all operating systems.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any

REPO = Path(__file__).resolve().parents[1]
SAC_PROTECTED = ("main", "production")
ALLOWED_SCOPE = ("repo", "webgui", "api", "sac", "all")
CLI_TIMEOUT_SECONDS = 12
CHECK_TIMEOUT_SECONDS = 150


def cmd(argv: list[str], root: Path = REPO, timeout: int = CLI_TIMEOUT_SECONDS
        ) -> subprocess.CompletedProcess[str]:
    """Capture output without exposing secrets, never use shell or sudo."""
    try:
        return subprocess.run(argv, cwd=root, text=True, capture_output=True,
                              timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        return subprocess.CompletedProcess(argv, 127, "", type(error).__name__)


def git(root: Path, *parts: str) -> str:
    result = cmd(["git", *parts], root)
    if result.returncode != 0:
        raise RuntimeError("Git command failed: " + " ".join(parts) +
                           " (" + (result.stderr.strip()[:180] or "unavailable") + ")")
    return result.stdout.strip()


def worktrees(root: Path) -> list[dict[str, str]]:
    records: list[dict[str, str]] = []
    current: dict[str, str] = {}
    for line in git(root, "worktree", "list", "--porcelain").splitlines() + [""]:
        if not line:
            if "path" in current:
                records.append(current)
            current = {}
        elif line.startswith("worktree "):
            current["path"] = line[len("worktree "):]
        elif line.startswith("HEAD "):
            current["head"] = line[5:17]
        elif line.startswith("branch "):
            current["branch"] = line[7:].removeprefix("refs/heads/")
        elif line == "detached":
            current["branch"] = "(detached)"
        elif line.startswith("locked"):
            current["locked"] = "yes"
        elif line.startswith("prunable"):
            current["prunable"] = "yes"
    return records


def origin_identity(root: Path) -> str:
    remote = git(root, "remote", "get-url", "origin")
    match = re.search(
        r"(?:github[.]com[:/])([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:[.]git)?$",
        remote)
    if not match:
        raise RuntimeError("Origin must be a recognized GitHub repository")
    return match.group(1)


def discover_checkout_paths(root: Path, tracked: list[dict[str, str]],
                            include_siblings: bool = False) -> list[dict[str, str]]:
    paths = {x["path"] for x in tracked}
    found = [dict(x, source="worktree") for x in tracked]
    if include_siblings:
        try:
            origin = origin_identity(root).lower()
        except RuntimeError:
            return found
        for sibling in root.parent.glob("ECU_*"):
            if not sibling.is_dir() or str(sibling) in paths:
                continue
            if not (sibling / ".git").exists():
                continue
            try:
                if origin_identity(sibling).lower() != origin:
                    continue
                found.append({"path": str(sibling),
                              "branch": git(sibling, "branch", "--show-current"),
                              "head": git(sibling, "rev-parse", "--short=12", "HEAD"),
                              "source": "separate-clone"})
            except RuntimeError:
                continue
    for item in found:
        path = Path(item["path"])
        result = cmd(["git", "status", "--porcelain", "--untracked-files=normal"], path)
        item["clean"] = result.returncode == 0 and not result.stdout.strip()
        item["dirty_entries"] = (
            len(result.stdout.splitlines()) if result.returncode == 0 else None)
    return sorted(found, key=lambda x: x["path"])


def parse_prs(raw: str) -> list[dict[str, Any]]:
    prs = json.loads(raw)
    if not isinstance(prs, list):
        raise ValueError("Expected a list of open pull requests")
    required = ("number", "headRefName", "baseRefName", "isDraft", "title")
    for pr in prs:
        if not isinstance(pr, dict) or any(key not in pr for key in required):
            raise ValueError("Unrecognized PR record")
    return prs


def pr_chains(prs: list[dict[str, Any]]) -> list[list[dict[str, Any]]]:
    """Group draft PR dependencies by their real base branch, not PR number."""
    by_head = {p["headRefName"]: p for p in prs}
    children: dict[str, list[dict[str, Any]]] = {}
    for pr in prs:
        children.setdefault(pr["baseRefName"], []).append(pr)
    for items in children.values():
        items.sort(key=lambda p: p["number"])
    result: list[list[dict[str, Any]]] = []
    included: set[int] = set()

    def follow(node: dict[str, Any], chain: list[dict[str, Any]]) -> None:
        if node["number"] in included:
            return
        included.add(node["number"])
        next_chain = [*chain, node]
        next_nodes = children.get(node["headRefName"], [])
        if not next_nodes:
            result.append(next_chain)
        else:
            for child in next_nodes:
                follow(child, next_chain)

    roots = sorted((p for p in prs if p["baseRefName"] not in by_head),
                   key=lambda p: p["number"])
    for root in roots:
        follow(root, [])
    # Broken dependency/cyclic chain: still show unresolved PR rather than hide.
    for orphan in sorted(prs, key=lambda p: p["number"]):
        follow(orphan, [])
    return result


def load_prs(root: Path) -> tuple[list[dict[str, Any]] | None, str | None]:
    if not shutil.which("gh"):
        return None, "gh CLI unavailable (offline report)"
    try:
        origin = origin_identity(root)
    except RuntimeError as error:
        return None, str(error)
    response = cmd(["gh", "pr", "list", "--repo", origin, "--state", "open",
                    "--limit", "100", "--json",
                    "number,headRefName,baseRefName,isDraft,title"], root, 35)
    if response.returncode != 0:
        return None, "GitHub listing unavailable; no network/credentials needed for local checks"
    try:
        return parse_prs(response.stdout), None
    except (json.JSONDecodeError, ValueError):
        return None, "Invalid GitHub response"


def cm5_snapshot(root: Path) -> dict[str, Any]:
    if not sys.platform.startswith("linux"):
        return {"available": False, "reason": "non-linux"}
    if cmd(["hostname", "-s"], root).stdout.strip() != "ecu":
        return {"available": False, "reason": "not-CM5"}
    services = ["ecu-api-v1", "ecu-sac-connect-v1", "ecu-webgui-static",
                "ecu-kiosk", "ecu-platform-v2-bench-agent"]
    states = {}
    for name in services:
        result = cmd(["systemctl", "is-active", name], root, 3)
        states[name] = result.stdout.strip() or "unknown"
    link = cmd(["ip", "-o", "link", "show", "can0"], root, 3)
    deployed = Path("/opt/ecu-platform/webgui/current")
    return {"available": True, "services": states,
            "can0_up": bool(re.search(r"<[^>]*\\bUP\\b", link.stdout)),
            "can0_state": "UNKNOWN" if link.returncode else (
                "UP" if re.search(r"<[^>]*\\bUP\\b", link.stdout) else "DOWN"),
            "webgui_release": (os.readlink(deployed) if deployed.is_symlink()
                               else "unavailable")}


def main_ref_snapshot(root: Path) -> str:
    # GitHub Actions checks out only one shallow commit by default.
    # A missing origin/main is not an invalid repository and must never
    # be presented as a guessed production revision or trigger a fetch.
    result = cmd(["git", "rev-parse", "--verify", "--short=12",
                  "refs/remotes/origin/main"], root)
    if result.returncode == 0 and result.stdout.strip():
        return result.stdout.strip()
    return "unavailable (shallow/missing local origin/main)"


def snapshot(root: Path, *, github: bool = False,
             include_siblings: bool = False, cm5: bool = False) -> dict[str, Any]:
    tree = worktrees(root)
    local = discover_checkout_paths(root, tree, include_siblings)
    data: dict[str, Any] = {
        "origin": origin_identity(root),
        "main_local": main_ref_snapshot(root),
        "repo_branch": git(root, "branch", "--show-current") or "(detached)",
        "checkouts": local,
        "git_worktrees": len(tree),
        "checkout_count": len(local),
        "dirty_count": sum(not entry["clean"] for entry in local),
    }
    if github:
        prs, error = load_prs(root)
        data["open_prs"] = prs
        if error:
            data["github_note"] = error
        if prs is not None:
            data["pr_chains"] = [[p["number"] for p in chain]
                                 for chain in pr_chains(prs)]
    if cm5:
        data["cm5"] = cm5_snapshot(root)
    return data


def print_report(data: dict[str, Any]) -> None:
    print("ECU V2 REPOSITORY DOCTOR — READ ONLY")
    print("Origin:", data["origin"])
    print("Production main (local remote ref):", data["main_local"],
          "— never merge without operator approval")
    print("Current branch:", data["repo_branch"])
    print("Checkouts:", data["checkout_count"], "(Git worktrees:",
          data["git_worktrees"], "), dirty:", data["dirty_count"])
    for item in data["checkouts"]:
        indicator = "CLEAN" if item["clean"] else "DIRTY"
        print(" ", indicator, item["branch"], item["head"],
              "[" + item["source"] + "]", item["path"])
    if "open_prs" in data:
        prs = data["open_prs"]
        if prs is None:
            print("GitHub:", data.get("github_note", "unavailable"))
        else:
            print("Open PRs:", len(prs), "(draft:",
                  sum(bool(p["isDraft"]) for p in prs), ")")
            lookup = {p["number"]: p for p in prs}
            for chain in data["pr_chains"]:
                labels = ["#" + str(number) for number in chain]
                base = lookup[chain[0]]["baseRefName"]
                print("  PR TRAIN", base, "→", " → ".join(labels))
    if "cm5" in data:
        state = data["cm5"]
        if state["available"]:
            print("CM5 deployed WebGUI:", state["webgui_release"])
            print("CM5 can0:", state["can0_state"], "(observed only)")
            for service, status in state["services"].items():
                print("  ", service, status)
        else:
            print("CM5 snapshot:", state["reason"])
    print("No changes were made. No hardware commands executed.")


def check_offline(root: Path, scope: str) -> int:
    tasks: list[tuple[str, list[str]]] = []
    if scope in ("repo", "all") and (root / "tests/test_repo_doctor.py").exists():
        tasks.append(("Repository doctor unit tests",
                      [sys.executable, "-m", "unittest", "discover", "-s", "tests",
                       "-p", "test_repo_doctor.py", "-q"]))
    if scope in ("webgui", "sac", "all"):
        if (root / "webgui/tests").exists():
            suite = sorted(str(x) for x in (root / "webgui/tests").glob("*.test.mjs"))
            if suite:
                tasks.append(("WebGUI Node tests", ["node", "--test", *suite]))
        if (root / "tests").exists():
            for pattern in ("test_webgui*.py", "test_sac_connect_adapter.py",
                            "test_sac_connect_install_policy.py",
                            "test_sac_kiosk_proxy.py"):
                if any((root / "tests").glob(pattern)):
                    tasks.append((pattern, [sys.executable, "-m", "unittest",
                                            "discover", "-s", "tests", "-p", pattern, "-q"]))
    if scope in ("api", "all"):
        if (root / "tests/api").exists():
            tasks.append(("Application API static tests",
                          [sys.executable, "-m", "unittest", "discover",
                           "-s", "tests/api", "-p", "*test.py", "-q"]))
    if not tasks:
        print("No matching offline test suites on this branch.")
        return 2
    failures = 0
    for title, argv in tasks:
        if shutil.which(argv[0]) is None and argv[0] != sys.executable:
            print("SKIP", title, "required runtime unavailable:", argv[0])
            failures += 1
            continue
        result = cmd(argv, root, CHECK_TIMEOUT_SECONDS)
        if result.returncode != 0:
            print("FAIL", title, "code", result.returncode)
            print((result.stdout + "\n" + result.stderr)[-3000:])
            failures += 1
        else:
            print("PASS", title)
    print("READ-ONLY-OFFLINE-CHECK=", "PASS" if failures == 0 else "FAIL",
          "(nothing transmitted to ECU)", sep="")
    return int(failures != 0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("status", "check"))
    parser.add_argument("--root", type=Path, default=REPO)
    parser.add_argument("--github", action="store_true",
                        help="include live open PR dependency trains via gh CLI")
    parser.add_argument("--siblings", action="store_true",
                        help="include matching separate Git clones in sibling ECU_* directories")
    parser.add_argument("--cm5", action="store_true",
                        help="read CM5 service, CAN UP/DOWN and GUI release status")
    parser.add_argument("--json", action="store_true", help="machine-readable status")
    parser.add_argument("--strict", action="store_true",
                        help="nonzero status exit code when any checkout is dirty")
    parser.add_argument("--scope", choices=ALLOWED_SCOPE, default="all",
                        help="offline check suite (never hardware tests)")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        if args.action == "check":
            return check_offline(root, args.scope)
        report = snapshot(root, github=args.github, include_siblings=args.siblings,
                          cm5=args.cm5)
        if args.json:
            print(json.dumps(report, ensure_ascii=False, indent=2))
        else:
            print_report(report)
        return 1 if args.strict and report["dirty_count"] else 0
    except RuntimeError as error:
        print("REPOSITORY_DOCTOR=FAIL", error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
