# Core V2 ISO-TP / DoCAN status

Date: 2026-10-07
Product scope: TRUCK / AGRI / OHV

Status:

- **ENGINEERING IMPLEMENTATION: PASS**
- **NORMATIVE ISO 15765-2:2024 CONFORMANCE: OPEN**

## V2 architecture

ISO-TP is implemented above the Core V2 shared CAN runtime rather than owning a
physical CAN driver.

- RX arrives through `ICanFrameSink::on_can_frame()`.
- The RX callback performs bounded local state transitions only.
- The callback never calls `CanBusRuntime::send()` or `poll()`.
- Flow Control is queued in one bounded deferred-control slot.
- `service()` performs at most one CAN TX attempt per call.
- PDU storage is fixed at 4095 bytes; the implementation performs no dynamic
  allocation.
- No Linux, SocketCAN, UI or product-layer dependency exists in Core V2 ISO-TP.

## Implemented engineering scope

- Classic CAN and CAN FD,
- 11-bit and 29-bit CAN identifiers,
- normal-addressing framing,
- Single Frame including CAN-FD escape length,
- First Frame and Consecutive Frame,
- Flow Control CTS, Wait and Overflow,
- Block Size,
- STmin including 0xF1..0xF9 sub-millisecond encodings,
- flow-control and consecutive-frame timeouts,
- sequence-number wrap,
- bounded retry after CAN `would_block`,
- fail-closed monotonic clock-domain, uncertainty and backward-time checks,
- deferred overflow FC for unsupported transfer size,
- maximum supported PDU: 4095 bytes.

## Explicitly outside the current claim

The current engineering gate does **not** claim complete ISO 15765-2:2024
conformance. In particular, these remain gated:

- extended and mixed addressing,
- 32-bit FF_DL transfers above 4095 bytes,
- complete clause-by-clause timing/profile mapping,
- independent external interoperability vectors,
- controller/transceiver certification.

29-bit CAN identifiers are supported, but this does not imply ISO-TP
"extended addressing"; the current framing is normal addressing.

## Regression evidence

The Core V2 suite covers:

- Classic CAN Single Frame end-to-end,
- Classic multi-frame transfer with repeated Flow Control,
- CAN-FD Single Frame escape encoding,
- CAN-FD multi-frame transfer,
- STmin preservation across Block Size / Flow Control boundaries,
- 29-bit CAN identifiers,
- Flow Control timeout,
- Wait-frame limit,
- Flow Control Overflow,
- wrong Consecutive Frame sequence,
- full 4095-byte transfer with sequence wrap,
- rejection of unsupported extended FF length,
- malformed matching CAN frame rejection,
- CAN `would_block` retry without state loss,
- independent monotonicity tracking for service time and queued RX timestamps,
- backward service/RX time fail-closed behavior and explicit reset recovery.

Validation is part of `ecu_core_v2_tests` and therefore executes in the same
Debug, Release, Generic non-Linux, sanitizer and CI matrix as the rest of Core
V2.

CORE_V2_ISOTP_ENGINEERING=PASS
CORE_V2_ISOTP_NORMATIVE_CONFORMANCE=OPEN
