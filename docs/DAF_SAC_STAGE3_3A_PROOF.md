# ECU Platform V2 — DAF SAC Stage 3.3A physical proof

Date: 2026-10-07
Branch: `dut-profile/proof-profiles`
Status: **SOFTWARE + CI PASS / FIRST 500 KBIT/S PHYSICAL READ ATTEMPT NO RESPONSE / PASSIVE BITRATE DISCOVERY NEXT**

## Scope

Stage 3.3A starts the real DUT proof sequence with the physically available
DAF SAC ECU.

The proof intentionally remains read-only:

- CAN: extended 29-bit addressing,
- tester -> SAC: `0x18DA30F9`,
- SAC -> tester: `0x18DAF930`,
- ISO-TP over Classic CAN,
- UDS `0x22 ReadDataByIdentifier`,
- `F190` VIN,
- `F188` software identification,
- `F192` hardware identification.

No diagnostic session change, ECU reset, write, security access, routine control,
output control or flashing command is part of this proof.

## DUT Profile proof

The DAF SAC concrete profile is implemented above the neutral Stage 3
contracts. It does not introduce concrete SAC behavior into Core V2 or Bench
Runtime.

Two evidence-backed bitrate variants are explicit immutable profiles:

- 250 kbit/s,
- 500 kbit/s.

The concrete identification program is injected with the Core V2 UDS client and
runs through the common DUT Profile lifecycle.

Local Stage 3 gate:

- Debug: PASS,
- Release: PASS,
- Generic non-Linux: PASS,
- ASan/UBSan: PASS,
- frozen Bench Runtime Stage 2 regression: PASS,
- frozen Core V2 regression: PASS.

## Linux Core V2 platform bridge

The previous Linux SocketCAN adapter belongs to the legacy Core and is not used
for the Stage 3.3A proof.

A separate Linux V2 platform target now provides:

- `BoottimeClock` using `CLOCK_BOOTTIME`,
- SocketCAN link-state/capability query through rtnetlink,
- `SocketCanAdapter : ICanDriver`,
- process-wide bounded CAN-channel arbitration,
- non-blocking send/receive,
- RX overflow accounting,
- bus-off fail-closed handling,
- strict verification that the physical link already matches the requested
  Core V2 `CanChannelConfig`.

The adapter does not configure Linux CAN bitrate, FD mode, listen-only mode or
link state. Privileged interface configuration remains outside Core V2 and
outside the DUT Profile.

Local Linux V2 platform gate:

- GCC Debug: PASS,
- GCC Release: PASS,
- ASan/UBSan: PASS,
- architecture gate: PASS,
- legacy Core dependency: NONE,
- heap ownership: NONE,
- hidden thread/sleep: NONE.

Clang is validated in GitHub CI because it is not installed on the physical ECU
host.

## Physical preflight

The Core V2 physical probe builds successfully.

A safe preflight on the connected `can0` reported:

- link state: DOWN,
- nominal bitrate: 500000,
- FD: enabled,
- data bitrate: 2000000,
- listen-only: enabled.

The probe therefore returned `LINK_NOT_READY` before opening the active CAN
session and before transmitting any CAN frame.

This is the intended fail-closed behavior.

## Privileged physical gate

The physical read-only proof is executed through:

`sudo ./scripts/run_stage3_3a_daf_sac_core_v2_gate.sh 500000`

The script:

1. requires root only for Linux CAN link configuration,
2. configures `can0` as Classic CAN, normal mode, at the selected
   evidence-backed bitrate,
3. executes the Core V2 probe as the ordinary ECU user,
4. reads only F190/F188/F192,
5. always returns `can0` to DOWN through an EXIT trap.

The application itself does not receive `CAP_NET_ADMIN`.

## First physical attempt — 500 kbit/s

The first active read-only attempt was executed on 2026-10-07.

Preflight confirmed:

- `can0` UP,
- Classic CAN,
- normal mode,
- nominal bitrate 500000 bit/s,
- controller state ERROR-ACTIVE.

Before the UDS request the controller already reported:

- TX error counter: 0,
- RX error counter: 42,
- valid RX packets: 0.

The Core V2 stack started the read-only identification request but received no
valid UDS response. The profile failed with NRC `0x00`, which exposed a
diagnostic-observability gap: transport/UDS timeout status was not retained
separately from a negative-response NRC.

The DUT Profile has therefore been updated to preserve:

- last UDS status,
- last UDS transport failure,
- NRC independently.

No Core V2 change was required.

The combination of a non-zero RX error counter and zero valid RX frames at
500 kbit/s is consistent with a bitrate mismatch or physical-layer receive
errors. It is not treated as proof by itself.

Legacy evidence defines:

- 250 kbit/s as primary,
- 500 kbit/s as secondary,
- legacy connection order: 250 kbit/s then 500 kbit/s.

The next hardware step is therefore passive bitrate discovery in LISTEN-ONLY
mode at both evidence-backed rates before another diagnostic request is sent.

The passive gate is `scripts/run_stage3_3a_daf_sac_passive_bitrate_gate.sh`.
It requires administrative link configuration, sends no diagnostic or CAN
data frames, and returns `can0` to DOWN on exit.

## Gate decision

Stage 3.3A is **not yet marked fully accepted** until:

- GitHub CI for the final code state passes,
- the physical Core V2 read-only probe succeeds against the connected SAC.

No Core V2 revision has been required.
No Bench Runtime revision has been required.
The actuator/cyclic-CAN proof remains a separate later Stage 3.3B requirement.
