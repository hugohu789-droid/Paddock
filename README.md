# Paddock

A spatially explicit pastoral farm simulator — terrain, weather, pasture growth,
livestock and pests coupled on real NZ geospatial data. C++17 core, Qt/VTK 2D+3D.

> **Status: M2 complete — weather, soil water and pasture.** A year of
> weather drives a soil water bucket and a ryegrass/white clover sward, all
> three budgets close to 1e-9, a scenario bundle reproduces a run bit for bit,
> and the T3 gate compares the modelled season with DairyNZ's measured averages
> on every pull request. The seasonal shape correlates 0.97 with the measured
> unfertilised site; the magnitude is 11% low, and the growth parameters are
> still placeholders. See [docs/verify.md](docs/verify.md) before quoting any
> number this produces. The devlogs describe each milestone:
> [M1](docs/devlog/m1-skeleton-and-automation.md),
> [M2](docs/devlog/m2-weather-water-pasture.md).

## Run a year

```bash
cmake --preset default && cmake --build --preset default
./build/default/bin/paddock scenario run data/scenarios/canterbury-baseline
```

```
canterbury_baseline (engine 0.1.0, seed 20240701)
  2023-07-01 to 2024-06-30, 366 days
  weather   synthetic:canterbury_plains_example (1b4174317ab0)
  rainfall  720.8 mm
  et        580.2 mm
  drainage  92.7 mm
  growth    9605.9 kg DM/ha
  fixed N   37.2 kg N/ha
  closing   2283.9 kg DM/ha cover, 101.9 mm soil water, 14% legume
```

The run is a New Zealand farm year, 1 July to 30 June.

A bundle is one directory: the manifest, the weather, soil and sward
definitions, the master seed, and the SHA-256 of every input. Change one of
those files and the run is refused rather than quietly producing different
numbers. `--csv <file>` writes the daily series.

## Watch a year

With Qt6 and a VTK built against Qt6 (see [docs/setup.md](docs/setup.md)):

```bash
cmake --preset gui && cmake --build --preset gui
./build/gui/bin/paddock-gui
```

Named with no bundle it opens `data/scenarios/lincoln-lurdf`, found by walking
up from the working directory and from the executable. Name a directory to open
a different one.

Pasture cover, soil water, water stress or legume fraction across the farm, with
a colour scale fixed over the whole run and a timeline you can play. The soil
gradient in the example bundle is a demonstration until real soils arrive from
S-map in M3.

The panel on the left sets a run up and starts it: which scenario, which class
of stock, how many, and the two numbers the farmer manages to - the cover the
sward is not taken below, and the cover above which the farm rotates. It opens
on the bundle named on the command line, configured as that bundle configures
itself, so pressing Run without touching anything reproduces the published
scenario. A run with stock on it also writes the report, which the Report button
shows and saves.

What the panel does *not* let you edit is anything the bundle hashes - weather,
soil, sward. Those are what make a bundle reproducible, and a form that let them
be retyped would quietly take that away.

The map draws the fences too, and picks out the paddocks carrying stock that
day. There is a second view that draws the same farm on the ground it sits on,
with the fences draped over it - and for one farm that ground is measured
rather than invented:

```bash
cmake --preset desktop && cmake --build --preset desktop
python scripts/nz-elevation-snapshot.py --lon 172.470 --lat -43.641 \
  --collection canterbury/selwyn_2023 --out data/snapshots/lincoln-dem-1m.tiff
./build/desktop/bin/paddock-gui data/scenarios/lincoln-lurdf --terrain
```

The three-dimensional view opens on any farm. Without measured ground it draws
the farm flat and says so - under the map, in the report's Ground row, and on
stdout when it is run headless - because a flat picture of real ground and a
flat picture of absent ground look identical, and mean opposite things. A
snapshot that is present but hashes to something else is refused outright:
that is not this farm's ground.

That is Lincoln University's research dairy farm on 2023 LiDAR at 1 m, and it
looks almost flat because it is: the ground falls 6.2 m across 900 m, which is
what the Canterbury Plains do. The height control stretches it and says by how
much, because exaggeration makes every slope look steeper than it is. The
`desktop` preset is the one that has both the map and GDAL; see
[docs/setup.md](docs/setup.md).

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
data/   species, pastures, calibration docs/  ADRs, devlog, setup, verification, backlog
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
