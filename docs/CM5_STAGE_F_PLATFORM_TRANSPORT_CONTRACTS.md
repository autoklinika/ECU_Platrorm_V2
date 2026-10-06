# ECU Platform V2 — Stage F platform/transport contract candidate

## Status

**IMPLEMENTED CANDIDATE / validation required / not frozen**

This stage is intentionally isolated on:

```text
stage-f/platform-transport-contracts
```

It does not modify production `main` and does not silently convert open architecture questions into approved decisions.

## Goal

Define the smallest useful Core-facing contracts required before implementing a real Linux/SocketCAN adapter.

Stage F focuses only on:

1. monotonic time,
2. CAN/CAN-FD wire data types,
3. CAN interface capability/configuration contract,
4. nonblocking send/receive boundary,
5. tests proving the contract can be implemented by a fake without any OS dependency.

Storage, networking/DoIP, generic hardware I/O, device identity, security, process topology and plugin architecture remain outside this stage.

## Candidate design principles

### 1. Core sees CAN, not SocketCAN

Core receives and emits `CanFrame`. It never sees:

- `linux/can.h`,
- Linux file descriptors,
- socket addresses,
- interface names such as `can0`,
- `ip link`,
- GPIO/SPI details,
- VID/PID or a specific CAN vendor SDK.

### 2. Capability-oriented interface

`ICanInterface::capabilities()` describes the class of interface available to Core.

Current candidate capabilities:

- classic CAN,
- CAN-FD,
- bit-rate switching,
- maximum payload size.

The exact hardware identity and discovery mechanism are deliberately excluded.

### 3. Configuration is domain-level

`CanChannelConfig` contains only transport concepts that are portable across implementations:

- nominal bitrate,
- CAN-FD enable,
- data bitrate,
- normal/listen-only mode.

Linux-specific timing knobs and restart policy do not enter Core.

### 4. Nonblocking primitive instead of callback threading

The candidate contract exposes:

- `send(...)`,
- `try_receive()`.

`try_receive()` reports `would_block` explicitly.

The interface does not create a callback thread, event loop or OS scheduler contract. A later runtime layer can decide how polling/wakeup is integrated.

### 5. Portable status values

Core receives `CanStatus`, not `errno`, Windows error codes or vendor SDK return values.

The first candidate set is intentionally small:

- `ok`,
- `would_block`,
- `not_open`,
- `invalid_argument`,
- `unsupported`,
- `io_error`.

More domain-relevant states can be added when the safety/runtime design proves they are needed.

### 6. Monotonic timestamps only

Received frames carry `MonotonicTime` based on `std::chrono::nanoseconds`.

The epoch is intentionally unspecified. Ordering and elapsed time matter to protocol logic; wall-clock time does not belong in this contract.

`IMonotonicClock` makes time injectable for deterministic tests.

### 7. CAN-FD wire-length validation

The candidate treats `CanFrame::length` as the actual wire payload length.

Valid CAN-FD lengths are:

```text
0..8, 12, 16, 20, 24, 32, 48, 64
```

A higher layer that has e.g. 9 bytes of meaningful data must choose/pad the appropriate 12-byte wire frame. This keeps the Core representation unambiguous.

## Candidate source layout

```text
src/core/include/ecu/core/time/monotonic_clock.hpp
src/core/include/ecu/core/transport/can_types.hpp
src/core/include/ecu/core/transport/i_can_interface.hpp
src/core/src/can_types.cpp
tests/can_contract_tests.cpp
scripts/validate_stage_f_contracts.sh
```

## Contract tests

The test suite covers:

- standard 11-bit identifier bounds,
- extended 29-bit identifier bounds,
- classic CAN max length,
- CAN-FD 64-byte frame,
- legal/illegal CAN-FD wire lengths,
- remote-frame exclusion from CAN-FD,
- BRS exclusion from classic CAN,
- classic and FD channel configuration validation,
- capability rejection/acceptance,
- send-before-open behavior in a fake adapter,
- explicit nonblocking `would_block`,
- injection of a fake monotonic clock.

The fake adapter is part of the test only and contains no Linux dependency.

## Explicitly not implemented

Stage F does not add:

- SocketCAN,
- `can0` knowledge,
- Linux interface configuration,
- a systemd Core service,
- ISO-TP,
- UDS,
- J1939,
- DoIP,
- WebGUI API/backend,
- database,
- generic device discovery.

## Review points before freezing

The user should approve or change these contract decisions before a real SocketCAN adapter is written:

1. `ICanInterface` lifecycle: `open/close/is_open`.
2. Nonblocking `try_receive()` rather than callback-based receive.
3. Current portable `CanStatus` set.
4. `CanChannelConfig` fields and listen-only mode.
5. Strict CAN-FD wire-length representation.
6. Monotonic timestamp representation in nanoseconds.

Until that review, this is a tested **candidate**, not a production-frozen contract.

## Validation

Run:

```bash
./scripts/validate_stage_f_contracts.sh
```

Expected marker:

```text
STAGE_F_CONTRACT_CANDIDATE=PASS
```

Validation includes the Stage E portability guard plus Debug and Release builds and CTest.

## Merge boundary

Do not merge this branch to `main` without explicit user approval.
