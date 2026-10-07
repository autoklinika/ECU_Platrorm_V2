# ECU Platform V2 — ISO 11783 / ISOBUS ETP checkpoint

Date: 2026-10-07

Status: **TECHNICAL GATE PASS / ISO 11783-6 CLAUSE AUDIT REQUIRED**

## Product scope

This layer exists primarily for **AGRI** and forestry/off-highway equipment that
uses ISO 11783 / ISOBUS extended transport. It reuses the same portable Core V2
CAN/J1939 substrate used by TRUCK and OHV, but it does not broaden the product
scope beyond TRUCK / AGRI / OHV.

## Implemented ETP subset

The layer implements the extended transport path used for messages larger than
Classical J1939 TP can carry:

- ETP.CM PGN 0xC800,
- ETP.DT PGN 0xC700,
- RTS control 0x14,
- CTS control 0x15,
- DPO control 0x16,
- EOMA control 0x17,
- Abort control 0xFF,
- 32-bit total message length in RTS/EOMA,
- 24-bit next-packet number in CTS,
- 24-bit zero-based packet offset in DPO,
- 1..255 local DT sequence numbers inside a DPO block,
- destination-specific / unicast-only sessions,
- minimum ETP payload 1786 bytes,
- maximum packet count 0xFFFFFF,
- maximum payload 117,440,505 bytes.

## Streaming architecture

Core does **not** allocate a message-sized buffer.

RX uses the non-owning interface `IEtpReceiveSink`:

- `begin(...)`,
- `write(byte_offset, 7-byte chunk, valid_bytes)`,
- `commit()`,
- `abort()`.

TX uses the non-owning interface `IEtpTransmitSource`:

- `read(byte_offset, 7-byte chunk, valid_bytes)`.

The source/sink is owned by the host/application layer and must outlive the
active session. Their destructors are protected and non-virtual, matching the
non-owning deterministic Core lifetime model.

This permits a transfer up to the ETP protocol maximum without embedding up to
111 MiB of storage in a Core state machine. Tests enforce that both ETP state
machine objects remain below 4 KiB.

## Receiver behavior

- one bounded destination-specific ETP RX session,
- validates RTS range and transported PGN,
- calls streaming sink `begin`,
- issues bounded CTS grants,
- validates DPO offset against absolute packets already received,
- maps DPO + local DT sequence into a 32-bit absolute byte offset,
- validates final FF padding,
- writes only valid payload bytes to the sink,
- commits before EOMA,
- sink failure -> resources Abort,
- remote Abort -> sink abort/reset,
- timeout -> timeout Abort + sink abort,
- backward/wrong-domain/over-uncertain time -> fail closed,
- fixed deferred-TX queue with fail-closed overflow.

## Transmitter behavior

- unicast-only metadata submit,
- RTS first,
- supports CTS grants and CTS(0) hold,
- emits DPO before each block,
- source reads are offset-based/random-access,
- one ETP.DT frame is queued per service step,
- source failure -> resources Abort,
- waits for EOMA after final packet,
- remote Abort terminates session,
- timeout -> timeout Abort,
- backward/wrong-domain/over-uncertain time -> fail closed,
- fixed deferred-TX queue with fail-closed overflow.

## Verification

Executable gate:

- `ecu.core_v2.isobus.etp`

Coverage includes:

- exact 32-bit/24-bit CM fields,
- 1786-byte lower boundary,
- 117,440,505-byte upper boundary without large allocation,
- unicast-only rejection of global destination,
- maximum 24-bit DPO offset bounds,
- receiver sink begin/write failures,
- source read failure,
- receiver/transmitter timeouts,
- backward-time faults,
- CTS(0) hold timeout,
- TX and RX queue overflow,
- end-to-end 1800-byte streaming transfer,
- crossing packet 255 -> second DPO offset 255,
- payload pattern integrity from source through receiver sink.

## Normative/reference status

Normative baseline:

- ISO 11783-6:2018 — Agricultural tractors and machinery for agriculture and
  forestry — Serial control and communications data network — Part 6: Virtual
  terminal.

The currently published 2018 edition remains the project baseline; a newer
edition under development is tracked but is not treated as published baseline.

Public implementation cross-check:

- Linux kernel SAE J1939 / ISO 11783 transport documentation and implementation.

The full licensed ISO 11783-6 normative text is not present in the repository.
Therefore this checkpoint is an engineering/technical PASS only. A
clause-by-clause normative audit and independent ISOBUS interoperability
evidence remain mandatory before standards-level module PASS.

## Next scope

ETP is only the large-message transport foundation. It does not claim complete
ISOBUS Virtual Terminal, Task Controller, diagnostics or application semantics.

ISOBUS_ETP_TECHNICAL_GATE=PASS
ISOBUS_ETP_STREAMING_NO_LARGE_CORE_BUFFER=PASS
ISOBUS_ETP_STANDARDS_CONFORMANCE=CLAUSE_AUDIT_REQUIRED
