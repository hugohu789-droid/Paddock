# ADR 0009 — A `config/` module, outside core, owns TOML

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M2

## Context

Two principles meet here and appear to contradict each other:

- **Core has zero external dependencies** ([ADR 0003](0003-core-has-zero-dependencies.md)).
- **Config as data first**: every simulator object is configurable via validated
  TOML with line-precise error messages.

Line-precise errors need a parser that tracks source positions. toml++ does;
writing one by hand would be a poor use of a milestone. But toml++ cannot go
into core.

The module layout in `CLAUDE.md` lists `core/`, `gis/`, `viz/`, `app/`, `ai/`
and `tests/`. None of them is the right home: `app/` would mean a headless
scenario runner could not read its own configuration, and putting it in `gis/`
would tie farm definitions to GDAL.

## Decision

A new module, `config/`, sits between the TOML files and core:

- It depends on core and on toml++; **core does not depend on it**, and core
  never sees a TOML type or parses text.
- It returns the plain parameter structs core already defines
  (`SyntheticWeatherParameters`, `SoilWaterParameters`, `SwardParameters`), so
  core's own `validation_error()` remains the single definition of what a valid
  parameter set is. `config/` reports those messages with a file, line and
  column attached rather than restating them.
- Errors are a `ConfigError` reading `path:line:column: what is wrong` — the
  form every editor and terminal can already jump to.
- **Unknown keys are rejected.** A silently ignored `runoff_fration = 0.05`
  would parse, validate and run, and the farm would simply never shed water.
  The error names the key and lists the ones that exist.
- toml++ follows the gtest precedent: from vcpkg when a manifest install is in
  play, fetched at a pinned commit otherwise. It is header-only, so this costs
  a download and no build time.

The example definitions under `data/` are loaded by a test, so a schema change
breaks the build rather than the first person who tries the simulator.

## Consequences

- The module list in `CLAUDE.md` is now six modules plus `config/`. That
  document describes the intent; this ADR records the one place the intent
  needed an extra box.
- Validation lives in two layers by design: types (ranges and relationships)
  and files (presence, spelling, position). Both report the same way.
- A GUI configuration panel, when one is built, edits these files and nothing
  else — it cannot acquire its own idea of what is valid.
- Anything that needs configuration must now depend on `config/` rather than
  parse for itself. That is the point.
