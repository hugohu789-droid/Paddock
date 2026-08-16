# ADR 0003 — Core has zero external dependencies

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M1

## Context

The scientific claims of this project — conservation to 1e-9, deterministic
replay, validation against measured seasonal curves — are only as credible as
the ease of checking them. If reproducing a result requires GDAL, PROJ, Qt and
VTK to build first, almost nobody will reproduce it, and CI will spend its time
on package managers rather than on the model.

## Decision

`core/` is pure C++17 and the standard library. No GDAL, no Qt, no VTK, no
network, no third-party headers of any kind. Dependencies live in the modules
that need them: `gis/`, `viz/`, `app/`, `ai/`. Nothing points out of core.

Enforcement is mechanical, not cultural:

- `scripts/check-dependency-direction.sh` fails the T2 gate if a core source
  includes a forbidden header, or reaches for a global RNG or the wall clock.
- `core/CMakeLists.txt` contains no `find_package` and links no libraries.
- The three-platform CI matrix builds core and runs the full test suite without
  installing a single dependency.

gtest is the one build-time exception, and only for the test targets: it comes
from the vcpkg manifest's `tests` feature when a vcpkg toolchain is present, and
is fetched at a pinned commit (v1.18.0) otherwise. Both paths expose
`GTest::gtest_main`, so the choice is invisible to the test code.

## Consequences

- A machine with a compiler, CMake, Ninja and network access can run the whole
  scientific suite. Without network access it can still build core itself.
- `gis/` must translate GDAL types into `Raster<T>` and `Polygon` at its
  boundary rather than passing them through. That is deliberate: it keeps the
  model testable without geospatial fixtures.
- Convenience libraries that would be reasonable elsewhere — a logging library,
  a units library — are not available inside core, and adding one means
  revisiting this ADR rather than adding a line to `vcpkg.json`.
