# ECU Platform V2 — DAF SAC Stage 3.3A physical proof

Date: 2026-10-07
Branch: `dut-profile/proof-profiles`
Status: **STAGE 3.3A PHYSICAL CORE V2 / ISO-TP / UDS GATE PASS AT 250 KBIT/S — FINAL CI PENDING**

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

`sudo ./scripts/run_stage3_3a_daf_sac_core_v2_gate.sh 250000`

The first historical attempt used 500000 bit/s and did not receive a UDS response.
The successful final physical proof used 250000 bit/s.

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

## First passive scan — capture-tool defect

The first passive scan was executed at 250 and 500 kbit/s on 2026-10-07.
Both configurations were correctly set to Classic CAN LISTEN-ONLY.
Kernel statistics from the user-provided report were:

- 250 kbit/s: RX packets delta 14, RX errors delta 0, CAN RX error counter 0;
- 500 kbit/s: RX packets delta 0, RX errors delta 0, CAN RX error counter 0.

However, the script incorrectly combined `candump -L -e`. The installed
`candump` refuses this combination with `Log file format selected: Please
disable ASCII/BINARY/SWAP/RAWDLC options!` and returns exit code 0.
The script mistakenly recorded zero captured frames for both candidates.
**These zero-frame readings are invalid and the bitrate is not verified.**

The corrective version uses `candump -D -ta -e` with the monitor armed before
the link is brought UP, an 8-second capture window, detection of premature
monitor termination, kernel RX/TX deltas, and raw output excerpts. It continues
to transmit no frames and leaves `can0` DOWN on exit.

The repeat passive scan was executed successfully on 2026-10-07 with the corrected capture tool:

| Metric (8 s each) | 250 kbit/s | 500 kbit/s |
|---|---:|---:|
| Captured CAN data frames | 12597 | 0 |
| Kernel RX packets delta | 12591 | 0 |
| Kernel RX bytes delta | 100760 | 0 |
| Kernel RX errors delta | 0 | 0 |
| Kernel TX packets delta | 0 | 0 |
| CAN RX/TX error counters | 0/0 | 0/0 |
| Capture mismatch flag | NO | NO |

At 250 kbit/s the capture contains repeated extended frames:

`0x18FEAE30 [8] FF FF FE FE FF FF FF FF`

J1939 identifier breakdown: priority 6, PGN `0xFEAE`, source address
`0x30`. This matches the anticipated SAC diagnostic destination address, but
source identity has not been independently authenticated.

Physical read-only CAN reception at 250 kbit/s is now **PASS**; no correct
frames were captured at 500 kbit/s. The recorded high repeat rate (~1,575
frames/s) is consistent with possible missing-ACK retransmissions on a bench
with the only receiver in LISTEN-ONLY mode; application periodicity is not
established. Observe behavior in normal/ACK mode separately.

The bounded, read-only Core V2 request with the **250000** bitrate profile
was executed with the script described above. Its recorded results follow.

## Successful physical proof — 250 kbit/s, 2026-10-07

The user-supplied log from the live connected DAF SAC confirms:

- `SAC_LINK status=0 up=1 bus_off=0 bitrate=250000 fd=0 data_bitrate=0 listen_only=0`
- `SAC_PHYSICAL_PROBE=START tx=0x18da30f9 rx=0x18daf930 bitrate=250000 mode=read-only-identification`
- UDS `0x22 F190` VIN read: **PASS**, 17-character VIN (intentionally omitted from this public repository)
- UDS `0x22 F188` software identification: **PASS**, `1973214`
- UDS `0x22 F192` hardware identification: **PASS**, `K075169` (trailing spaces trimmed)
- `SAC_PHYSICAL_PROBE=PASS CORE_V2_UDS_ISOTP_CAN`
- `DAF_SAC_STAGE3_3A_PHYSICAL_GATE=PASS`

Before the active probe, CAN state was UP, ERROR-ACTIVE, Classic CAN at
250000 bit/s. Kernel totals: RX 12759 packets (102072 bytes), TX 0 packets;
RX/TX errors 0/0. After the active proof and before the EXIT trap: RX 12777
packets (102189 bytes), TX 6 packets (21 bytes); RX/TX errors remained 0/0,
no bus-off and no new TX drops. Probe deltas: **RX +18, TX +6**.

The script's EXIT trap returned the interface to **DOWN**, verified separately
on the CM5 after the user test. No session control, write, reset, security,
routine, output-control, or flash command was used. The complete physical
read-only Core V2 / ISO-TP / UDS / DUT Profile chain is now evidenced.

The full vehicle VIN is intentionally not included in the repository,
command history, or committed test artifacts. Only the fact of successful
reading and its expected 17-character length are retained here.

## Gate decision

- **Physical read-only Stage 3.3A gate: PASS** at 250000 bit/s.
- **Full Stage 3.3A acceptance: conditional** on GitHub CI passing for the
  final documentation revision. Do not infer a CI result from the physical test.
- Core V2 revision: **not needed** based on these results.
- Bench Runtime revision: **not needed** based on these results.
- Stage 3.3B actuator/cyclic-CAN proof remains separate and is not covered by
  this ECU-only read-only acceptance.

No changes to production `main` without explicit user approval.
