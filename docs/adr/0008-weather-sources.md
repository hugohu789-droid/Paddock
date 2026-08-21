# ADR 0008 — Weather sources: generated and replayed in core, fetched by script

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M2

## Context

Every data source is meant to ship three adapters behind one port: synthetic,
file and live. For weather that means a generator, a replay of a local snapshot,
and NIWA CliFlo itself.

Two constraints pull against a straightforward "live adapter":

1. **Core takes no dependencies.** A live adapter needs HTTP, so it cannot live
   in core, and core is where the simulation runs.
2. **A run must be reproducible.** If a simulation can reach the network
   mid-run, then the same bundle can produce different results on different
   days, and a golden baseline means nothing.

CliFlo adds a third: access is tied to a registered account, and its exports
arrive one datatype per file.

## Decision

The port (`WeatherSource`) and two adapters live in core, because both need
nothing but the standard library:

- **`SyntheticWeatherSource`** generates weather from monthly climate normals.
  Each day is drawn from an engine keyed by the master seed **and the date**, so
  fetching March alone returns exactly the March a full-year fetch returns. That
  property is asserted by a test; it is what lets a scenario start mid-year.
- **`SnapshotWeatherSource`** replays a local CSV, one row per day, and records
  the file's SHA-256 as provenance. A gap in the dates is an error, not a
  truncated series: to the soil water bucket a missing day is indistinguishable
  from a dry one. So is a request that runs past the snapshot's coverage.

**The live adapter is a script, not a class.** `scripts/cliflo-snapshot.py`
merges CliFlo exports into the snapshot format, checks the series is contiguous,
and writes the SHA-256 that a scenario bundle pins. Downloading stays manual:
CliFlo's terms tie access to the user's own account, and a script that drives
someone's login is a script that eventually does it without them noticing.

`paddock source test <snapshot.csv>` is the CLI face of the port. It prints what
the source is, what it covers, and either that it is ready or what to do about
it. There is no GUI panel for this and there may never be one.

## Consequences

- No simulation run can touch the network, by construction rather than by
  convention.
- A bundle that pins a hash either replays exactly the data it was built on or
  fails loudly with both hashes.
- Snapshots are not committed (`data/snapshots/` is gitignored); the script and
  the hash are. A collaborator reproduces a bundle by re-fetching and checking
  the hash matches.
- The CliFlo column mappings in the script are marked PLACEHOLDER until they
  have been checked against a real export of each datatype (docs/validation/verify.md,
  item 7). The script prints the headings it actually found and accepts
  `--column HEADING=field`, so an unexpected export is a five-second fix rather
  than a code change.
- **The generator draws wet days independently.** Real rainfall comes in
  spells, and a Markov chain would capture that — but a chain's state depends on
  the previous day, which would break the date-keyed reproducibility above
  unless it is anchored somewhere arbitrary. Tracked as E4 in docs/validation/verify.md;
  drought scenarios in M4 need it, and by then the anchor can be part of the
  scenario bundle. Real-year replay is unaffected and is the honest path for
  anything where wet spells matter.
