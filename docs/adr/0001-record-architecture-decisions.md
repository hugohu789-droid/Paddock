# ADR 0001 — Record architecture decisions

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M1

## Context

Paddock couples a scientific model with a desktop application. Decisions taken
early — the coordinate system, the dependency direction, how randomness is
seeded — are cheap now and expensive later, and their reasons are invisible in
the code that results from them. A reader who cannot see why a constraint
exists will eventually remove it.

## Decision

Every significant decision is recorded as a short file in `docs/adr/`, numbered
in sequence, in the format used here: context, decision, consequences. An ADR is
never rewritten once accepted; it is superseded by a later one that says so.

Ordinary code review comments, naming choices and implementation details do not
get an ADR. A decision qualifies when reversing it would touch several modules,
break reproducibility of existing results, or contradict one of the
non-negotiable principles in `CLAUDE.md`.

## Consequences

- The reasoning behind a constraint outlives the conversation that produced it.
- A milestone's devlog entry can point at the ADRs it created instead of
  re-explaining them.
- ADRs are written when the decision is made, not at the end of a milestone.
