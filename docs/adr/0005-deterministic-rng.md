# ADR 0005 — Random numbers are keyed by entity ID, never by iteration order

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M1

## Context

A scenario bundle promises that a run reproduces bit-for-bit. The usual way that
promise breaks is not a wrong algorithm but a changed traversal: one shared
generator, drawn from in whatever order a loop happens to visit entities. Sort
the herd differently, split the paddock list across threads, add an entity, and
every draw downstream shifts.

Parallelism is explicitly out of scope for v1, but the door is meant to stay
open, and reordering is not only a threading problem — it happens the first time
a container changes type.

## Decision

- There is no global generator. `std::rand`, `std::random_device` and the wall
  clock are banned inside `core/`, and `scripts/check-dependency-direction.sh`
  fails the build if one appears.
- Every run has one master seed, recorded in the scenario bundle.
- `RngStreams` derives a stream seed from the master seed, a fixed subsystem
  number and a **key that is an entity ID**, using splitmix64 as the mixing
  function. `Subsystem` values are part of the reproducibility contract and are
  never renumbered.
- Each subsystem holds its own engine; engines are created once per entity and
  advanced, never re-seeded per step (re-seeding daily would repeat one draw
  forever).
- `DeterminismTest` asserts all of it: same seed gives bit-identical results,
  different seeds do not, and shuffling or reversing the work queue changes
  nothing.

## Consequences

- A future parallel or partitioned traversal cannot change results, which is
  what keeps the 1e-9 conservation assertions and golden baselines meaningful.
- Entity IDs are never reused within a run, because reuse would alias two
  animals onto one stream.
- Creating an engine per entity per subsystem costs a splitmix64 mix and an
  `mt19937_64` initialisation. At thousands of entities stepping daily this is
  not the bottleneck; if it becomes one, engines get cached, not shared.
- **Known gap:** the standard library's distributions
  (`std::uniform_real_distribution` and friends) are implementation-defined, so
  bit-identical output holds within one platform and standard library, not
  across the CI matrix. Cross-platform golden baselines need core's own
  distribution transforms on top of the engine. Tracked as E1 in
  [docs/validation/verify.md](../verify.md), to be closed in M2 before the first golden
  baseline is pinned.
