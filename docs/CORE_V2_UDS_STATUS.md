# Core V2 UDS status

Date: 2026-10-07
Product scope: TRUCK / AGRI / OHV

Status:

- **ENGINEERING FOUNDATION: PASS**
- **NORMATIVE ISO 14229 CONFORMANCE: OPEN**

## Architecture

UDS is implemented as a transport-neutral L4 protocol. The client depends only
on `IDiagnosticTransport` and monotonic-time contracts.

The current ISO-TP binding is provided by a separate
`IsoTpDiagnosticTransport` adapter. UDS therefore does not depend directly on
CAN, SocketCAN, Linux, a specific CAN driver or a specific vehicle module.

The link/runtime owner remains responsible for CAN RX dispatch. UDS service
advances only the diagnostic transport state exposed by the adapter.

## Implemented engineering scope

- fixed-storage, allocation-free request/response handling up to 4095 bytes,
- generic raw UDS request client,
- positive-response SID validation,
- strict three-byte negative-response parsing,
- NRC preservation,
- NRC 0x78 ResponsePending with P2* transition,
- P2 timeout,
- P2* timeout,
- timeout decisions using transport completion timestamps rather than scheduler
  polling time,
- monotonic clock-domain and timestamp-uncertainty validation,
- fail-closed handling of backward transaction timestamps,
- explicit transport-failure classification,
- session timing update from DiagnosticSessionControl positive response,
- service builders for:
  - DiagnosticSessionControl (0x10),
  - ReadDTCInformation / reportDTCByStatusMask (0x19/0x02),
  - ReadDataByIdentifier (0x22),
  - TesterPresent (0x3E),
- ISO-TP adapter integration,
- end-to-end multi-frame UDS response over Core V2 ISO-TP and shared CAN runtime.

## Timing boundary

ISO-TP exposes a conservative TX completion reference timestamp and RX PDU
completion timestamp. UDS uses those timestamps to establish and evaluate P2
and P2* deadlines. This avoids extending protocol deadlines merely because the
host calls UDS service late.

Clock domain mismatch, excessive uncertainty, discontinuity/unavailable
readings, and backward transaction time fail closed.

## Validation

The Core V2 local validation matrix passes with 13 tests in every configured
variant:

- Debug: 13/13 PASS,
- Release: 13/13 PASS,
- Generic non-Linux: 13/13 PASS,
- sanitizers: 13/13 PASS.

Architecture, portability, isolated dependency graph, external-runtime-symbol
and dynamic/static-init gates also pass.

Dedicated coverage includes positive responses, negative responses, NRC 0x78,
P2, P2*, late-response rejection, mismatched response SID, bus-off transport
classification, clock-domain faults, backward timestamps, server timing
promotion and UDS-over-ISO-TP multi-frame end-to-end transfer.

## Explicitly outside the current claim

This engineering gate does **not** claim complete ISO 14229 conformance.

Still open:

- ISO 14229-1:2026 clause/service/NRC matrix,
- ISO 14229-2:2021 complete session and timing behavior,
- ISO 14229-3:2022 UDSonCAN profile conformance,
- suppressPositiveResponse handling as an explicit client transaction mode,
- full service builders/parsers,
- SecurityAccess, Authentication and secured-data workflows,
- download/upload/programming state machines,
- RoutineControl execution semantics,
- functional-addressing response policy,
- DoIP binding,
- independent interoperability vectors against external diagnostic tools/ECUs.

AUTOSAR may be used as an implementation cross-check but does not replace ISO
normative evidence.

CORE_V2_UDS_ENGINEERING=PASS
CORE_V2_UDS_NORMATIVE_CONFORMANCE=OPEN
