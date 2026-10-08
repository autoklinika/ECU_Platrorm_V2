# Application API V1 — read-only foundation

Status: isolated API branch; **not merged or deployed**. Kiosk/WebGUI PR #19 is untouched.

## Architectural ownership

WebGUI -> authenticated Application API -> injected `IReadModel` -> application-owned snapshots -> Bench Runtime / DUT Profiles -> Core V2.

- C++17 `ecu_application_api`: portable DTOs, read-only provider interface, validation, versioned JSON router; no CAN hardware dependency. Cross-compiles for Generic targets without any HTTP transport.
- C++17 `ecu_api_http`: thin loopback-only HTTP/1.1 GET transport with Windows Winsock / Unix sockets. Not a general-purpose HTTP server, no arbitrary endpoints or proxy routes.
- Optional Linux read adapter: `query_socketcan_link` from **existing** Linux V2 adapter; queries kernel netlink without opening PF_CAN, modifying link state, transmitting frames or connecting to Bench Agent. Interface is explicit configuration (`--can-interface can0`).
- Bench projection: existing `BenchSessionSnapshot` and selected `DutProfileDefinition` are adapted without changing Core/Bench. The selected profile never implies physical DUT presence.
- The current privileged Bench Agent provides command operations, including CAN link configuration. **It must not be queried by this API**, even for its status operation.
- No authoritative cross-process source for live Bench session, selected DUT, capabilities, DTC, or platform runtime version is installed yet. Those endpoints return HTTP 503 rather than synthetic data. `/about` reports only this API binary's actual build revision.

No writes, TX, DTC clear, actuator control, ECU reset, sessions, power control, job execution, arbitrary CAN frames, generic socket relaying or dynamic method selection exist in the API.

## Endpoints

All paths use `/api/v1`; all responses are `application/json` with `schema_version: 1`, `Cache-Control: no-store`. GET requires `Authorization: Bearer <64 lowercase hex characters>` and `Host: 127.0.0.1:<API port>`.

| GET path | 200 data when authoritative provider available |
| --- | --- |
| `/about` | `api_version`, `read_only`, `build_revision` — running API binary only |
| `/platform` | backend-owned platform `product` and `version` |
| `/interfaces` | `interfaces[]`: name, up, bus-off, FD, listen-only, nominal/data bitrates |
| `/bench/session` | state, profile ID, revision, configured, cleanup required |
| `/dut` | **selected** profile ID and kind; physical presence unverified |
| `/dut/capabilities` | allowlisted **read-only** operations only |
| `/dut/dtcs` | protocol and published DTC entries/status masks from completed backend read |

200: `{"schema_version":1,"data":{...}}`.
4xx/5xx: `{"schema_version":1,"error":{"code":"..."}}`.

Status:
- 400 invalid schema/request, 401 missing/invalid bearer, 403 invalid Host/Origin, 404 unknown path or no active session;
- 405 forbidden HTTP method (some methods with forbidden request bodies return 400);
- 501 unsupported capability; 502 invalid backend snapshot; 503 backend unavailable.
- A missing backend is **not** an idle bus, no DUT, no faults, zero DTCs or an empty capability set.

The API follows request/response polling only. No stream, subscription, WebSocket or SSE is implemented.

## Security baseline

- Process listens on IPv4 `127.0.0.1` only, not `0.0.0.0`, and is designed to run as an independent `ecu-api` account with no `ecu` group membership, no device/capability grants and no access to `/run/ecu-platform-v2-bench/request.sock`.
- Always authenticate requests, including loopback. Exactly 64 lowercase hex characters of entropy must be generated with a CSPRNG (`openssl rand -hex 32`), stored in a protected file, never bundled in static JS/HTML and never committed.
- Strict Host check and only `Origin: http://127.0.0.1:8877`. CORS preflight permits only `GET` with `Authorization`; no cookies, `Access-Control-Allow-Credentials`, wildcards or writes. Header-based bearer auth avoids ambient cookie CSRF; there are no state-changing HTTP methods.
- Strict HTTP/1.1 small-header parser (8 KiB), bounded socket timeouts, one-request-per-connection, duplicate security headers and all request bodies rejected. Do not expose this narrow server to a LAN or Internet.
- Future GUI must provide an explicit operator authentication flow; retain token only in page memory, do not store in localStorage or expose via static assets. **Do not embed a permanent token to make the topbar auto-authenticate.**
- If token is absent/weak, service will fail startup. It never silently disables auth.
- The service unit in `deploy/api/` is a **deployment template**, not enabled by this change. It intentionally has no permission to read Bench Agent's privileged socket.

