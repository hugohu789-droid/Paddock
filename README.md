# Paddock

A spatially explicit pastoral farm simulator — terrain, weather, pasture growth,
livestock and pests coupled on real NZ geospatial data. C++17 core, Qt/VTK 2D+3D.

> **Status: M1 — skeleton and automation.** The repository is deliberately empty
> of business logic. What exists is the kernel's type foundation and a real
> build/test/analysis pipeline, so that every later milestone lands against
> working gates. See [docs/devlog/m1-skeleton-and-automation.md](docs/devlog/m1-skeleton-and-automation.md).

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
