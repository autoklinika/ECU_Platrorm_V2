# ECU Platform V2 — WebGUI is a client only

Decision: **ACCEPTED / NON-NEGOTIABLE**

Date: 2026-10-08

Scope: Every WebGUI deployment, including the local CM5 touchscreen kiosk, desktop browser, tablet, and remote browser.

Owner: ECU Platform V2 project.

Implementation: **not started by this document**. This is a mandatory architecture/security gate.

## 1. Non-negotiable invariant

**WebGUI is an untrusted, presentation-only client. It is never a controller, executor, resource owner, diagnostic stack, or safety authority.**

The client may render authorized data, collect user input, and transmit *intent* using a documented, authenticated ECU Platform API. The client has **zero direct authority** to alter hardware, OS configuration, process state, persistent platform data, diagnostic sessions, safety logic, or bus traffic.

A user clicking a button is **not** authorization to execute a command. The authoritative backend independently decides whether the request is allowed, valid in the current state, and safe to execute.

The GUI may update only its own transient visual presentation (navigation, form drafts, filters). Browser state, offline cache, and client-side validation are **never authoritative** for the state of the DUT, Bench session, hardware, or security policy.

## 2. Required authority chain

```text
WebGUI / CM5 kiosk (untrusted visual client)
    | HTTP API request: query or high-level intent
    | SSE/WebSocket: server-published state/telemetry only
    v
ECU Platform API boundary
    - authentication, authorization, CSRF/origin checks, schema validation
    - API operation allowlist, session binding, rate/admission limits
    - auditing, idempotency and fail-closed result handling
    v
Application Layer (authoritative domain operations and state)
    - checks DUT capabilities, session ownership, required confirmations
    v
Bench Runtime / DUT Profile
    - exclusive resource leases, watchdog, interlocks, safe-stop
    - DUT-specific services and bounded cyclic programs
    v
CORE V2 / hardware platform adapters
    - CAN/CAN-FD, ISO-TP, UDS, J1939, GPIO/power adapters as applicable
    v
Connected DUT (TRUCK / AGRI / OHV; ECU, EGR, VGT, sensor, ...)
```

The API is **not** a transparent proxy to a CAN socket, process shell, USB device, privileged daemon, or arbitrary method invocation. Transport and protocol execution belong below the domain API. No alternate direct path is allowed because a browser happens to run on the CM5.

The UI does not know how to construct a UDS service, a cyclic actuator CAN frame, an EEPROM write, or an output-control sequence. It never implements real-time scheduling, TX pacing, interlocks, watchdogs or neutralization.

## 3. Explicitly forbidden from WebGUI or kiosk browser process

- Direct access to `can0`, SocketCAN, raw CAN, CAN-FD, ISO-TP/UDS, J1939 packets, J2534, serial/RS485, diagnostic hardware or bus transmit queues.
- Direct control of power, ignition, wake, GPIO, relays, drivers, EGR/VGT or any physical DUT output.
- Shell/terminal execution, SSH, sudo, systemd service controls, arbitrary subprocesses, arbitrary host files, USB/WebUSB/WebSerial devices or a generic command-execution endpoint.
- Calls to the privileged Bench agent's Unix socket or any equivalent local IPC bypassing the public authenticated API.
- Direct database access, authoritative session files, capture archives, backups, credentials, security policy stores or configuration mutation.
- Deciding whether a DUT is safe, forcing runtime session transitions, issuing erase/reset/flash/output tests or clearing interlocks.
- Holding long-lived, independent control loops, cyclic frame timers or an alternative resource manager in frontend code.
- Direct dependency/import/link on CORE V2, Bench Runtime, DUT Profile implementations or platform driver libraries.

The exact technical API method, authentication scheme and GUI framework may be selected in a later stage. **No selection changes these prohibitions.**

## 4. API-mediated interactions

- **Queries:** the API supplies authenticated, versioned DTOs/snapshots, operation capabilities, measurements, DTC inventories, lifecycle status and redacted diagnostic metadata. The GUI renders them; it does not reconstruct domain truth.
- **User intents:** the GUI may ask the API for high-level operations such as `identify DUT`, `read DTC`, `start authorized lab test`, `stop` or `recover`. A request is not a CAN frame and contains no arbitrary CAN ID, raw payload, driver name or executable script.
- **Backend authority:** the API and Application Layer validate operator identity, scopes, DUT/profile capability, physical resource ownership, current session state, command preconditions, interlocks and required approvals. Unsafe/unavailable operations are denied even if a forged UI sends the request.
- **Durability and ordering:** server-assigned request/session IDs, idempotency and sequence handling prevent duplicate execution on network retries. Browser refresh, disconnection or lost focus never bypasses server-side watchdog/safe-stop.
- **Destructive/high-energy operations:** initially unavailable in GUI 1.0. Any future implementation needs a separate explicit project decision, backend enforcement, operator confirmation, immutable audit and physical safety prerequisites; hiding a button is not protection.
- **Telemetry:** one-way server-published read-only event streams or polling. UI feedback and optimistic updates never replace a confirmed backend result.

