# ECU Platform V2 — ISOBUS Working Set lifecycle status

Date: 2026-10-07
Scope: AGRI / read-only ISO 11783 Working Set topology
Status: **ENGINEERING FOUNDATION PASS / NORMATIVE CLAUSE AUDIT OPEN**

## Purpose

This layer converts the existing Working Set Master (WSMSTR, PGN 65037) and
Working Set Member (WSMEM, PGN 65036) codecs into a bounded read-only topology
model.

It is intentionally above the ISO 11783 network-management Control Function
registry. It does not infer membership from source addresses alone.

## Message semantics used by the engineering foundation

A WSMSTR declaration identifies a working set by the source address of its
master and declares the total number of Control Functions in the set, including
the master itself.

The master then identifies each non-master member with WSMEM messages. Therefore
the expected number of distinct WSMEM messages is:

`declared_total_members - 1`

A WSMEM is associated with a working set by the source address of the master
that sent it. The payload contains the member's 64-bit NAME. The member's
current source address is obtained separately from network management / Address
Claim state.

## Implemented lifecycle model

The lifecycle tracks up to 16 simultaneous Working Set declarations. Each
tracked set can represent the full codec range of up to 250 declared Control
Functions using fixed-capacity storage.

No dynamic allocation, worker threads, sleeps or platform APIs are used.

Each Working Set has one of these explicit states:

- `assembling` — fewer distinct WSMEM declarations than expected,
- `incomplete` — declaration count is complete but one or more required NAMEs
  do not currently have a valid unique claimed address,
- `complete` — exact declaration count and every required NAME resolves to one
  current unambiguous claimed address,
- `conflict` — structural inconsistency or ambiguous network address mapping,
- `network_incomplete` — required topology cannot be resolved after the shared
  Control Function registry has exhausted capacity,
- `stale_master_address` — the master's current Address Claim no longer matches
  the source address from the Working Set declaration.

## Safety and consistency properties

The lifecycle:

- counts the master as part of the declared total,
- requires exactly `total - 1` distinct member NAMEs before completeness,
- treats repeated identical WSMEM messages as idempotent,
- rejects an additional distinct member beyond the declared count,
- rejects the master NAME being reused as a non-master member,
- does not guess membership when WSMEM arrives before WSMSTR,
- keeps membership identity by NAME when a member changes source address,
- invalidates a declaration when the master changes source address,
- reuses the same bounded slot when that master sends a fresh WSMSTR from its
  new address,
- can retain an unresolved WSMSTR declaration until the master's Address Claim
  is later observed,
- distinguishes ordinary incomplete membership from globally incomplete
  network topology,
- propagates ambiguous source-address claims as a Working Set conflict.

A fresh WSMSTR is a new declaration generation and clears the previous member
list for that Working Set.

## Explicitly outside this engineering gate

This layer does not implement:

- normative Working Set timing/repetition deadlines,
- timeout-based deletion or ageing of a Working Set,
- transmission scheduling for WSMSTR/WSMEM,
- VT object-pool lifecycle,
- Task Controller / DDOP lifecycle,
- TIM,
- Auxiliary Control,
- process-data control,
- machine actuation.

Timing and ageing behavior require clause-level verification against the
licensed normative ISO 11783 text before being introduced as a standards claim.

## Executable evidence

Regression coverage verifies:

- a three-CF set becomes complete only after two distinct WSMEM messages,
- the master is included in total-member accounting,
- a single-CF declaration requires no WSMEM,
- missing member Address Claim produces `incomplete`,
- a later Address Claim promotes the set to `complete`,
- member Cannot Claim removes completeness without losing declared membership,
- duplicate WSMEM is idempotent,
- excess distinct WSMEM is a structural conflict,
- master-as-member is rejected,
- orphan WSMEM is not guessed into a set,
- master source-address migration marks the old declaration stale,
- a fresh WSMSTR after master migration starts a new generation,
- ambiguous member addresses propagate as conflict,
- shared network-registry overflow produces `network_incomplete`,
- Working Set table exhaustion is explicit,
- a WSMSTR seen before the master's Address Claim can be bound later,
- malformed WSMSTR never creates lifecycle state.

The complete Core V2 local validation passes Debug, Release, Generic non-Linux
and ASAN/UBSAN matrices with all current tests passing.

CORE_V2_ISOBUS_WORKING_SET_LIFECYCLE_ENGINEERING=PASS
CORE_V2_ISOBUS_WORKING_SET_LIFECYCLE_CLAUSE_AUDIT=OPEN
CORE_V2_ISOBUS_WORKING_SET_LIFECYCLE_EXTERNAL_CONFORMANCE=NO
