# ECU Platform V2 — Stage G Linux SocketCAN adapter

## Status

**G1 VERIFIED / G2 PHYSICAL GATE PENDING**

Branch:

```text
stage-g/linux-socketcan-adapter
```

Base:

- approved/frozen Stage F contract,
- no merge to production `main`.

## Goal

Provide the first real OS-specific adapter for the frozen `ICanInterface` contract without introducing Linux dependencies into Core.

Boundary:

```text
Core / ICanInterface
        |
        v
Linux SocketCanAdapter
        |
        v
Linux PF_CAN / CAN_RAW / rtnetlink
        |
        v
can0 / MCP251xFD on Prototype A
```

Linux headers and syscalls exist only under:

```text
src/platform/linux/socketcan/
```

The Stage E Core portability gate remains mandatory.

## Stage G1 — build + codec + read-only link introspection

G1 implements:

- classic CAN frame encoding/decoding,
- CAN-FD frame encoding/decoding,
- EFF/RTR/BRS/ESI mapping,
- nonblocking PF_CAN raw socket,
- CAN-FD socket enable,
- bus-off error-frame subscription,
- adapter-level listen-only TX software guard,
- read-only rtnetlink inspection of:
  - interface UP/DOWN,
  - nominal bitrate,
  - data bitrate,
  - FD mode,
  - listen-only mode,
  - CAN state,
  - controller capabilities.

The adapter does not shell out to `ip` and Core never sees a Linux interface name.

## Link configuration policy for this stage

Stage G intentionally separates safe validation from link mutation.

The first adapter implementation verifies that the Linux CAN interface already matches the requested `CanChannelConfig`.

It does **not yet mutate** bitrate/controller mode through rtnetlink inside `SocketCanAdapter::open()`.

This keeps G1 testable without root and lets the physical G2 gate prove the real socket path before adding privileged link configuration logic.

A later Stage G substage can add controlled rtnetlink configuration without changing the frozen Core contract.

## Stage G2 — physical Prototype A gate

G2 configures `can0` temporarily as:

- nominal bitrate: 500 kbit/s,
- data bitrate: 2 Mbit/s,
- CAN-FD: on,
- listen-only: on.

Then the adapter is opened as the normal `ecu` user.

The physical gate:

1. binds the real PF_CAN socket,
2. proves the adapter sees the configured profile,
3. proves `try_receive()` works nonblocking,
4. calls `send()` only to verify the **software listen-only guard**; the call must return `unsupported` before any kernel write occurs,
5. leaves kernel listen-only enabled as a second independent TX barrier,
6. returns `can0` to DOWN on exit.

No Stage G acceptance test intentionally transmits a CAN frame.

## Commands

G1:

```bash
./scripts/validate_stage_g_socketcan.sh
```

Expected:

```text
STAGE_G1_SOCKETCAN=PASS
```

G2 requires one administrator authentication:

```bash
sudo ./scripts/run_stage_g_can0_listen_only_gate.sh
```

Expected:

```text
LISTEN_ONLY_TX_GUARD=PASS
SOCKETCAN_RECEIVE_PROBE=PASS
SOCKETCAN_ADAPTER_PROBE=PASS
STAGE_G2_LISTEN_ONLY=PASS
```

## Non-goals

Stage G does not implement:

- ISO-TP,
- UDS,
- J1939,
- DoIP,
- generic API/backend,
- WebGUI logic,
- database,
- systemd Core service,
- persistent CAN link configuration,
- arbitrary device discovery.

## Acceptance

Stage G is VERIFIED only after G1 and G2 both pass on Prototype A and `can0` is confirmed DOWN after the physical test.

No merge to `main` is authorized by Stage G work.


## G1 validation evidence — 2026-10-06

Result: **VERIFIED / PASS**

Validated directly on Prototype A:

- GNU C++ 14.2.0
- Debug build: PASS
- Release build: PASS
- Debug CTest: `3/3` PASS
- Release CTest: `3/3` PASS
- Core portability self-test: PASS
- real Core portability scan: PASS
- SocketCAN codec tests: PASS
- read-only rtnetlink probe of physical `can0`: PASS
- detected physical link:
  - `up=0`
  - `bus_off=0`
  - `cap_classic=1`
  - `cap_fd=1`
  - `cap_brs=1`
  - `cap_listen_only=1`
  - `max_payload=64`
- no CAN frame was transmitted
- `can0` remained DOWN after G1

A parser defect discovered during G1 was fixed before physical validation: nested netlink attribute flags are now masked with `NLA_TYPE_MASK`, allowing `IFLA_CAN_CTRLMODE_SUPPORTED` to be decoded correctly.

G2 remains the only open acceptance gate.
