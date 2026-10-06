# ECU Platform V2 — Project Governance

Status: **mandatory / non-negotiable**

This document defines project-level operating rules. These rules are stronger than convenience, implementation speed, or temporary workflow preferences.

## 1. Project isolation

ECU Platform V2 is an independent project boundary.

Do not import, reuse, infer, or apply code, data, architecture, workflow, naming, decisions, assumptions, or implementation patterns from any other user project unless the user explicitly requests that specific cross-project reuse for ECU Platform.

Legacy `autoklinika/ecu_platform` is considered part of the ECU Platform lineage and may be used as a read-only source of validated facts, evidence and implementation references. It is not an automatic architecture template for V2.

Cross-project analogies may be discussed, but they do not become ECU Platform requirements or implementation decisions without explicit user approval.

## 2. Production main gate

The production `main` branch is user-controlled.

No merge, squash merge, rebase merge, fast-forward, direct commit, cherry-pick, release merge, or equivalent write into production `main` may be performed without explicit user approval for that concrete change set.

Approval to:
- investigate,
- implement,
- test,
- create a branch,
- prepare a PR,
- fix CI,
- update documentation,

does **not** imply approval to merge into production `main`.

Work should be prepared on branches and presented for review. The final merge decision belongs to the user.

## 3. Architectural isolation

Core, transport abstractions, device abstractions, WebGUI/API contracts and security boundaries must remain specific to ECU Platform requirements.

If a dependency from another project appears in ECU Platform code or documentation without explicit authorization, treat it as a project-boundary violation and remove or isolate it before merge.

## 4. Enforcement

These rules are release/merge gates.

A change that violates project isolation or bypasses the explicit production-main approval gate must not be merged, even if technically correct or tests pass.
