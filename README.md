# Paddock

A spatially explicit pastoral farm simulator for New Zealand conditions, built
in C++17 with Qt 6 and VTK.

Paddock simulates how weather, soil water, pasture growth, irrigation, grazing
and farm-management rules interact day by day across a farm — and lets any one
paddock be opened up and followed through the year.

![The Paddock simulator: a Lincoln farm on LiDAR ground, one paddock selected, its day open in the inspector](docs/images/paddock-main.png)

## What Paddock Does

- Simulates a pastoral farm day by day over a full New Zealand farm year
- Grows pasture cell by cell across real paddock boundaries, not as a farm mean
- Tracks the soil-water balance and what dryness costs the pasture
- Runs rule-based irrigation and reports the decision it made each day
- Moves stock between paddocks under a farmer that decides from the state of the farm
- Runs several management scenarios on the same weather and compares them
- Opens any paddock at any day of the run and explains what was done to it
- Exports the year as Markdown, CSV or PDF

## Why Paddock

Pastoral farms are dynamic systems.

Weather affects soil moisture. Soil moisture affects pasture growth. Pasture
availability affects grazing. Management decisions affect what happens next.

Paddock was built to make those interactions visible and testable through time
and space. It aims at transparent simulation, scenario comparison and
explainable management decisions rather than black-box prediction — and at
saying plainly which of its own numbers carry weight, which is what
[docs/verify.md](docs/verify.md) is for.

## Key Capabilities

### Weather

- Daily rainfall, temperature, solar radiation and wind
- Real recorded years replayed from a snapshot, or a synthetic generator, behind one interface
- Sun position and clearness worked out from the date and latitude

### Soil Water

- Daily soil-water balance with evapotranspiration and drainage
- Available water as a share of what the soil can hold
- FAO-56 water stress: growth held back as the root zone dries

### Pasture

- Daily growth, and cover carried per cell
- Seasonal growth driven by temperature and radiation
- Water-limited growth
- Ryegrass with white clover, and the nitrogen the clover fixes

### Grazing

- Livestock energy demand from liveweight, and what the ground costs to walk
- Rotation: a mob moves when it has gone short or after its maximum graze days
- A cover floor the farmer will not graze below
- Rest tracked per paddock
- Bought feed when the farm cannot both carry the stock and hold the cover

### Irrigation

- Soil-moisture trigger and refill target
- Maximum application depth and a return interval
- Application efficiency, and what reaches the root zone
- Daily and annual totals, events, and water pumped

## Scenario Comparison

Paddock runs several farm-management scenarios against the same farm and the
same weather, so a difference between them is a difference between the rules
rather than between the seasons they met.

The comparison below is one variable: same farm, same recorded weather, same
stock, and irrigation on in one arm and off in the other. It is produced by
`paddock-gui <bundle> --smoke --compare`, and CI runs it on every pull request.

| Metric | Rain-fed | Irrigated |
|---|---:|---:|
| Pasture grown (kg DM/ha) | 11 355 | 15 848 |
| Mean cover (kg DM/ha) | 3 502 | 4 385 |
| Lowest cover (kg DM/ha) | 1 883 | 2 255 |
| Days growth held back by dry soil | 240 | 0 |
| Irrigation applied (mm) | 0 | 369 |
| Water pumped (ML) | 0.0 | 295.6 |
| Evapotranspiration (mm) | 554 | 842 |

The table the application produces also lists what differed between the
scenarios, so the reader can see which single setting the comparison turned on.

## Explainable Paddock Inspector

Any paddock can be clicked and inspected at any point on the timeline. The
selection stays put while the year plays, so one paddock can be followed through
the season.

![The paddock inspector: pasture, current water, the irrigation decision, and grazing](docs/images/paddock-inspector.png)

The inspector separates what the soil is **now** from what it was **when the
decision was made**, because they are not the same soil:

```
Pasture
Cover                     5549 kg DM/ha
Growth today              90.2 kg DM/ha

Current
Available water                     91%
Water growth factor                1.00

Today's irrigation
Before irrigation                   50%
Trigger                             50%
Applied                         21.1 mm
Target                              85%
To date                          246 mm
↓ at or below the trigger
```

A paddock watered at 50% often ends the day at 91% — the rain, the grass and the
water it was just given all land between the two figures. Showing only the
evening number beside a 50% trigger would describe a farm that waters ground
which is already wet.