## Build and tests

```sh
cmake -S . -B build/api -G Ninja -DECU_BUILD_APPLICATION_API=ON
cmake --build build/api --target ecu_api_http ecu_application_api_tests
ctest --test-dir build/api -R '^ecu[.]api[.]' --output-on-failure
```

The ordinary production build graph is unchanged because `ECU_BUILD_APPLICATION_API` defaults OFF. CI additionally runs Linux GCC/Clang and Windows MSVC x64/Win32 Debug/Release. No physical DUT is needed.

## Second code audit and first SAC-oriented tests — 2026-10-08

- Native Bench state conversion now rejects unknown enum values, rather than silently mapping them to `faulted`. A configured, but unconfigured-phase snapshot is invalid.
- Strict bounds, UTF-8 validation, unique interfaces and read capabilities, and DTC identifier limits make malformed backend DTOs return `502 invalid_snapshot`.
- CAN response now explicitly carries `bus_off` from the existing Linux netlink adapter. A link that is DOWN with zero bitrate **does not mean this CAN-FD controller lacks capabilities or that an ECU is healthy**.
- Expanded HTTP negative tests: unsupported methods, duplicate Host/Origin, obsolete HTTP versions, untrusted origins and unauthenticated same-origin requests.
- Fixed a real HTTP lifecycle failure: immediate process restart on the same TCP port failed due to TIME_WAIT. Linux now uses SO_REUSEADDR before bind; Windows uses SO_EXCLUSIVEADDRUSE to prevent another local process from taking the listening address. Repeat-listener test is part of the Linux HTTP integration gate.
- Read-only smoke on CM5: authenticated API `/about` and kernel `/interfaces` succeed; `/bench/session` and `/dut/dtcs` return 503; missing Bearer returns 401; CAN state and RX/TX/error counters unchanged. This is **not** a new physical SAC DTC read.
- Previous **operator-validated** SAC 500k evidence `read-500k-20261008T081749Z-422826.active-uds.log` was reverified offline (SHA-256 `423cd78e64f6e797e22d6acaf742802c87d5d45737411a688baee06559dcb5ad`); 32 diagnostic frames, 0 parse errors, 13 DTC entries, 0 ClearDiagnosticInformation commands. Private raw trace is not committed.
- Physical test of attached SAC at 500k needs an operator with an interactive sudo terminal to bring CAN into listen-only/active modes. The Application API process neither has nor requests CAP_NET_ADMIN and must not perform this transition; the already validated Stage 4.3 proof runner is maintained separately, outside PR #20.

Remaining architecture gate for actual SAC -> API DTC: add an **authorized, freshness-checked application-owned read snapshot publisher** without exposing Bench Agent commands or letting HTTP dispatch diagnostic operations. Keep WebGUI disconnected until this gate is complete.

## Staged deployment procedure (not executed by this PR)

1. Confirm API-specific CI and existing Core/Bench/application gates are all green, and a clean kiosk/agent baseline.
2. On CM5, create *separate* `ecu-api` system user/group without `ecu` membership; verify explicitly that the identity cannot open Bench Agent socket.
3. Install a tested release binary and unit. Place a random 256-bit token at `/etc/ecu-platform-v2/api/token`, owner `root:ecu-api`, mode `0640`; directories readable only by the API service identity. Never echo it to logs.
4. Start only the API unit on port 8878; smoke `/about`, netlink CAN read, negative auth/origin/method tests, and verify kiosk and Bench Agent remain healthy. Do not alter can0 link settings.
5. WebGUI integration is a **separate, later GUI branch change**: operator auth plus real topbar state, only after endpoint gates. Until then, topbar must show explicitly unavailable, never synthetic ECU states.

Future stage: authorized read-only **application snapshot export** rather than bridging arbitrary Bench Agent calls. Design its freshness, ownership, privacy, permissions and failure semantics before publishing Bench/DUT/DTC data.

## SAC 500k evidence and typed API projection — 2026-10-08

Operator-controlled Stage 4.3 test (`all`) on a physically connected DAF SAC completed successfully (evidence prefix `read-500k-20261008T144954Z-6267`). The raw UDS trace SHA-256 was verified on CM5: `713f698150e06fa331b6e15d44deffa78abb64235313f0c7aa47734f6adb02f8`. Offline parsing: 32 diagnostic frames, 0 ISO-TP parse errors, 13 DTC entries, 0 UDS 0x14 clear requests, 0 positive clear acknowledgements. Controller cleaned up to `can0 DOWN`. No raw trace or VIN are included in this repository.

