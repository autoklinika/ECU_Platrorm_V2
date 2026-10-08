# ECU Platform V2 — Core V2 migration matrix

Date: 2026-10-06

Policy values:

- **RETAIN** — evidence/test vectors may be kept unchanged as reference.
- **REVALIDATE** — implementation may be reused only after adaptation and new
  Core V2 tests.
- **REBUILD** — concept remains useful, public contract/runtime semantics are
  redesigned.
- **EXCLUDE** — does not belong in generic Core V2.

| Current area | Policy | Core V2 decision |
|---|---|---|
| CAN frame validation/test vectors | RETAIN + REVALIDATE | Valuable regression vectors; move into V2 namespace/contracts only after V2 tests pass. |
| `ICanInterface` direct RX/TX contract | ENGINEERING PASS | Rebuilt as physical `ICanDriver` behind authoritative `CanBusRuntime`; protocols/profiles use routed RX and centralized TX, never direct driver ownership. |
| Linux SocketCAN adapter | RETAIN as reference | Platform adapter only; later implement new `ICanDriver` contract. |
| ISO-TP state machine | REVALIDATE | Revalidated on Core V2: routed RX through `ICanFrameSink`, deferred centralized TX through `CanBusRuntime`, fixed bounded storage, Classic CAN/CAN-FD and negative timing/flow-control tests. Normative ISO 15765-2 conformance remains module-gated. |
| Diagnostic transport abstraction | ENGINEERING PASS | Transport-neutral diagnostic contract is in Core V2 and UDS is exercised independently of its ISO-TP adapter. DoIP remains a separately gated transport module. |
| UDS client/service helpers | ENGINEERING PASS / NORMATIVE AUDIT OPEN | Transport-neutral UDS foundation and ISO-TP E2E tests pass; service expansion and ISO 14229 clause audit remain module-gated. |
| J1939 identifier/PGN helpers | ENGINEERING PASS / NORMATIVE AUDIT OPEN | Identifier/PGN/NAME, Address Claim, TP, diagnostics subset and CAN-FD foundations are implemented with separate standards gates. |
| Resource manager | ENGINEERING PASS | Fixed-capacity single-executor resource ownership with generation-safe leases; stale leases cannot release newer owners. |
| Command/state/event runtime | ENGINEERING PASS | Fixed topology after configuration freeze; typed command dispatch, caller-buffered revisioned state, monotonic event sequencing, declared execution bounds and explicit reentrancy rejection. |
| DUT identity/class/capability contract | ENGINEERING PASS | Core V2 models ECU, actuator, sensor, gateway and other automotive DUT classes without making diagnostics mandatory; raw/cyclic CAN are first-class capabilities. |
| Module/device registries | ENGINEERING PASS | Module and automotive-DUT registries are fixed after configuration freeze; DUTs use stable generation-bearing handles. Bench/UI peripherals remain outside generic Core. |
| Device classes camera/printer/robot | EXCLUDE from generic Core | Bench/UI peripherals belong to device services/adapters, not TRUCK/AGRI/OHV DUT domain foundation. |
| Actuator contract | REVALIDATE / ENGINEERING PASS | Core V2 now has deterministic cyclic raw-CAN execution with bounded profile rendering, cadence/lateness enforcement, command/feedback freshness, interlock ownership and safe-stop. Real MAN EGR/VGT profiles remain proof-case work outside generic Core. |
| Safety watchdog | ENGINEERING PASS | Core V2 monotonic DeadlineWatchdog fails closed on late kick, clock faults and expiry; expired state cannot be revived implicitly. |
| Trace/replay concepts | DEFER / NOT CORE FREEZE BLOCKER | User-selected capture/replay acquisition will be provided by separate hardware; Core event/frame identities remain suitable for later import/replay adapters. |
| Simulated CAN | TEST-ADAPTER GATED / NOT CORE FREEZE BLOCKER | Core logic is already hardware-independent through `ICanDriver`; current regression fakes prove the boundary. A reusable simulator is a later tooling adapter, not generic Core runtime semantics. |
| Network stream/datagram contracts | USE-CASE GATED / NOT CORE FREEZE BLOCKER | DoIP remains first-class when required by a real DUT, but stream/socket platform adapters are not required to freeze the CAN-centric generic Core contracts. |
| Storage abstraction | PLATFORM GATED / NOT CORE FREEZE BLOCKER | Persistent configuration is a platform/product service; generic Core runtime contracts remain storage-independent. |
| Security extension points | PRODUCT GATED / NOT CORE FREEZE BLOCKER | Root of Trust, licensing and release integrity remain product-security gates; they must not alter DUT/protocol/runtime semantics. |
| SAC module | RETAIN as reference only | Product module is not part of Core V2 foundation; rebase later. |

## Migration rule

No current implementation is copied into Core V2 merely because its old tests
pass.

For every migrated component:

1. declare its V2 responsibility and dependency boundary,
2. assign standards applicability,
3. port or rewrite under the V2 contract,
4. add V2 negative/boundary/concurrency tests,
5. compare against retained reference vectors,
6. run independent review,
7. only then retire the corresponding old-Core dependency.