Where the schedule held water back, the inspector prints the schedule's own
recorded reason — "the profile is still wetter than the trigger", "watered too
recently" — rather than working one out afterwards. The GUI computes no
management decisions of its own.

Grazing is reported as fact rather than verdict: stock on it today, days rested,
and the minimum rest the farmer aims at. This farm has no per-paddock
"grazeable" test to report — a mob moves when it has gone short or has been on
its paddock long enough, and goes to whichever free paddock has rested longest —
so the inspector states that rule instead of inventing a threshold.

## Spatial Visualisation

Paddock draws farm state across the ground rather than reporting only farm-wide
averages. The layers available today are:

- Pasture cover
- Growth today
- Soil moisture
- Available water
- Irrigation today
- Irrigation to date
- Water stress
- Legume fraction
- Slope

The 3D view draws the farm on its own LiDAR surface where the scenario names
one, with the weather of the day over it and the layers separable into a stack
so one state can be read against another over the same ground.

## Simulation Through Time

The run advances a day at a time through the farm year, 1 July to 30 June. Every
day's rasters are kept, so the timeline scrubs and plays without re-simulating —
and shows the same numbers every time it is dragged.

The chart carries two quantities at a time, each owning an axis, with the days
that were irrigated marked underneath. A selected paddock stays selected while
the timeline plays, and the inspector follows it.

## Architecture

```
Qt Application (app/)
      |
      +---- VTK visualisation (viz/)
      |
      +---- Scenario, reports, comparison, inspection (config/)
      |
      v
   C++17 core (core/)
      |
      +---- Weather            +---- Grazing and the farmer
      +---- Soil water         +---- Livestock energy
      +---- Pasture growth     +---- Irrigation
      +---- Terrain            +---- Budget ledger
```

The simulation core is independent of the GUI and of the visualisation. Qt and
VTK consume simulation state; they do not own biological or management logic.
`core/` has no external dependencies at all — a machine with only a compiler
builds and runs the scientific test suite — and a CI script fails the build if
anything points out of it.

## Design Principles

- Deterministic simulation: same seed, bit-identical output, asserted by a test
- A daily timestep, single-threaded, reproducible
- State, biology, management and presentation kept apart
- Simulation logic independent of Qt and VTK
- Scenario reproducibility: a bundle carries its inputs' hashes and is refused if they change
- Explicit units, at every boundary and on every panel
- Conservation-oriented bookkeeping: dry matter, water and nitrogen all balance
- Rule-based decisions recorded by the model, not reconstructed by the interface

## Engineering Quality

- Modern C++17, CMake with `CMakePresets.json` as the single source of build truth
- Automated tests in five classes — unit, conservation, validation, statistical
  and property (475 of them at the time of writing)
- GitHub Actions CI on Linux, macOS and Windows, with the GUI built and exercised headlessly
- clang-tidy and clang-format gates, and an ASan/UBSan build
- Deterministic replay, and dry matter, water and nitrogen balance checks on every commit
- Scenario comparison and CSV, Markdown and PDF reporting

## Model Validation

### Engineering validation

Checked on every commit, and none of it depends on reference data:

- deterministic replay — the same seed twice, bit for bit
- dry-matter conservation to 1e-9 over a grazed year
- water conservation to 1e-9
- nitrogen bookkeeping to 1e-9
- scenario bundles refused when an input's hash does not match

### Biological validation

Biological components are validated progressively against published New Zealand
and international reference data — DairyNZ, CSIRO, Nicol and Brookes, and
Gillingham's slope and aspect trial — with tolerance-band tests in CI and
comparison plots uploaded as artifacts.

This should be treated as a simulation and research platform, not as a certified
farm advisory model. [docs/verify.md](docs/verify.md) records, output by output,
what may be quoted and what may not.

## Known Limitations

Taken from [docs/verify.md](docs/verify.md), which is kept current with the code:

- Sheep maintenance requirement is low by about 15% against CSIRO (2007), so
  **carrying capacity for sheep is overstated by up to 17%**
- **Absolute liveweight gain is not quotable**: standard reference weight is
  unverified and it drives the energy value of gain
- Lactation is not modelled — a "dairy cow" here is a dry cow
- Dung and urine are not returned to the soil, so nitrogen over more than a
  season is wrong in a known direction
- Nitrogen leaching is not modelled
- Pasture growth parameters are not yet fully calibrated against measured New
  Zealand yields; the seasonal shape correlates well with DairyNZ's measured
  site, the magnitude does not