Confirmed profile: `0xDAF00050`, CAN Classic 500 kbit/s, UDS read-only. F190 supplied `UNPROGRAMMED_FF17` (not a VIN); F188/F192 identification succeeded. FE96 permanent/ignition both 28 V. PGN FEAE was observed but both pressure values remained **unavailable**, not 0 bar. DTC `statusAvailabilityMask=0x8B`, count 13.

API V1 now includes an **optional, separately linked C++ adapter** `ECU::api_daf_sac_projection`. It accepts a native, already completed `AppSnapshot` plus `SacDtcList` from Application Layer and emits generic, bounded `DtcInfo` with 6-digit UDS DTC codes, status bytes, requested and availability masks. It rejects incorrect 250k/500k profile pairings, incomplete or contradictory snapshots, invalid status bits, duplicate/out-of-range codes, missing completed-operation generation, mismatched Bench/DUT session identity, unfinished resource cleanup and invalid lifecycle states. Profile-supported read capabilities come from the existing DAF SAC operation catalog, excluding unsupported or unvalidated operations.

**Important boundary:** no HTTP handler calls this adapter directly against the physical ECU. It is a pure projection library, tested using explicitly synthetic native snapshot fixtures. The real Stage 4.3 completed result is operator-generated evidence with a verified SHA-256, but is **not** an authenticated live application session. The `/api/v1/dut/dtcs` endpoint must continue returning `503 backend_unavailable` until a controlled application-owned cross-process snapshot publisher/reader supplies origin, capture time, freshness, session identity and operator authorization. Never parse the private raw UDS trace inside the HTTP server; never grant the API access to privileged Bench Agent commands or devices.

## Application-owned completed readout IPC — Stage API V1.1

### Strict semantics

`GET /api/v1/readouts/dtc/latest` is a **completed historical operation**, not a
live DUT status query. The response contains `source:
"completed_application_operation"`, `live: false`, `captured_at_unix_ms`,
`profile_id`, `completed_generation` and `dtcs` (protocol, status availability
and requested masks, six-digit hexadecimal codes and their status bytes).
Readouts are accepted for 24 hours after capture; expired evidence returns
HTTP 410 `readout_expired`, missing evidence returns HTTP 503 and malformed or
insecure snapshots return HTTP 502. The original live
`/api/v1/dut/dtcs` endpoint still returns 503 until a separate, authorized
current-session provider exists. No automatic action is triggered by HTTP.

Transport: portable `ECU_COMPLETED_DTC_V1` canonical newline format
(maximum 8192 bytes and 128 DTC entries). Parsing is exact, rejects duplicates,
invalid masks and unknown fields, and preserves the UDS mask and 24-bit codes.
One Linux-specific adapter publishes a new immutable inode by atomic rename
after a native Application Layer operation reaches `dtcs_ready`, the Bench
resource leases are released, the matching profile is verified, and its
completion generation is nonzero. The companion reader checks file and
directory ownership, group, exact modes, symlinks, hard-link count and schema
before making it accessible through authenticated GET. It never invokes the
Bench Agent, opens PF_CAN or transmits CAN frames.

**Trust model:** Linux producer `ecu` is the trusted local lab operator;
reader `ecu-api` has a separate account and read-only membership in
`ecu-api-read`. The operating-system ownership boundary does not attest that
an operator-owned file came from a specific physical ECU; it guarantees that
the kiosk and arbitrary unauthenticated HTTP clients cannot overwrite or
inject these snapshots. Hardware proof remains separately auditable.

### Provisioning plan (requires operator's terminal and review)

This is **not executed** by the API development PR. Only after production
review, API/legacy CI PASS, and confirmation that CM5 `can0` is DOWN:

- Create unprivileged system accounts `ecu-api` and read-only group
  `ecu-api-read`. The `ecu-api` account must **never** be a member of the
  privileged `ecu`, `gpio`, `spi`, `dialout` or `input` groups.
- Create `/var/lib/ecu-platform-v2/api-readouts` owned
  `ecu:ecu-api-read`, mode **2750** (setgid); the parent
  `/var/lib/ecu-platform-v2` is root-owned, mode 0755. The published file
  `dtc-latest.v1` is mode **0640**, owned `ecu:ecu-api-read`.
