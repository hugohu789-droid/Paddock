# ADR 0013 — Release packages are built in CI on Linux and macOS, and by hand on Windows

- **Status:** accepted
- **Date:** 2026-08-31
- **Milestone:** M5

## Context

M5's acceptance is a person, not a checksum: "a stranger downloads the
installer, loads the sample farm, and watches a year of weather-driven farm
life within 30 seconds". `CLAUDE.md` specifies T5 as three-platform builds
through `windeployqt`/`macdeployqt`, packaged with a sample farm dataset,
published as a GitHub Release, with Doxygen going to GitHub Pages.

Three facts shape how much of that a tag can do on its own.

**Qt and VTK are cheap on two platforms and expensive on the third.** Ubuntu
26.04 ships `qt6-base-dev`, `qt6-charts-dev` and `libvtk9-qt-dev`, and the T2
map-view job already builds against them on every pull request. Homebrew ships
`qt` and `vtk` as bottles. Windows has neither: vcpkg builds both from source,
and the measurement recorded in the `gis-windows` job of `ci.yml` — 1h3m for
GDAL alone against 1m14s on Linux — is the same fan-out with Qt and VTK added
on top. A release job that takes hours and fails at hour three is not a gate
anyone will run.

**The data a release may carry is already decided.** `scripts/check-data-licences.sh`
keeps NIWA and S-map data out of the repository, and the same reasoning governs
an archive that leaves the machine. LiDAR and cadastre snapshots are fetched,
never committed, so they are not in a release either. This was tested rather
than assumed: with `data/snapshots/` moved aside, both the CLI and the desktop
GUI run a full year, report `ground modelled flat`, and name the script that
fetches the surface. A package without the elevation is therefore a working
package, not a broken one.

**A package that was assembled is not a package that runs.** The failure mode
here is a missing runtime library, and it appears on the downloader's machine
rather than the builder's. `proj.db` is the sharpest instance: PROJ looks beside
its own library and then in `../share/proj`, and putting it anywhere else turns
every coordinate transform into a runtime error — which is exactly how
`GisEnvironmentTest.ProjCanResolveNztm2000` first went red on Windows CI.

## Decision

**Linux and macOS packages are built by the T5 workflow on a version tag.
Windows packages are built with the same script on a developer machine and
attached to the draft release by hand.**

`scripts/package-release.sh` is one script for all three platforms. It reads Qt,
VTK and the vcpkg tree out of the build directory's own `CMakeCache.txt`, so a
developer machine and a CI runner need no knowledge of each other's paths. It
deploys Qt with `windeployqt` or `macdeployqt`, copies VTK and the geospatial
fan-out beside the binary, puts `proj.db` in `share/proj` where PROJ will look
for it, and copies every `data/` directory except the snapshots.

Two things the script refuses to do on trust:

- **The version is asked of the binary** (`paddock --version`), not read from a
  header, so an archive cannot claim a number the program inside it would not
  print.
- **`NOTICE.txt` is generated from the `.provenance.json` files** shipped beside
  the data, quoting the licensor's own attribution wording. Copying that wording
  into the script is how an attribution silently stops matching the data it
  covers.

Before archiving, the script runs a full simulated year from inside the staged
tree. A staging directory that cannot run is a failure of the packaging step,
not a discovery for whoever downloads it.

The T5 release is created as a **draft**. That is what makes the hand-attached
Windows package possible without racing people who are already downloading an
incomplete release.

## Consequences

The Linux tarball links against the distribution's own Qt6 and VTK rather than
carrying copies. That is weaker than the other two platforms and is written down
as weaker: making it self-contained needs `linuxdeploy` and an AppImage, which
is a decision of its own rather than something to slip into a packaging script.

The Windows step is manual, so it can be forgotten. What makes that survivable
is that the script refuses to produce an archive it could not run, and that the
release is a draft until someone looks at it. What would reverse this decision
is a Windows runner with Qt and VTK available as binaries — either an action
that installs official Qt binaries paired with a prebuilt VTK, or a vcpkg binary
cache in a scope a tag build can read. Either one turns the manual step into a
matrix row and nothing else in the script changes.

Doxygen publishes to GitHub Pages on a tag only. The documentation job itself
runs on every dispatch so that a broken `Doxyfile` is found while rehearsing
rather than while releasing.
