# ECU Platform V2 — DUT Profile foundation architecture

Date: 2026-10-07
Branch: `dut-profile/foundation`
Status: **STAGE 3 FOUNDATION IN DEVELOPMENT**

## 1. Entry boundary

Stage 3 starts above the accepted Stage 2 Bench Runtime.

Frozen lower layers remain unchanged:

- Core V2: generic transport/protocol/runtime/safety primitives,
- Bench Runtime: one physical DUT session, resource ownership, electrical
  orchestration, minimal environment, host service deadline and observability.

Stage 3 introduces the common DUT Profile contract. It must not move concrete
OEM behavior into Core or Bench.

## 2. Primary separation

A DUT Profile has two different concerns that are deliberately separated.

### Portable profile definition

`DutProfileDefinition` describes what a DUT requires independently of the
bench computer and connected adapters.

It may declare:

- Core DUT identity/class/capabilities,
- required protocol foundations,
- logical resource roles,
- CAN link requirements,
- expected CAN RX filters,
- power / ignition / wake requirements,
- optional minimal environment requirement,
- cyclic-control timing/safety requirements.

It does not contain:

- Linux interface names,
- `can0` / `can1`,
- GPIO numbers,
- relay board identifiers,
- physical VCI driver objects,
- `ICanDriver`,
- a concrete power-controller implementation.

### Bench binding

`DutProfileBinding` maps logical profile resource roles to
`ResourceKey` values available on one physical bench and supplies the Core
monotonic timestamp domain used by CAN adapters.

The same portable profile definition may therefore be resolved on two benches
with different CAN channel instances without modifying the profile.

## 3. Resolution result

`resolve_profile_session()` validates the portable definition and physical
binding against the frozen Core `DutRegistry`.

A successful resolution produces `ResolvedDutSessionPlan` containing:

- the accepted profile/schema revision,
- profile ID,
- required protocol mask,
- a resolved `BenchSessionConfig`,
- resolved CAN links with `CanChannelConfig`,
- cyclic-control requirements.

No physical CAN driver pointer crosses this boundary.

The concrete Stage 3 runtime/profile module will later consume this plan and
provide the actual `IDutSessionEndpoint` behavior to Bench Runtime.

## 4. Resource roles

A profile declares logical `ResourceRoleId` values.

Example conceptually:

- primary CAN bus -> role 10,
- DUT power domain -> role 20.

A bench binding may resolve those roles to:

- CAN channel instance 3,
- power-domain instance 1.

A different bench may resolve the same roles to other instances.

Rules:

- role 0 is invalid,
- roles are unique,
- a profile cannot request `device_under_test` as an additional resource,
- CAN link roles must resolve to `can_channel`,
- electrical roles must resolve to `power_domain` or `hardware_io`,
- two logical roles cannot silently bind to the same physical resource.

The DUT resource itself remains owned automatically by Stage 2 Bench Session.

## 5. CAN requirements

The portable profile declares CAN link properties without the physical driver:

- logical link ID,
- resource role,
- nominal bitrate,
- Classic CAN vs CAN-FD,
- CAN-FD data bitrate when applicable,
- normal vs listen-only mode.

The bench binding supplies the monotonic timestamp domain. Resolution then
creates a standard Core `CanChannelConfig`.

Expected RX is a fixed-capacity list of filters associated with logical links.

No frame is received directly by Stage 3. Runtime traffic still flows through
the authoritative Core `CanBusRuntime`.

## 6. Protocol requirements

Stage 3 foundation currently declares only protocol foundations already accepted
in Core and relevant to the present roadmap:

- J1939,
- ISO-TP,
- UDS over the current CAN transport path,
- ISOBUS.

DoIP is deliberately not added on speculation.

Protocol declarations must be consistent with the Core `DutDescriptor`
capability mask.

Current consistency rules include:

- required UDS requires required ISO-TP,
- required ISOBUS declares its J1939 network foundation.

## 7. Electrical requirements

A portable profile may declare:

- desired run power state,
- ignition state,
- wake level,
- wake pulse and duration,
- electrical-state verification.

The profile only declares the requirement. Stage 2
`IBenchElectricalControl` remains the hardware boundary.

The profile also names the logical resource role used to reserve the relevant
power/I/O resource during the Bench Session.

No Raspberry Pi GPIO, relay, high-side switch or CAN-I/O module is selected in
Stage 3 foundation.

## 8. Minimal environment

A profile that carries Core capability `requires_environment` must request
`minimal_profile_environment`.

The common contract does not define a complete vehicle simulation.

The concrete profile runtime will later provide only the minimal surrounding
network behavior required by that DUT proof case.

## 9. Cyclic active control

A DUT that is both `cyclic_can` and `active_control` declares a
`CyclicControlRequirement`:

- logical CAN link,
- period,
- maximum lateness,
- command timeout,
- optional feedback timeout.

These values are common timing/safety metadata. They will be mapped to the
already-frozen Core cyclic actuator runtime.

Concrete CAN IDs, payload encoding, rolling counters, CRC/checksum/E2E,
engineering scaling and safe-stop frame rendering remain the responsibility of
the concrete profile program/endpoint. They are not generic schema algorithms.

## 10. Expected RX vs command semantics

The common definition can state which CAN traffic is expected through bounded
filters.

It does not try to standardize OEM command names, parameter scaling, diagnostic
DIDs or actuator payload fields.

Those semantics belong to the concrete profile module and will be exercised by
the proof cases after the common contract is accepted.

## 11. Stage 3 delivery sequence

### 3.0 — Definition and resolver foundation

- fixed-capacity `DutProfileDefinition`,
- strict validation,
- logical resource roles,
- CAN requirement resolution,
- Bench config handoff,
- DUT-registry identity matching,
- diagnostic-style and actuator-style synthetic proof tests.

**Implementation in progress on this branch.**

### 3.1 — Runtime profile contract

- bind a concrete profile runtime to `IDutSessionEndpoint`,
- define profile-owned startup/shutdown behavior,
- bind required protocol modules and cyclic program without direct driver
  access,
- preserve bounded execution contracts.

### 3.2 — Profile registration / selection

- minimal fixed-capacity discovery/selection mechanism if required by the
  application boundary,
- no dynamic plugin framework unless a real product requirement justifies it.

### 3.3 — Real proof profiles

Only after 3.0/3.1 are accepted:

1. MAN Sonceboz EGR actuator proof from retained evidence,
2. one ECU-class proof using existing CAN / ISO-TP / UDS or J1939 foundations.

The two proof cases will test whether the common contract is genuinely
DUT-neutral before further devices are added.

## 12. Explicit non-goals

Do not add in the generic Stage 3 foundation:

- concrete MAN EGR frames,
- concrete ECU identifiers/DIDs,
- VGT-specific rules,
- OEM checksum algorithms,
- GUI,
- physical adapter selection,
- full vehicle simulation,
- DoIP without a real DUT need,
- direct access to physical CAN drivers.

DUT_PROFILE_STAGE3_CORE_REVISION_REQUIRED=NO
DUT_PROFILE_STAGE3_BENCH_REVISION_REQUIRED=NO
DUT_PROFILE_STAGE3_PHYSICAL_DRIVER_ACCESS=NO
DUT_PROFILE_STAGE3_CONCRETE_DUT=NO
DUT_PROFILE_STAGE3_FOUNDATION=IN_DEVELOPMENT
