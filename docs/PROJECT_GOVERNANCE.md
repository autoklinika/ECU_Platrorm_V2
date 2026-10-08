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

## 4. WebGUI is strictly an untrusted client

WebGUI (including the CM5 touchscreen kiosk) is a presentation-only client. It cannot directly control hardware, CAN, diagnostic sessions, power/ignition/wake, system processes, local privileged agents, configuration, databases or authoritative Bench/DUT state.

Only authenticated, authorized, versioned requests through the ECU Platform API may convey user intent. Application/Bench/CORE remain the sole owners of execution, resource arbitration, watchdogs, safety interlocks and safe-stop. Localhost or physical co-location with the CM5 never grants additional authorization.

The kiosk browser and privileged Bench backend must run under **separate OS identities** with no direct IPC or hardware-control path. The existing Stage D kiosk identity is a known pre-WebGUI security-isolation gap and must be migrated and revalidated before publishing actual WebGUI operations.

**Mandatory design, backend and deployment gates:** [WEBGUI_CLIENT_ONLY_ARCHITECTURE.md](WEBGUI_CLIENT_ONLY_ARCHITECTURE.md). No framework or feature decision may relax them without explicit user approval.

## 5. Enforcement

These rules are release/merge gates.

A change that violates project isolation or bypasses the explicit production-main approval gate must not be merged, even if technically correct or tests pass.
