# ECU Platform V2 — Stage H Core ISO-TP

## Status

**VERIFIED / PASS**

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


## Validation evidence — 2026-10-06

Result: **VERIFIED / PASS**

Validated directly on Prototype A:

- compiler: GNU C++ 14.2.0
- Core portability self-test: PASS
- real Core portability scan: PASS
- Debug build: PASS
- Debug CTest: `4/4` PASS
- Release build: PASS
- Release CTest: `4/4` PASS
- direct ISO-TP suite: `ISOTP_CORE_TESTS=PASS`
- final validator: `STAGE_H_ISOTP=PASS`
- AddressSanitizer: PASS
- UndefinedBehaviorSanitizer: PASS
- no new OS packages installed
- no physical CAN link configuration changed by Stage H

Additional edge coverage verified:

- standard 11-bit addressing
- extended 29-bit addressing
- Classic CAN SF and multi-frame
- CAN-FD SF escape format
- CAN-FD multi-frame
- repeated Flow Control with block size
- STmin in milliseconds
- STmin in 100-us units
- sequence-number mismatch rejection
- FC Overflow handling
- FC Wait limit
- FC timeout
- 4095-byte full PDU transfer
- sequence-number wrap during long transfer
- >4095-byte transfer rejection with FC Overflow response

## Defects found and fixed during validation

### STmin across FC block boundaries

The first implementation allowed a new CF immediately after a new FC frame when
`BS=1`, even if the previous CF-to-CF STmin had not elapsed.

The scheduler was corrected so the next eligible CF time remains:

```text
previous CF send time + STmin
```

even when a block boundary requires another FC.

A regression test now covers this behavior.

### FC Overflow delivery ordering

The first implementation queued FC Overflow for unsupported extended FF length
but could return the local `payload_too_large` result before the queued FC was
actually sent.

`poll()` now defers the local error return until pending control traffic has had
a send attempt. A regression test verifies that unsupported extended FF encoding
produces FC Overflow.

## Final Stage H boundary

The validated Core path is now:

```text
future UDS
   |
   v
IsoTpEndpoint
   |
   v
ICanInterface
   |
   +--> SocketCanAdapter on Prototype A
   +--> future platform adapters
```

Stage H remains intentionally limited to normal physical 1:1 addressing and
12-bit FF payload length up to 4095 bytes. Extended/mixed/functional addressing
and extended-length FF are future revisions, not hidden Stage H assumptions.

No merge to production `main` was performed or authorized.
