#!/usr/bin/env python3
"""Fail-closed validation of the scoped Core V2 TRUCK/AGRI/OHV conformance claim."""

import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = pathlib.Path(
    os.environ.get(
        "ECU_CORE_V2_CONFORMANCE_MANIFEST",
        str(ROOT / "docs" / "CORE_V2_FOUNDATION_CONFORMANCE.json"),
    )
).resolve()

EXPECTED_DOMAINS = {"truck", "agri", "ohv"}
REQUIRED_BASELINES = {
    "ISO 11898-1:2024",
    "ISO 11898-2:2026",
    "SAE J1939/21_202205",
    "ISO 11783-3:2026",
    "ISO 11783-12:2019",
    "ISO 15765-2:2024",
    "ISO 25119-1:2018",
    "ISO 19014-1:2018",
}
REQUIRED_EXCLUSIONS = {
    "electrical/physical CAN transceiver conformance",
    "J1939 PGN semantics, Address Claiming, TP/ETP and diagnostics",
    "ISO 11783/ISOBUS application, transport, network and diagnostic services",
    "ISO-TP/DoCAN protocol conformance",
    "UDS protocol conformance",
    "DoIP protocol conformance",
    "product functional-safety lifecycle compliance or certification",
}
ALLOWED_CLAIM_TYPES = {"implemented", "boundary"}


def fail(message: str) -> None:
    print(f"CORE_V2_FOUNDATION_CONFORMANCE=FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def checked_path(relative: str) -> pathlib.Path:
    candidate = (ROOT / relative).resolve()
    try:
        candidate.relative_to(ROOT)
    except ValueError:
        fail(f"evidence path escapes repository: {relative}")
    if not candidate.is_file():
        fail(f"missing evidence file: {relative}")
    return candidate


def verify_markers(ref: dict, requirement_id: str) -> None:
    path_value = ref.get("path")
    markers = ref.get("markers")
    if not isinstance(path_value, str) or not path_value:
        fail(f"{requirement_id}: evidence path missing")
    if not isinstance(markers, list) or not markers or not all(
        isinstance(marker, str) and marker for marker in markers
    ):
        fail(f"{requirement_id}: markers missing for {path_value}")

    text = checked_path(path_value).read_text(encoding="utf-8")
    for marker in markers:
        if marker not in text:
            fail(f"{requirement_id}: marker not found in {path_value}: {marker}")


def main() -> None:
    try:
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"manifest unreadable: {error}")

    if data.get("schema_version") != 1:
        fail("unsupported schema_version")
    if data.get("status") != "PASS":
        fail("manifest status is not PASS")
    if data.get("certification_claimed") is not False:
        fail("foundation must not claim external certification")

    scope = data.get("scope")
    if not isinstance(scope, dict):
        fail("scope missing")
    if set(scope.get("domains", [])) != EXPECTED_DOMAINS:
        fail("product scope must be exactly TRUCK/AGRI/OHV")
    exclusions = set(scope.get("excluded_and_module_gated", []))
    if not REQUIRED_EXCLUSIONS.issubset(exclusions):
        fail("required module-gated exclusions are incomplete")

    baselines = data.get("normative_baseline")
    if not isinstance(baselines, list):
        fail("normative_baseline missing")
    baseline_ids = {
        entry.get("id")
        for entry in baselines
        if isinstance(entry, dict)
    }
    if not REQUIRED_BASELINES.issubset(baseline_ids):
        fail("required normative baseline is incomplete")
    for entry in baselines:
        if not isinstance(entry, dict):
            fail("invalid normative baseline entry")
        if not entry.get("official_url", "").startswith("https://"):
            fail(f"{entry.get('id')}: official_url missing")
        if entry.get("applicability") not in {
            "applicable_subset",
            "boundary_only",
            "profile_boundary",
            "module_gated",
            "process_boundary",
        }:
            fail(f"{entry.get('id')}: invalid applicability")

    requirements = data.get("requirements")
    if not isinstance(requirements, list) or not requirements:
        fail("requirements missing")

    seen = set()
    referenced_norms = set()
    for req in requirements:
        if not isinstance(req, dict):
            fail("invalid requirement entry")
        req_id = req.get("id")
        if not isinstance(req_id, str) or not req_id:
            fail("requirement id missing")
        if req_id in seen:
            fail(f"duplicate requirement id: {req_id}")
        seen.add(req_id)

        if req.get("status") != "PASS":
            fail(f"{req_id}: status is not PASS")
        claim_type = req.get("claim_type")
        if claim_type not in ALLOWED_CLAIM_TYPES:
            fail(f"{req_id}: invalid claim_type")

        domains = set(req.get("domains", []))
        if not domains or not domains.issubset(EXPECTED_DOMAINS):
            fail(f"{req_id}: invalid domains")

        normative_refs = req.get("normative_refs", [])
        if not isinstance(normative_refs, list):
            fail(f"{req_id}: normative_refs must be a list")
        unknown = set(normative_refs) - baseline_ids
        if unknown:
            fail(f"{req_id}: unknown normative refs: {sorted(unknown)}")
        referenced_norms.update(normative_refs)

        if claim_type == "implemented":
            code_refs = req.get("code_refs")
            test_refs = req.get("test_refs")
            if not isinstance(code_refs, list) or not code_refs:
                fail(f"{req_id}: implemented requirement lacks code evidence")
            if not isinstance(test_refs, list) or not test_refs:
                fail(f"{req_id}: implemented requirement lacks test/gate evidence")
            for ref in code_refs + test_refs:
                verify_markers(ref, req_id)
        else:
            evidence_refs = req.get("evidence_refs")
            if not isinstance(evidence_refs, list) or not evidence_refs:
                fail(f"{req_id}: boundary requirement lacks evidence")
            for ref in evidence_refs:
                verify_markers(ref, req_id)

    applicable_or_boundary = {
        entry["id"]
        for entry in baselines
        if entry["applicability"] in {
            "applicable_subset",
            "boundary_only",
            "profile_boundary",
            "process_boundary",
        }
    }
    if not applicable_or_boundary.issubset(referenced_norms):
        missing = sorted(applicable_or_boundary - referenced_norms)
        fail(f"baseline entries have no mapped requirement: {missing}")

    trace_doc = checked_path("docs/CORE_STANDARDS_TRACEABILITY.md").read_text(
        encoding="utf-8"
    )
    required_markers = (
        "CORE_STANDARDS_BASELINE=PASS",
        "CORE_V2_FOUNDATION_STANDARDS_CONFORMANCE=PASS",
        "CORE_STANDARDS_CONFORMANCE=PASS",
        "CORE_PROTOCOL_CONFORMANCE_POLICY=MODULE_GATED",
    )
    for marker in required_markers:
        if marker not in trace_doc:
            fail(f"traceability status marker missing: {marker}")

    print(f"CORE_V2_CONFORMANCE_REQUIREMENTS={len(requirements)}")
    print("CORE_V2_PRODUCT_SCOPE=TRUCK,AGRI,OHV")
    print("CORE_V2_FOUNDATION_CONFORMANCE=PASS")


if __name__ == "__main__":
    main()
