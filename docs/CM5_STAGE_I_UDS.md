# ECU Platform V2 — Stage I UDS Core client

## Status

**IMPLEMENTATION / VALIDATION IN PROGRESS**

Branch:

```text
stage-i/uds-core
```

Base:

- Stage F CAN/time contract: approved/frozen,
- Stage G Linux SocketCAN adapter: VERIFIED / PASS,
- Stage H portable ISO-TP: VERIFIED / PASS.

## Goal

Implement the first portable UDS client layer entirely inside Core and above
`IsoTpEndpoint`.

Architecture:

```text
future diagnostic application/services
        |
        v
UdsClient
        |
        v
IsoTpEndpoint
        |
        v
ICanInterface
        |
        +--> Linux SocketCAN adapter
        +--> future VCI / simulator adapters
```

No UDS Core code may include Linux, SocketCAN, systemd, interface names or
vendor-specific DAF/SAC details.

## Stage I scope

Implemented:

- one outstanding UDS request per `UdsClient`,
- positive response SID validation (`request SID + 0x40`),
- negative response format `0x7F <request SID> <NRC>`,
- NRC preservation,
- NRC `0x78 ResponsePending`,
- P2 timeout,
- P2* timeout,
- DiagnosticSessionControl request builder (`0x10`),
- ReadDataByIdentifier request builder (`0x22`),
- TesterPresent request builder (`0x3E`),
- parsing/updating P2 and P2* from a positive `0x10` response,
- fixed-size response storage up to the Stage H ISO-TP limit.

Not included yet:

- ECUReset (`0x11`) physical use,
- SecurityAccess (`0x27`),
- WriteDataByIdentifier (`0x2E`),
- RoutineControl (`0x31`),
- RequestDownload/TransferData/RequestTransferExit,
- DTC service implementations,
- DAF/SAC-specific DIDs,
- automatic TesterPresent scheduler,
- security seed/key algorithms,
- flashing workflows.

Those features are intentionally excluded from the first UDS client gate.

## P2 / P2*

Stage I treats P2/P2* as UDS session-layer timing, independent from ISO-TP
transport timing.

Initial client timing is configurable.

NRC `0x78 ResponsePending` transitions the request from the P2 wait window to
P2* and each accepted ResponsePending refreshes that P2* wait.

A positive DiagnosticSessionControl response can update subsequent client timing.
The parser uses the standard UDS wire representation:

- P2ServerMax: 16-bit big-endian milliseconds,
- P2*ServerMax: 16-bit big-endian value in 10-ms units.

AUTOSAR R24-11 was used as a trusted external cross-check that P2/P2* are
per-session UDS timing parameters and are changed by successful
DiagnosticSessionControl processing.

## Concurrency model

One `UdsClient` has exactly one outstanding request.

This is intentional:

- response correlation is deterministic,
- P2/P2* state belongs to one conversation,
- NRC handling cannot cross-contaminate another request.

Future parallel diagnostics should use separate conversation/client instances
rather than multiplex unrelated requests through one state object.

## Safe request builders

Stage I provides builders for:

```text
0x10 DiagnosticSessionControl
0x22 ReadDataByIdentifier
0x3E TesterPresent
```

This does not mean all are automatically safe to send to every ECU. The physical
SAC gate remains separate from Core validation.

No write, reset, security-unlock, actuator, routine, erase or flash request is
part of the Stage I physical gate without explicit user approval.

## Tests

The simulated ECU tests cover:

1. request builders,
2. positive `0x22` response,
3. negative response and NRC preservation,
4. `0x78 ResponsePending` followed by positive response,
5. P2 timeout,
6. P2* timeout,
7. positive `0x10` response and timing update,
8. multi-frame UDS response through the real Core ISO-TP implementation,
9. mismatched positive SID rejection.

All tests run over fake `ICanInterface` instances through the actual Stage H
`IsoTpEndpoint`.

## External-source policy used for Stage I

Protocol details were cross-checked only against trusted automotive sources:

- official AUTOSAR diagnostics specifications,
- Vector UDS material where needed.

No forum/community source is used as normative input.

## SAC / DAF physical validation policy

A SAC from DAF is physically connected, but Stage I does not assume its CAN IDs,
DIDs, session requirements or bus profile.

Before the first request to the real SAC:

1. search legacy `autoklinika/ecu_platform` for validated same-project evidence,
2. confirm current physical CAN state,
3. use only read-only / non-mutating diagnostics initially,
4. stop and ask the user if addressing or safety is ambiguous.

The initial physical gate must not use reset, write, security access, routine
control, output control, erase or flashing services.

## Validation

Run:

```bash
./scripts/validate_stage_i_uds.sh
```

Expected marker:

```text
STAGE_I_UDS=PASS
```

## Merge boundary

Stage I work does not authorize merge to production `main`.