- Generate a 256-bit random bearer token *on the host* under
  `/etc/ecu-platform-v2/api/token` (root:`ecu-api`, mode 0640), never in Git,
  HTML, JS or shell history. Keep parent credential directory inaccessible
  to other accounts.
- Install a tested `ecu_api_http` binary and the staged
  `deploy/api/ecu-api-v1.service`, which runs with
  `User=ecu-api`, `Group=ecu-api`, `SupplementaryGroups=ecu-api-read`,
  empty Linux capabilities and an IPv4 loopback listener. Check the service
  cannot connect to the restricted Bench Agent socket.
- First smoke `/api/v1/about`, `/api/v1/interfaces` and the negative
  authentication cases. **No live ECU request through API.**

The operator's **physical SAC 500k proof runner** accepts the optional
`all --publish-readout` flag. Without it, it runs unchanged. With it,
the probe must come from a separate build with
`-DECU_BUILD_APPLICATION_API=ON` (build target
`ecu_daf_sac_stage42_read_probe` into `build/api-readout-linux`).
Publishing is attempted only after a successful *read-only* native DTC
operation; the generated evidence remains under the operator's control.
The flag explicitly requires the pre-provisioned group-private directory
and is rejected before CAN activity if that directory is missing.

This PR does **not** use that opt-in flag on the physical SAC: the existing
operator's successful Stage 4.3 trace stays private and is not silently
re-imported or relabeled as a fresh Application Layer snapshot. The local
end-to-end HTTP test uses an explicitly synthetic publisher fixture, including
negative checks for file modes, symlinks, malformed content, expired results,
no authorization and no confusion with live `/dtcs`.

No WebGUI changes, production merge, installed user or enabled service are
part of this stage.

## CM5 standalone API deployment gate (stage, no main merge)

Files added under `scripts/`:

- `prepare_cm5_api_v1.sh`: non-root Release candidate build of `ecu_api_http`,
  API tests, and the operator-only SAC read probes in a separate build
  tree. Records exact branch commit and SHA-256 in build output. Does not
  touch CAN or services.
- `install_cm5_api_v1.sh`: **one-time interactive root installer**, rejects
  non-CM5 Debian, dirty/wrong branch, missing candidate stamp, active CAN,
  unavailable kiosk/Bench Agent, occupied port 8878, existing/ambiguous
  accounts/groups and unexpected filesystem paths. Creates separate
  `ecu-api`, `ecu-api-read` and group-private readouts; generates a random
  token on the device; installs the root-owned binary and systemd unit.
  Starts only the API and calls the independent post-install smoke. Enables
  startup only after smoke PASS. No ECU commands, no raw Bench Agent access.
- `verify_cm5_api_v1.py`: checks exact running build SHA, bearer auth,
  strict Host/Origin, absence of CAN write routes, netlink data, absent
  live session and historical DTC, loopback-only socket, kiosk/agent service
  health and access denial to the privileged Bench socket and API token.
- `rollback_cm5_api_v1.sh`: stops and disables only the new API unit,
  removes its runtime binary/service, preserves DUT evidence, credentials
  and OS account for inspection. Can be invoked via
  `sudo /usr/local/sbin/ecu-api-v1-rollback` after installation; invoked
  automatically when the install smoke fails. **Reinstallation after
  rollback is intentionally refused** until a separate recovery review
  reconciles the retained accounts, token and filesystem state.

On CM5, stage a candidate after this PR is committed and all CI checks pass:

`bash scripts/prepare_cm5_api_v1.sh`

Then in the operator's **interactive** VS Code terminal, from the **separate
API branch directory** (`~/ECU_API_V1`), execute:

`sudo bash scripts/install_cm5_api_v1.sh`

The installer checks the candidate SHA itself and does not ask ChatGPT to
handle a password. Success requires
`ECU_API_INSTALL=PASS`, `ECU_API_CM5_INSTALL_SMOKE=PASS`, and
`ECU_API_CAN_UNCHANGED=PASS`. Any failure is fail-closed; do not
manually override device or service permissions to make it pass.

After installation, historical `/api/v1/readouts/dtc/latest` is
**unavailable** until a trusted operator deliberately executes a *new*
SAC readout with the protected opt-in flag (after reviewing current DUT
connection and ignition/power conditions). Existing private SAC trace
files are **not** imported or masqueraded as a new capture.

This CM5 deployment procedure is separate from `main` and WebGUI PR #19.
Neither the existing WebGUI nor the restricted Bench Agent is restarted or
reconfigured.
