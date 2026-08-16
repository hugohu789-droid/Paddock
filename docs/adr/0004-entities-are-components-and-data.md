# ADR 0004 — Entities are components and data, not a class hierarchy

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M1

## Context

The obvious first design for a farm simulator is an inheritance tree:
`Animal → Ruminant → Sheep`. It survives contact with New Zealand for about a
day. Red deer are farmed stock behind a fence and a browsing pest on the other
side of it, in the same catchment, in the same simulated year. Possums are pests
that carry a disease that matters to cattle. A tree forces one identity per
species; the model needs the identity to depend on context.

The same argument applies to pastures, diseases and pests, and it has a second
edge: a hierarchy makes adding a species a code change, which puts it out of
reach of the agronomist who knows the numbers.

## Decision

Entities are identifiers. Behaviour comes from the components attached to them
(`Position`, `Grazer`, `Liveweight`, `Health`, `Reproduction`, `Owned`,
`Labour`, `SpeciesRef`) and from the TOML definition that `SpeciesRef` names.

- `World` owns entities; `ComponentStore<T>` owns one component type.
- Ownership is the `Owned` component. Farmed deer carry it, wild deer do not,
  and nothing else about them differs.
- Extending the simulator to a new species, pasture, disease or pest means
  adding a data file under `data/`, not a class.
- If a change wants a new entity subclass, that is a signal to re-express it as
  components plus data.

Storage is a sorted dense array per component type, keyed by entity ID.

## Consequences

- Traversal order is ascending entity ID, always, whatever order entities were
  created or components attached in. This is what makes [ADR
  0005](0005-deterministic-rng.md) enforceable in practice.
- Insertion is O(n) in the number of entities holding that component. At the
  target scale — thousands of animals, not millions — the determinism is worth
  more than the asymptotics, and the alternative (hash maps) would reintroduce
  iteration-order nondeterminism.
- Component lookup goes through `std::type_index`, so a component type is
  identified by its C++ type rather than by a registry of names. Test code can
  therefore define its own component types, which the conservation harness does.
- There is no compile-time check that an entity carries the components a process
  needs; processes must handle absence. That is the price of context-dependent
  identity.