- Some example soil and sward inputs remain placeholders and are marked as such

Work that compares two scenarios is unaffected by the parameter gaps: both arms
carry the same parameters, so an error in one cancels.

## Irrigation Model Scope

Paddock models irrigation at the farm-system level. It represents when
irrigation is triggered, how much water is applied, how much reaches the root
zone, how soil moisture responds, how water stress changes, and how the pasture
responds over the season.

Hydraulic network design — pipes, pumps, pivot geometry, pressure — is not
modelled.

## Reports and Export

A run can be exported as:

- **PDF** — the run report, laid out for a page
- **Markdown** — the same report, and the comparison table
- **CSV** — the daily series, and the comparison

Reports carry the pasture, water, stock and budget results together with what
differed between scenarios and a closing section on what the run may be relied
on for.

## Example Output

```
canterbury_grazed (engine 0.1.0, seed 20240702)
  2023-07-01 to 2024-06-30, 366 days
  weather   synthetic:canterbury_plains_example (1b4174317ab0)
  rainfall  925.5 mm
  et        590.4 mm
  drainage  262.0 mm
  growth    10282.1 kg DM/ha
  fixed N   38.2 kg N/ha
  closing   2248.0 kg DM/ha cover, 116.8 mm soil water, 14% legume
```

A run whose budgets do not close is reported as a failure rather than printed:
the numbers would be meaningless.

## Build and Run

Requirements:

- A C++17 compiler
- CMake and Ninja
- Qt 6 and a VTK **built against Qt 6** — for the desktop application only
- GDAL and PROJ — only for reading LiDAR and cadastral data

The core and its tests need none of the above beyond a compiler and CMake:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Run a year from the command line:

```bash
./build/default/bin/paddock scenario run data/scenarios/canterbury-baseline
```

The desktop application, with the geospatial stack so that measured ground is
drawn as measured:

```bash
cmake --preset desktop -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x;/path/to/vtk"
cmake --build --preset desktop
./build/desktop/bin/paddock-gui data/scenarios/lincoln-lurdf
```

The `gui` preset builds the same application without GDAL, which draws every
farm flat whatever its manifest says. [docs/setup.md](docs/setup.md) has the
clean-machine instructions, including the Windows vcpkg flags and the elevation
snapshot script.

## Repository Structure

```
core/     simulation kernel: pure C++17, no external dependencies
config/   scenario bundles, runs, reports, comparison, paddock inspection
gis/      GDAL · PROJ · GEOS — reads DEM and cadastre, feeds core's own types
viz/      VTK — the 2D map and the 3D terrain scene
app/      Qt 6 desktop application and the command line tool
tests/    unit, conservation, validation, statistical and property tests
data/     species, pastures, calibration data and scenario bundles
docs/     ADRs, devlog, setup, verification, backlog
ai/       an optional module, off by default and a stub today
```

Dependency direction is one way: `gis → core`, `viz → core`, `config → core`,
`app → core`, `tests → core`. Nothing points out of core.

## Documentation

- [docs/setup.md](docs/setup.md) — build and dependency setup, all three editors
- [docs/verify.md](docs/verify.md) — model verification, evidence, and what may be quoted
- [docs/adr/](docs/adr/) — one file per architectural decision
- [docs/devlog/](docs/devlog/) — development notes, one per milestone
- [docs/backlog.md](docs/backlog.md) — what is queued and why

## Roadmap

Near-term priorities:

- close the sheep maintenance gap, which is what gates carrying-capacity work
- calibrate pasture growth against measured New Zealand yields
- return dung and urine nitrogen, so a multi-season run is sound
- extend paddock-level explainability to grazing decisions the model records
- widen real New Zealand farm-data integration beyond the Lincoln example

## Screenshots

- [Main simulation view](docs/images/paddock-main.png) — the farm on its own
  ground, one paddock selected, its day open beside the year
- [Paddock inspector](docs/images/paddock-inspector.png) — pasture, current
  water, the irrigation decision and grazing state

## Project Status

Paddock is an actively developed personal project. The current version runs a
full farm year with weather, soil water, pasture, livestock, grazing management
and irrigation; draws it in 2D and 3D over real LiDAR ground; compares
management scenarios; inspects individual paddocks through time; and exports
reports.

Biological calibration and validation are ongoing, and
[docs/verify.md](docs/verify.md) is the honest account of where they stand.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
