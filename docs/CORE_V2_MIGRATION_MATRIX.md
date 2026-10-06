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
| `ICanInterface` direct RX/TX contract | REBUILD | Split physical driver from central bus runtime. Protocols may not consume driver RX directly. |
| Linux SocketCAN adapter | RETAIN as reference | Platform adapter only; later implement new `ICanDriver` contract. |
| ISO-TP state machine | REVALIDATE | Preserve algorithms/test vectors; replace direct CAN-driver dependency with routed RX + centralized TX port. |
| Diagnostic transport abstraction | REVALIDATE | Direction is correct; rebase on V2 transport/lifecycle semantics. |
| UDS client/service helpers | REVALIDATE | Keep transport independence; redo lifetime/timing and standards evidence. |
| J1939 identifier/PGN helpers | REVALIDATE | High-value heavy-duty primitive; expand toward Address Claiming and TP only after clause audit. |
| Resource manager | REBUILD | Keep generation-safe ownership concept; align with frozen runtime graph and domain-aware resources. |
| Command/state/event runtime | REBUILD | Current proof is useful, but lifetime/versioning/executor semantics must be explicit. |
| Module/device registries | REBUILD | Configuration-time stable handles; no unsafe runtime removal. |
| Device classes camera/printer/robot | EXCLUDE from generic Core | Bench/UI peripherals belong to device services/adapters, not TRUCK/AGRI/OHV domain foundation. |
| Actuator contract | REBUILD | Keep safe-stop principle; add deterministic scheduler/interlock ownership model. |
| Safety watchdog | REBUILD | Late kick must fail closed; expiry cannot be revived implicitly. |
| Trace/replay concepts | REVALIDATE | Keep bounded flight-recorder idea; move to V2 event/frame identities. |
| Simulated CAN | REBUILD around V2 driver | Keep testability concept and useful test vectors. |
| Network stream/datagram contracts | REVALIDATE/REBUILD | Reassess against DoIP needs and explicit connection/lifetime state. |
| Storage abstraction | REVALIDATE | Keep platform-neutral boundary; define versioned atomic config semantics later. |
| Security extension points | REBUILD | Tie to product identity, signed capabilities and integrity lifecycle without choosing hardware yet. |
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
