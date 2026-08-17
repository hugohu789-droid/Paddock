# ADR 0011 — GDAL and PROJ from system packages on Linux and macOS, vcpkg on Windows

- **Status:** accepted
- **Date:** 2026-08-17
- **Milestone:** M3

## Context

M3 needs `gis/` to read LINZ DEM, cadastre and waterway data and to transform
coordinates (`CLAUDE.md`: "gis/ GDAL · PROJ · GEOS", "All internal computation
in NZTM2000 (EPSG:2193)"). The question is where GDAL and PROJ come from on the
three platforms this project builds on, and it is the same question ADR 0010
answered for VTK — with the same failure mode if it is answered from assumption
rather than from package listings.

What the distributions actually ship:

| Platform | Source | GDAL | Notes |
|---|---|---|---|
| [Ubuntu 24.04](https://packages.ubuntu.com/noble/libgdal-dev) | `libgdal-dev` | 3.8.4 | depends on `libproj-dev (>= 6.0.0)`, `libgeos-dev` |
| [Ubuntu 26.04](https://packages.ubuntu.com/resolute/libgdal-dev) | `libgdal-dev` | 3.12.2 | same dependency chain |
| macOS | Homebrew `gdal` | current | pulls `proj` as a dependency |
| Windows | vcpkg `gdal`, `proj`, `geos` | manifest | already declared as the `gis` feature in `vcpkg.json` |

Both Ubuntu releases in this project's CI clear the floors the code needs, so
the core build matrix does not have to move.

## Decision

**Linux and macOS take GDAL and PROJ from the system package manager; Windows
takes them from vcpkg.**

- `gis/CMakeLists.txt` requires `GDAL 3.0` and `PROJ 6.0` and links them
  **PRIVATE**. A PUBLIC link would put GDAL's include directories on every
  consumer of `Paddock::gis`, and "GDAL types stop at this boundary" would hold
  only by convention. PRIVATE makes it hold by link error.
- The `gis` job builds and tests on all three platforms. On Windows the vcpkg
  binary archives are cached, because building GDAL from source is the
  expensive part and it is identical from run to run.
- `PADDOCK_BUILD_GIS` stays **OFF** by default, as it has been since M1. A
  developer working on pasture or livestock does not need a geospatial stack
  installed, and `cmake --preset default` must keep succeeding on a clean
  machine without one.

### Why the version floors are what they are

PROJ 6 is the release that moved the coordinate operation tables out of the
source tree into `proj.db` and introduced the API `Environment.cpp` uses. GDAL 3
is the release built on it — and the release that started honouring the
axis order declared by the authority.

That second change matters more in New Zealand than almost anywhere:
**EPSG:2193 declares its axis order as (northing, easting)**. A GDAL 3 build
therefore returns coordinates in the opposite order from the (easting, northing)
a New Zealand reader expects, unless the traditional order is requested
explicitly. The failure is not a crash. It is a farm that appears in the Tasman
Sea, or worse, a farm that appears plausibly placed because both numbers are
seven digits. Task #17 pins this down with round-trip tests at published
control points; this ADR records why the floor exists.

## Consequences

- Three package sources to keep working instead of one. The `gis` job exists to
  make that a build failure rather than a surprise, and it reports the version
  each platform actually installed before it configures.
- `proj.db` is the file this decision really buys. PROJ links and runs without
  it and fails every transform at the point of use — the most common way a
  working geospatial build stops working on another machine. Distribution
  packages and vcpkg both ship it; a hand-built PROJ often does not end up with
  it on the search path. `paddock_gis_tests` asserts EPSG:2193 resolves, and
  prints the search path when it does not.
- A developer on a distribution older than Ubuntu 24.04 may have GDAL 2, which
  this build rejects at configure time rather than at run time. `docs/setup.md`
  says so.
- GEOS is declared in the vcpkg manifest but not yet required by
  `find_package`. It arrives with the polygon work in task #21; requiring it now
  would fail configuration for a dependency nothing calls.

## Alternatives considered

**vcpkg everywhere.** One source, one version, identical on all three
platforms — genuinely attractive, and it is what `CLAUDE.md` already assumes for
the GUI. Rejected because GDAL from source is a long build with a wide
dependency fan-out, and paying it on Linux and macOS buys nothing that the
distribution package does not already provide, `proj.db` included. It stays the
fallback if the distribution packages ever diverge in a way the tests catch.

**Conda or a prebuilt SDK.** Rejected: another package manager in the build
instructions, for a problem two lines of `apt` and `brew` already solve.
