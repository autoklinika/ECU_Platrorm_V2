# ECU Platform V2 — Stage H Core ISO-TP

## Status

**IMPLEMENTATION / VALIDATION IN PROGRESS**

Branch:

```text
stage-h/isotp-core
```

Base:

- Stage F CAN/time contract: approved/frozen,
- Stage G Linux SocketCAN adapter: VERIFIED / PASS.

## Goal

Implement ISO-TP entirely inside portable Core and strictly above `ICanInterface`.

Architecture boundary:

```text
UDS / future diagnostics
        |
        v
Core ISO-TP
        |
        v
ICanInterface
        |
        +----------------------+
        |                      |
        v                      v
Linux SocketCAN          future Windows/VCI
adapter                  / simulator adapter
```

ISO-TP contains no Linux headers, interface names, file descriptors or SocketCAN calls.

## Initial Stage H scope

Supported:

- normal physical addressing, 1:1,
- 11-bit and 29-bit CAN identifiers,
- Classical CAN,
- CAN-FD,
- Single Frame (SF),
- First Frame (FF),
- Consecutive Frame (CF),
- Flow Control (FC),
- block size,
- STmin,
- FC Wait limit,
- FC Overflow,
- sequence-number validation,
- flow-control timeout,
- consecutive-frame timeout,
- BRS through the existing CAN-FD contract,
- fixed maximum ISO-TP PDU size: 4095 bytes,
- nonblocking/poll-driven execution.

Not yet included:

- extended addressing,
- mixed addressing,
- functional 1-to-N addressing,
- >4095-byte extended First Frame length,
- padding policy configuration,
- wait-frame generation by the receiver,
- UDS semantics.

These exclusions are intentional and keep the first Core transport implementation reviewable before UDS.

## Scheduling model

`IsoTpEndpoint` is nonblocking.

It does not create:

- threads,
- timers,
- event loops,
- callbacks tied to an operating system.

The caller repeatedly invokes:

```cpp
endpoint.poll();
```

Time comes exclusively from the frozen `IMonotonicClock` abstraction.

This preserves deterministic tests and platform independence.

## Memory model

Stage H uses fixed-size Core buffers:

```text
max ISO-TP PDU = 4095 bytes
```

No heap allocation is required by the ISO-TP state machine itself.

The 4095-byte limit corresponds to the standard 12-bit First Frame length form. Extended >4095-byte First Frame encoding is deliberately deferred.

## CAN-FD Single Frame

For CAN-FD payloads larger than 7 bytes, Stage H uses the ISO-TP Single Frame escape form:

```text
byte 0: 0x00
byte 1: SF_DL
byte 2..: payload
```

The CAN-FD wire length is rounded upward to the next legal CAN-FD length supported by the frozen CAN contract.

## STmin

Accepted FC STmin encodings:

```text
0x00..0x7F -> 0..127 ms
0xF1..0xF9 -> 100..900 us
```

Reserved values are rejected as protocol errors.

## Flow control

Receiver configuration exposes:

- `rx_block_size`,
- `rx_stmin`.

Sender behavior:

- waits for FC after FF,
- respects BS=0 as unlimited block,
- waits for another FC after each configured block,
- respects received STmin,
- accepts FC Wait up to `max_wait_frames`,
- treats FC Overflow as transfer failure.

## Tests

The Stage H test suite covers at least:

1. STmin decoding,
2. Classic CAN Single Frame,
3. Classic CAN multi-frame transfer,
4. repeated FC with block size,
5. CAN-FD 20-byte Single Frame escape encoding,
6. CAN-FD multi-frame transfer,
7. microsecond STmin timing,
8. flow-control timeout,
9. consecutive-frame sequence mismatch,
10. payload >4095 rejection.

Tests use only fake `ICanInterface` and fake monotonic clock.

No SocketCAN code is required to validate ISO-TP logic.

## Validation

Run:

```bash
./scripts/validate_stage_h_isotp.sh
```

Expected marker:

```text
STAGE_H_ISOTP=PASS
```

The validation includes:

- Core portability self-test,
- real Core portability scan,
- Debug build + CTest,
- Release build + CTest,
- direct ISO-TP test.

## Merge boundary

Stage H work does not authorize merge to production `main`.
