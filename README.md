# Paddock

A spatially explicit pastoral farm simulator — terrain, weather, pasture growth,
livestock and pests coupled on real NZ geospatial data. C++17 core, Qt/VTK 2D+3D.

> **Status: M2 in progress — weather, soil water and pasture.** A year of
> weather now drives a soil water bucket and a ryegrass/white clover sward, all
> three budgets close to 1e-9, and a scenario bundle reproduces a run bit for
> bit. The growth parameters are still placeholders: see
> [docs/verify.md](docs/verify.md) before quoting any number this produces.
> M1's foundation is described in
> [docs/devlog/m1-skeleton-and-automation.md](docs/devlog/m1-skeleton-and-automation.md).

## Run a year

```bash
cmake --preset default && cmake --build --preset default
./build/default/bin/paddock scenario run data/scenarios/canterbury-baseline
```

```
canterbury_baseline (engine 0.1.0, seed 20240701)
  2023-01-01 to 2023-12-31, 365 days
  weather   synthetic:canterbury_plains_example (1b4174317ab0)
  rainfall  815.0 mm
  et        638.7 mm
  drainage  205.7 mm
  growth    10567.8 kg DM/ha
  fixed N   46.2 kg N/ha
  closing   4015.6 kg DM/ha cover, 19.7 mm soil water, 14% legume
```

A bundle is one directory: the manifest, the weather, soil and sward
definitions, the master seed, and the SHA-256 of every input. Change one of
those files and the run is refused rather than quietly producing different
numbers. `--csv <file>` writes the daily series.

## What it is

Paddock models a New Zealand pastoral farm as a coupled system: terrain and soil
from LINZ and Manaaki Whenua data, weather from NIWA CliFlo, a soil-water bucket,
pasture growth, grazing livestock, pests and diseases, and the management
decisions that tie them together. It is meant to be verifiable as an
agricultural systems model and playable as a farm-management simulation.

## Design commitments

| Commitment | What it means in the code |
|---|---|
| Core has zero external dependencies | `core/` is pure C++17 + stdlib. A machine with only a compiler builds and runs the scientific test suite. Enforced by `scripts/check-dependency-direction.sh` in CI. |
| Determinism | Every random draw comes from an injected engine; streams are keyed by entity ID, never by iteration order. Same seed → bit-identical output, asserted by a test. |
| Conservation | Dry matter, water and nitrogen budgets close to 1e-9 over a 365-day closed run, on every commit. |
| Species and pests are data | Entities are components plus TOML definitions. No `Animal → Ruminant → Sheep` inheritance. |
| Published parameters only | Every model parameter cites a source or is marked `PLACEHOLDER` and tracked in [docs/verify.md](docs/verify.md). |

## Layout

```
core/   pure C++17 simulation kernel   gis/   GDAL · PROJ · GEOS
viz/    VTK                            app/   Qt6 desktop application
ai/     optional LLM features          tests/ gtest + benchmarks
data/   species, pastures, calibration docs/  ADRs, devlog, setup, verification
```

Dependency direction is one-way: `gis → core`, `viz → core`, `app → core`,
`ai → core`, `tests → core`. Nothing points out of core.

## Build

```bash
cmake --preset default && cmake --build --preset default && ctest --preset default
```

`CMakePresets.json` is the single source of build truth — the command line,
VS Code and Qt Creator all consume it. Full clean-machine instructions for all
three entry points are in [docs/setup.md](docs/setup.md).

## Licence

Not yet chosen; see [docs/verify.md](docs/verify.md) for the open items that
gate publication of bundled data.
