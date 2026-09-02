# System architecture

How Paddock is put together, why it is put together that way, and what each part
is not allowed to do.

**This document is about the software.** The agricultural equations, their
sources and their calibration status live in [the model
documents](../model/) and in [the verification
tracker](../validation/verify.md). Where a diagram here shows "pasture growth",
what that means numerically is over there.

Every diagram below was drawn from the code as it stands. A flow that is
pleasant to read and does not match the execution order would be worse than no
diagram, because it would be believed.

## Contents

- [Purpose and scope](#purpose-and-scope)
- [Architecture goals](#architecture-goals)
- [Level 1: the system](#level-1-the-system)
- [Level 2: inside the core](#level-2-inside-the-core)
- [What each module owns](#what-each-module-owns)
- [Flow 1: running a scenario](#flow-1-running-a-scenario)
- [Flow 2: one simulated day](#flow-2-one-simulated-day)
- [Flow 3: the irrigation decision](#flow-3-the-irrigation-decision)
- [Flow 4: the grazing decision](#flow-4-the-grazing-decision)
- [The domain, as types](#the-domain-as-types)
- [Dependency direction](#dependency-direction)
- [Sequence: inspecting a paddock](#sequence-inspecting-a-paddock)
- [Sequence: comparing scenarios](#sequence-comparing-scenarios)
- [Consequences of these choices](#consequences-of-these-choices)

## Purpose and scope

Paddock is a spatially explicit, deterministic pastoral farm simulation
platform. It spans these concerns:

weather · soil water · pasture · irrigation · grazing · livestock · management ·
scenarios · visualisation and reporting

It runs a New Zealand farm year, 1 July to 30 June, one day at a time, over a
grid of cells that belong to real surveyed paddocks — and it keeps every day, so
the year can be scrubbed, inspected and reported on without being run again.

## Architecture goals

These are the properties the structure exists to protect. Each one is enforced
by something, not merely intended.

| Goal | What enforces it |
|---|---|
| **Separation of concerns** — simulation logic is not presentation logic | `core/` compiles and its tests run with no Qt, no VTK, no GDAL |
| **Determinism** — the same inputs give the same output, bit for bit | Every random draw comes from an injected engine keyed by entity ID; a property test runs a year twice and compares |
| **Testability** — the science can be tested without a window | Several hundred tests, of which the scientific classes link `core` and `config` only |
| **Extensibility** — a new species or policy is data, not a class | Species, pastures and farms are TOML; entities are components |
| **Explainability** — a management decision can be accounted for | The model records its decisions; the interface reads them and never re-derives them |
| **Conservation** — nothing appears or vanishes unaccounted for | A budget ledger tracks dry matter, water and nitrogen; a closed run must balance to 1e-9 |

The last two are the ones that shape the code most. Conservation means every
process declares what it took and what it gave. Explainability means the
irrigation schedule writes down *why* it did nothing, because by the evening the
reason is unrecoverable.

## Level 1: the system

```mermaid
flowchart TB
    subgraph app["app/ — Qt 6 desktop application and CLI"]
        setup["Setup panel<br/>scenario, stock, management, irrigation"]
        inspector["Paddock inspector"]
        controls["Timeline, layers, chart"]
        reports["Reports and comparison dialogues"]
        boards["Indicators · every year<br/>tiles, bars, shares, seasons"]
    end

    subgraph viz["viz/ — VTK visualisation"]
        map2d["MapScene<br/>2D field, fences, mobs"]
        map3d["TerrainScene<br/>3D relief, layer stack, weather"]
    end

    subgraph config["config/ — scenarios, runs, presentation"]
        bundle["Scenario bundles<br/>TOML + hashes"]
        runner["Run + observer + the books"]
        present["Report · comparison · paddock inspection"]
        dash["Dashboard · nitrogen report<br/>every indicator with its provenance"]
        econ["Economics · prices and costs"]
    end

    subgraph core["core/ — simulation kernel, C++17 and the standard library only"]
        weather["Weather"]
        soil["Soil water"]
        pasture["Pasture"]
        irrigation["Irrigation"]
        grazing["Grazing and the farmer"]
        stock["Livestock energy<br/>maintenance · pregnancy · lactation"]
        flock["Flock<br/>age structure, lambing, culls"]
        feed["Feed store<br/>cut in spring, fed in summer"]
        money["Farm account"]
        ledger["Budget ledger"]
    end

    subgraph gis["gis/ — GDAL · PROJ · GEOS"]
        dem["LiDAR elevation"]
        cadastre["Paddock boundaries"]
        fetch["Elevation download<br/>one pinned tile, hash-checked"]
    end

    app --> viz
    app --> config
    viz --> core
    config --> core
    gis --> core
    app -.->|"value types + pure helpers"| core
    gis -.->|"feeds at load time"| config
```

The arrows are what may include what, and nothing points out of `core`.

**What the dashed line means, precisely.** `app` does depend on `core` directly,
and it is worth being exact about how, because "the GUI does not touch the core"
would be a slogan rather than a description:

- It uses core's **value types** — `Raster`, `Paddock`, `Date`, the daily weather
  record — to hold and draw what a run produced
- It calls a few **pure functions** that are presentation work rather than farm
  simulation: `sun_position`, `clearness_index`, `sky_from_clearness`, which
  place the sun and colour the sky for the day being shown
- It builds a `PaddockMask`, which decides which cell belongs to which paddock —
  a core algorithm, used here to average a paddock's figures for the inspector

What it does **not** do is run the model. The daily loop, the water balance,
growth, the grazing decision and the irrigation decision are all behind
`config::run_managed_scenario`, and the window learns their results by being
handed them. No biological or management rule is evaluated in `app/` or `viz/`.

## Level 2: inside the core

```mermaid
flowchart TB
    runner["config::run_managed_scenario<br/><i>owns the daily loop</i>"]

    farmer["core::Farmer<br/>chooses the system, moves mobs, buys feed"]
    schedule["core::IrrigationSchedule<br/>decides water per cell"]
    farm["core::Farm<br/>paddocks · mobs · rest counters"]
    grid["core::FarmletGrid<br/>one Farmlet per cell"]
    farmlet["core::Farmlet<br/>soil + sward on one cell"]
    soil["core::SoilWater"]
    sward["core::Pasture"]
    energy["core::AnimalEnergy · core::Grazing"]
    ledger["core::BudgetLedger"]

    runner --> farmer
    runner --> schedule
    runner --> farm
    farm --> grid
    grid --> farmlet
    farmlet --> soil
    farmlet --> sward
    farm --> energy
    soil -.->|"declares its fluxes"| ledger
    sward -.->|"declares its fluxes"| ledger
    farm -.->|"declares what was eaten"| ledger
```

`Farmlet` is the unit the science happens on: one cell of soil with one sward on
it. `FarmletGrid` is a farm's worth of them, and `Farm` adds the paddock
boundaries, the mobs and the bookkeeping that ties cells to paddocks.

## What each module owns

**`config::run_managed_scenario`** — owns simulated time. It runs the day loop,
calls the farmer, asks the schedule, steps the farm, records the series, and
offers each finished day to an observer. It computes no biology.

**`core::Farm`** — the aggregate: paddocks, mobs, the mask that says which cell
belongs to which paddock, and the count of days since each paddock was grazed.
It steps the grid and then feeds the mobs from the cells they stand on.

**`core::Farmlet`** — one cell. Soil water and sward, stepped in that order,
because growth is limited by the water that is left after the balance runs.

**`core::SoilWater`** — rain plus irrigation in, evapotranspiration and drainage
out, and the FAO-56 stress coefficient that comes out of what is left.

**`core::Pasture`** — the sward: ryegrass and white clover, growth under
temperature, radiation and that stress coefficient, and the nitrogen the clover
fixes.

**`core::IrrigationSchedule`** — decides, per cell, whether to water and how
much; keeps the tally, the fraction it read this morning, and the reason it held
water back.

**`core::Farmer`** — chooses the grazing system from the state of the farm,
moves mobs, and decides what feed to buy. It is the only thing that moves stock.

**`core::BudgetLedger`** — every process declares what it took and gave; the
ledger closes, or the run is reported as unusable.

**`config::PaddockInspection`** — gathers one paddock's day from what the run
recorded. It computes means over a paddock's cells and nothing else: no
thresholds are re-evaluated here.

**`app::MapWindow`** — owns the selection, the day being shown, and the wiring.
It holds every day's rasters so scrubbing does not re-simulate.

## Flow 1: running a scenario

```mermaid
flowchart TB
    choose["Person sets up a scenario<br/>or names a bundle"] --> load["Load bundle: TOML + weather + soil + sward"]
    load --> hash["Check each input's SHA-256<br/><i>refuse the run if one moved</i>"]
    hash --> ground["Attach elevation, or record why it is flat"]
    ground --> build["Build Farm: grid, paddock mask, mobs"]
    build --> loop["Run the daily loop"]
    loop --> observe["Observer keeps each day's rasters and records"]
    observe --> close["Check the three budgets close"]
    close --> result["RunSummary + kept days"]
    result --> draw["VTK scene"]
    result --> chart["Chart and timeline"]
    result --> inspect["Paddock inspector"]
    result --> report["Report: Markdown · CSV · PDF"]
```

A bundle that fails its hash check is refused rather than run: a scenario whose
inputs have moved is no longer the scenario its results were recorded for.

## Flow 2: one simulated day

This is the order in `config::run_managed_scenario` and `core::Farm::step`, as
written.

```mermaid
flowchart TB
    start(["Start of day"]) --> farmer["Farmer decides:<br/>grazing system, mob moves, feed purchases"]
    farmer --> read["Read soil depletion as it stands this morning"]
    read --> decide["Irrigation schedule decides, cell by cell"]
    decide --> water["Water to apply, per cell"]
    water --> soil["Soil water balance:<br/>rain + irrigation − evapotranspiration − drainage"]
    soil --> stress["Water stress coefficient"]
    stress --> growth["Pasture growth:<br/>temperature × radiation × stress"]
    growth --> rest["Rest counters advance"]
    rest --> demand["Mob energy demand → intake"]
    demand --> eat["Pasture removed from the cells the mob stands on"]
    eat --> record["Record the day:<br/>series, ledger, rasters, decisions"]
    record --> next(["Next day"])
```

Two things in that order matter and are easy to get wrong:

**The farmer moves stock before the day is stepped**, so a mob that moves eats
on the paddock it moved to rather than the one it left.

**Irrigation is decided on the morning's depletion**, before the balance runs.
By the evening the soil holds the rain, has lost the day's evapotranspiration
and has been given the water itself — a paddock watered at 45% can finish the
day at 84%. This is why the schedule keeps what it read, and why the inspector
shows it.

## Flow 3: the irrigation decision

One cell, one morning. The guards are in the order `decide_irrigation` applies
them, and each one records the phrase the inspector later prints.

```mermaid
flowchart TB
    start(["Cell, this morning"]) --> enabled{"Irrigation enabled?"}
    enabled -->|no| off["held back:<br/><i>irrigation is off</i>"]
    enabled -->|yes| holds{"Soil holds available water?"}
    holds -->|no| none["held back:<br/><i>no available water to refill</i>"]
    holds -->|yes| recent{"Return interval elapsed?"}
    recent -->|no| soon["held back:<br/><i>watered too recently</i>"]
    recent -->|yes| trigger{"Depletion ≥ trigger?"}
    trigger -->|no| wet["held back:<br/><i>still wetter than the trigger</i>"]
    trigger -->|yes| need["Water wanted = depletion − target"]
    need --> already{"Anything to put on?"}
    already -->|no| attarget["held back:<br/><i>already at the target</i>"]
    already -->|yes| cap["Cap by policy depth, then by system depth"]
    cap --> eff["Applied = effective ÷ efficiency<br/>pumped = applied × 10 m³/ha"]
    eff --> apply["Water goes to the soil balance"]
    apply --> keep["Decision and reason recorded"]
    off --> keep
    none --> keep
    soon --> keep
    wet --> keep
    attarget --> keep
    keep --> shown["Inspector · report"]
```

The chain from a domain rule, through the simulation, to something a person can
read is the whole point of the shape: the interface prints `held_back` and never
re-evaluates a threshold.

## Flow 4: the grazing decision

As `core::Farmer::manage` runs it. **There is no per-paddock "grazeable" test in
this model** — the rule is about the mob and about which ground has rested
longest.

```mermaid
flowchart TB
    start(["Each mob, each day"]) --> hungry{"Went short yesterday?"}
    hungry -->|no| long{"Days on paddock ≥ maximum?"}
    hungry -->|yes| pick
    long -->|no| stay["Stays where it is"]
    long -->|yes| pick["Look for somewhere to go"]
    pick --> free{"Any free paddock?"}
    free -->|no| extend["Nowhere to go:<br/>grazing lengthens, recorded"]
    free -->|yes| best["Choose the free paddock rested longest"]
    best --> spell{"Rested for the minimum spell?"}
    spell -->|no| short["Moves anyway,<br/>short spell recorded"]
    spell -->|yes| move["Move the mob"]
    short --> move
    move --> feed["Farm::step: demand → intake → pasture removed"]
    extend --> feed
    stay --> feed
    feed --> floor{"Cover at the floor?"}
    floor -->|yes| buy{"Farmer may buy feed?"}
    floor -->|no| done(["Day done"])
    buy -->|yes| bought["Buy the shortfall, recorded with its reason"]
    buy -->|no| gives["The stock go short, recorded"]
    bought --> done
    gives --> done
```

The spell is what the farmer aims at, not a gate. A mob moves onto ground that
has had less rest and the run records that it happened — which is what makes the
shuffle measurable instead of hidden.

## The domain, as types

The core types, not every class in the repository.

```mermaid
classDiagram
    class Farm {
        +step(weather, diet, supplement, ledger, water) FarmDay
        +paddocks() vector~Paddock~
        +mobs() vector~FarmMob~
        +days_since_grazed() vector~int~
    }
    class FarmletGrid {
        +step(weather, ledger, irrigation)
        +cover_kg_dm() Raster
        +available_water_fraction() Raster
        +water_stress() Raster
        +depletion_mm() Raster
    }
    class Farmlet {
        +step(weather, radiation_ratio, ledger, irrigation) DailyRecord
    }
    class SoilWater {
        +step(...) SoilWaterFluxes
        +water_mm() double
    }
    class Pasture {
        +step(weather, stress, ledger) PastureGrowth
        +cover_kg_dm() double
        +legume_fraction() double
    }
    class Paddock {
        +name string
        +boundary Polygon
    }
    class PaddockMask {
        +owner(col,row) size_t
        +rasterised_hectares(paddock) double
    }
    class FarmMob {
        +mob Mob
        +paddocks vector~size_t~
        +days_on_paddock int
    }
    class Farmer {
        +manage(farm, date, diet, went_short, supplement) Day
        -policy ManagementPolicy
    }
    class IrrigationSchedule {
        +decide(depletion, available) vector~double~
        +last_available_fraction() vector~double~
        +last_held_back() string
        +tally() IrrigationTally
    }
    class BudgetLedger {
        +record(budget, flux)
        +closes(budget, closing, tolerance) bool
    }
    class RunSummary
    class PaddockInspection

    Farm "1" *-- "1" FarmletGrid
    Farm "1" *-- "*" Paddock
    Farm "1" *-- "*" FarmMob
    Farm "1" *-- "1" PaddockMask
    FarmletGrid "1" *-- "*" Farmlet
    Farmlet "1" *-- "1" SoilWater
    Farmlet "1" *-- "1" Pasture
    Farmer ..> Farm : moves mobs on
    IrrigationSchedule ..> FarmletGrid : waters
    Farm ..> BudgetLedger : declares fluxes
    RunSummary <.. Farm : recorded from
    PaddockInspection <.. PaddockMask : averaged over
```

## Dependency direction

```mermaid
flowchart LR
    app["app/<br/>Qt 6"] --> viz["viz/<br/>VTK"]
    app --> config["config/"]
    viz --> core["core/<br/><b>no external dependencies</b>"]
    config --> core
    gis["gis/<br/>GDAL · PROJ"] --> core
    tests["tests/"] --> core
    tests --> config

    core -.->|"forbidden"| qt["Qt"]
    core -.->|"forbidden"| vtk["VTK"]
    core -.->|"forbidden"| gdal["GDAL"]

    linkStyle 7,8,9 stroke:#c0392b,stroke-width:2px,stroke-dasharray:5
```

`scripts/check-dependency-direction.sh` fails the build if `core/` gains an
external include or a global random draw. The point is not purity for its own
sake: it is that the simulation kernel can be built, tested and run on a machine
that has never heard of Qt, which is what makes the scientific suite cheap
enough to run on every commit.

## Sequence: inspecting a paddock

```mermaid
sequenceDiagram
    actor Person
    participant Scene as viz::TerrainScene
    participant Window as app::MapWindow
    participant Inspect as config::inspect_paddock
    participant Kept as Kept days<br/>(rasters + records)
    participant Panel as app::PaddockInspector

    Person->>Scene: click
    Scene->>Window: ground point (easting, northing)
    Window->>Window: which paddock boundary contains it
    Window->>Scene: ring that paddock
    Window->>Inspect: paddock index, day shown
    Inspect->>Kept: read the day's rasters and records
    Kept-->>Inspect: cover, growth, water, morning water, held back, rest
    Inspect-->>Window: PaddockInspection
    Window->>Panel: show it
    Note over Person,Panel: The day changes → the window asks again<br/>for the same paddock. The selection does not move.
```

VTK resolves a click to a point on the ground and nothing else. It evaluates no
rules, and it holds no simulation state.

## Sequence: comparing scenarios

```mermaid
sequenceDiagram
    actor Person
    participant Window as app::MapWindow
    participant Run as config::run_managed_scenario
    participant Compare as config::compare
    participant Dialog as app::ComparisonDialog

    Person->>Window: Compare
    loop each stored scenario
        Window->>Run: same bundle, same weather, that scenario's settings
        Run-->>Window: RunSummary
    end
    Window->>Compare: the summaries
    Compare-->>Window: table: what differed, then the metrics
    Window->>Dialog: show it
    Dialog-->>Person: PDF · Markdown · CSV
    Note over Run: Every arm goes through the same engine.<br/>Scenarios are re-run rather than remembered,<br/>so no arm is an answer from an older model.
```

Both arms running through one engine is what makes the comparison worth more
than the absolute figures: the same parameters and the same structure are
applied to each. It does not make a comparison exact. The responses those
parameters feed are not linear, so a parameter that is wrong can bias the size
of a difference — more under irrigation than under drought, say — even though
the direction survives.

## Consequences of these choices

Worth stating plainly, because every structure costs something.

**Keeping every day in memory** is what makes the timeline instant and the
inspector honest, and it is why a run holds a year of rasters — about nine
series over 1 280 cells and 366 days for the Lincoln example. The alternative,
re-simulating on scrub, risks showing different numbers on the way back.

**Recording decisions rather than deriving them** means the model carries state
it does not need for the simulation itself — the morning water fraction, the
held-back phrase. That is the price of an interface that cannot disagree with
the model.

**A single-threaded daily step** is a deliberate limit: parallel floating-point
reductions would break the 1e-9 conservation assertions and the golden
baselines. Ensembles and parameter sweeps are meant to be run as separate
processes.

**No inheritance for species or policies.** A deer is farmed stock in one
scenario and a pest in another, and a class tree cannot express that. Entities
are components; species, pastures and farms are TOML.

## See also

- [Model verification and what may be quoted](../validation/verify.md)
- [The model documents](../model/)
- [Build and dependency setup](../setup.md)
- [Architecture decision records](../adr/)
