# ECU Platform V2 — Core V2 engineering audit

Date: 2026-10-07
Scope: TRUCK / AGRI / OHV portable Core V2
Status: **ENGINEERING AUDIT PASS / NORMATIVE CLAUSE AUDITS OPEN**

## Audit objective

Re-check the Core before adding more application layers, with emphasis on multiplatform portability, deterministic/bounded execution, absence of platform dependencies in Core, J1939/ISOBUS protocol boundaries, fail-closed behavior, standards-status honesty and regression resistance.

## Verification performed

- architecture gate,
- portability gate,
- scoped Core V2 conformance gate,
- negative architecture/conformance gates,
- isolated Core-only build graph,
- unresolved external-runtime symbol inspection,
- dynamic static-initialization inspection,
- Debug, Release and Generic non-Linux builds/tests,
- ASAN + UBSAN builds/tests,
- additional GCC warning audit using -Wconversion, -Wsign-conversion, -Wshadow, -Wformat=2, -Wundef, -Wnull-dereference and -Werror.

## Findings closed in this audit

### A-01 — global source address admitted by generic J1939 identifier codec

The encoder/decoder previously accepted source address 0xFF, even though that value is the J1939 global destination address.

Resolution: source 0xFF is rejected by both identifier encode and decode paths; regression tests added.

### A-02 — Request PGN payload length was permissive

Request for Address Claimed accepted a payload longer than the required three-byte requested-PGN field.

Resolution: the Address Claim request path now requires exactly three bytes; malformed addressed Request frames are counted but do not trigger a response.

### A-03 — local-win Address Claim conflict was not counted

When the local NAME won arbitration, the address was reasserted but the conflict counter did not record the observed collision.

Resolution: conflict observation is explicit in AddressClaimStep; both local-win and local-loss collisions are counted.

### A-04 — state transition invisible from service_time()

The transition claiming -> claimed returned no_action if no TX frame was generated.

Resolution: protocol state transitions are now reported as ok; regression coverage added.

### A-05 — Commanded Address missing from J1939/81 subset

Resolution: a 9-byte Commanded Address payload is accepted after Classical TP reassembly, target matching is by 64-bit NAME, invalid/null/global new source address is rejected, a successful command restarts normal Address Claim behavior, and the feature is opt-in/disabled by default.

### A-06 — own-TX echo contract was implicit

An own Address Claimed TX delivered back through authoritative protocol RX is indistinguishable from a duplicate remote NAME.

Resolution: ICanDriver::try_receive() now explicitly forbids same-driver own-message echo on protocol ingress. Analyzer/trace loopback remains permitted outside that ingress.

### A-07 — J1939/ETP status documentation drift

The J1939 checkpoint still described ETP as a future layer although the streaming ISO 11783 ETP module already passed its technical gate.

Resolution: traceability and checkpoint documentation updated.

### A-08 — J1939 diagnostic layer absent

Resolution: added read-only J1939-73 DM1/DM2 parsing for single-frame and Classical TP-reassembled messages. Current conversion-method DTC decoding is bounded; unsupported legacy conversion method fails closed; no DTC-clear/control operations were introduced.

### A-09 — Request framing duplicated inside Address Claim

Resolution: introduced one generic strict PGN 59904 Request codec with canonical requested-PGN validation. Address Claim now consumes that codec and DM2/on-request diagnostics can use the same boundary.

### A-10 — diagnostic request failures had no shared Acknowledgment parser

Resolution: added a read-only PGN 59392 Acknowledgment decoder covering ACK, NACK, Access Denied and Cannot Respond with strict DLC, reserved-field, address and requested-PGN validation. No automatic response transmission was added.

### A-11 — ISO-TP still depended on the legacy direct CAN interface

Resolution: revalidated ISO-TP on the Core V2 shared-bus contract. A header-only
`IsoTpCanFrameSinkAdapter` receives routed `ICanFrameSink` traffic and forwards it
to the non-polymorphic state machine; callbacks are bounded and never transmit.
This keeps compiler-specific pure-virtual runtime support out of the Core static
library. Flow Control and data
TX are deferred to `service()` through `CanBusRuntime`, with at most one CAN TX
attempt per service call. The V2 implementation uses fixed 4095-byte storage,
supports Classic CAN and CAN-FD, 11-bit/29-bit CAN identifiers, Single/First/
Consecutive/Flow-Control frames, Block Size, STmin, Wait/Overflow and fail-closed
monotonic timing. Regression coverage includes sequence wrap, timeouts,
`would_block`, malformed CAN input and clock faults.

The engineering gate is PASS. Normative ISO 15765-2:2024 clause conformance,
extended/mixed addressing, 32-bit FF_DL beyond 4095 bytes and independent
interoperability evidence remain explicitly open.

### A-12 — UDS existed only in the legacy Core

Resolution: promoted a transport-neutral UDS foundation into Core V2 above a
new `IDiagnosticTransport` boundary. The implementation includes positive and
negative response validation, P2/P2* deadlines, NRC 0x78 ResponsePending,
transport failure classification, fail-closed monotonic clock checks and
selected diagnostic service builders/parsers. ISO-TP binding is isolated in a
separate adapter. P2/P2* use transport TX/RX completion timestamps rather than
host polling time. A dedicated multi-frame UDS-over-ISO-TP end-to-end test is
part of the Core V2 matrix.

The UDS engineering gate is PASS. Formal ISO 14229-1/-2/-3 clause conformance,
full service coverage, suppressPositiveResponse transaction semantics and
independent interoperability evidence remain explicitly open.

### A-13 — J1939-73 diagnostics were too narrow for workshop read-only use

Resolution: expanded the bounded read-only diagnostics layer from DM1/DM2 to
DM1/DM2/DM4/DM5/DM6/DM12. DM4 now validates and exposes variable-length freeze
frame records, raw standard snapshot fields and bounded manufacturer-specific
data. DM5 exposes diagnostic-readiness fields without application-level
reinterpretation. DM6/DM12 reuse the bounded DTC-list primitive. Reassembled
diagnostic TP messages now accept both BAM/global and destination-specific
RTS/CTS response envelopes, with strict source/destination checks. No clearing,
actuation, test execution or other state-changing diagnostic operation was
introduced.

The expanded read-only technical gate is PASS. Full SAE J1939-73 clause mapping,
remaining diagnostic messages and external interoperability evidence remain
open.

## Open items that are not engineering defects

The repository does not contain licensed full SAE/ISO normative texts. Consequently this audit does not close clause-level conformance for SAE J1939/21, SAE J1939/81, SAE J1939-73, ISO 11783-6, ISO 15765-2:2024 or ISO 14229-1/-2/-3. Independent interoperability evidence is also required before formal protocol-conformance PASS.

## Next protocol priorities

1. SAE J1939-22 CAN FD adaptation — FEFF/no-assurance C-PG subset implemented after this audit; FBFF, FD.TP and assurance profiles remain open.
2. ISO 11783 network/application profiles needed by AGRI.
3. ISO 11992 / WWH-OBD where required by heavy-truck modules.
4. UDS service/profile expansion after the transport-neutral foundation.
5. Further read-only J1939-73 services selected from actual TRUCK/AGRI/OHV workshop needs.
6. ISO-TP/UDS/J1939 normative clause mapping and external interoperability evidence.

CORE_V2_ENGINEERING_AUDIT=PASS
CORE_V2_NORMATIVE_CLAUSE_AUDITS=OPEN