The same contracts apply to 250/500 kbit/s SAC, future EGR/VGT cyclic-control devices, sensors, and other laboratory DUT classes. The UI renders **server-reported capabilities**, not hard-coded OEM protocol behavior.

## 5. Separate security identities — REQUIRED BEFORE REAL WEBGUI

Physical co-location is not a trust boundary. The local kiosk gets **no implicit privilege** merely because it uses `localhost`.

### Existing deployment finding (2026-10-08)

The current Stage D kiosk is technically working: Cage and Chromium run on CM5, with an automatically launched placeholder webpage and a previously verified WaveShare touchscreen.

**However, its current Linux service runs as user `ecu`.** The permanent privileged Bench agent listens at `/run/ecu-platform-v2-bench/request.sock`, owned `root:ecu`, permissions `0660`, with parent directory `root:ecu`, permissions `0750`. The kiosk process therefore shares the same OS user/group identity that can access this restricted local IPC endpoint. A compromised browser process could cross the intended client-only boundary.

This is an **existing deployment isolation gap**; it is NOT proof of an observed intrusion or of JavaScript itself having Unix-socket access.

The kiosk is **Stage D display/touch PASS, but WebGUI security isolation NOT YET PASS**. Do not expose privileged operations to this browser as-is.

### Required change before connecting WebGUI

1. Run Cage/Chromium under a **dedicated, non-privileged kiosk OS identity** (working name `ecu-kiosk`), different from the Bench operator and all privileged backend identities.
2. Kiosk identity must have **no access** to the Bench-agent Unix socket, the `ecu` operator group, `sudo`, `dialout`, `spi`, `i2c`, `gpio`, CAN interfaces, service-management interfaces or protected project/evidence directories. Grant only the minimum DRM/Wayland/touchscreen access needed to display and interact with the UI; validate that these display permissions cannot be reused for DUT IO.
3. Keep Chromium sandbox enabled. Harden the kiosk service with suitable systemd restrictions (including `NoNewPrivileges`, dedicated writable browser-profile state and filesystem/device limitations), tested against DRM/logind/Wayland functionality. Do not break the validated HDMI/touch path.
4. Run the authenticated API/service under a **separate backend identity**, with a narrow, audited application-command channel to a resource-owning worker/broker. The existing agent must not become an unrestricted backend for arbitrary GUI commands.
5. Enforce authentication and authorization for both local and remote API clients; no `127.0.0.1` trust bypass. Where cookies are used, enforce CSRF protection and safe origin policy. Remote access must be explicitly authenticated and protected; no anonymous bind on public interfaces.
6. Keep the kiosk URL and deployment settings externally configurable as in Stage D, without running a privileged Chromium or exposing privileged filesystem paths through HTTP static serving.

**Do not alter the currently working kiosk service as part of this architecture-only decision.** Its user/permission migration needs an isolated, approved deployment change with console fallback and post-reboot touch validation.

## 6. Separation of release gates

### Architecture/design gate (this decision)

- GUI is presentation-only; the sole operational input path is the authenticated API.
- API is a domain-specific authorization and command boundary, not a shell/hardware proxy.
- Core, Bench Runtime, DUT Profile and CAN/actuator scheduling stay unchanged merely because of UI requirements.
- No coupling to a specific DUT or vendor, no reimplementation of Bench state machines.
- New API capabilities are versioned and documented; backend is responsible for result authority.

### Backend API gate (before exposing operations)

- Negative tests for unauthorized, missing-session, stale, duplicate, malformed, cross-DUT and unsupported requests — denied without side effects.
- Bench lease acquisition/release, watchdog, stop and recovery tested independently of browser lifetime.
- API trace/audit correlates operator intent to backend action, error and cleanup (without publishing VIN or raw sensitive traces).
- Read-only whitelist for initial GUI 1.0, with destructive actions explicitly unavailable and tested as such.
- Fail-closed behavior on disconnect, restart, timeout, bus-off or interlock failure.

### Kiosk deployment gate (before setting the real WebGUI URL)

- Dedicated unprivileged kiosk account configured; no membership in Bench-agent or hardware-control groups.
- From the kiosk identity: attempted access to the Bench-agent socket, CAN control interface and protected evidence files fails; from the trusted backend: only authorized API operations succeed.
- Existing `STAGE_D_KIOSK=PASS` reconfirmed with dedicated identity.
- Kiosk auto-start/recovery after reboot, fullscreen Wayland rendering and WaveShare physical touch PASS.
- Remote normal-browser access obeys the **identical** authorization policy.
- A compromised/forged client request cannot cross into unmediated device control.

Only then can the GUI/kiosk be accepted as a strictly client-only ECU Platform interface. These tests must be actual executed gates, not assumptions based on a browser UI.

## 7. Change management

This ADR is ECU Platform-specific and must not import assumptions from other independent projects. Changes to `main` remain under the user's explicit approval (see `PROJECT_GOVERNANCE.md`). Any proposal to weaken this boundary, add raw bus transmit facilities, allow direct hardware access from a browser, or use a privileged kiosk identity requires a new explicit user architecture decision. Do not silently relax these guarantees during feature work.
