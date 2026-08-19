# Review of the pasture, irrigation and grazing roadmap

A proposed roadmap was supplied on 19 August 2026 setting a direction for
Paddock: rain-fed pasture, then irrigation, then grazing, then both together,
then scenario comparison. This file is the assessment it asks for in its own
§40 and §41 — inspect what exists before writing anything — and the plan that
follows from it.

**The direction is right and the sequencing needs one change.** The roadmap
proposes building grazing after irrigation. Grazing is already built and is
further along than the roadmap's own Grazing MVP, so that stage is behind us
rather than ahead. What is genuinely missing is narrower than the document
assumes, and one thing it does not mention at all is the largest risk to its
headline demonstration.

## What the roadmap asks for that already exists

| Roadmap | State | Where |
| --- | --- | --- |
| §4 Rain-fed daily loop: weather → soil water → stress → growth → cover | **Built** | `core/src/SoilWater.cpp`, `core/src/Pasture.cpp`, `core/src/FarmletGrid.cpp` |
| §5–6 Irrigation as a water input to the balance | **Built** | `SoilWaterFluxes::irrigation_mm`, recorded to the water ledger |
| §9 Trigger from soil-water depletion | **Half built** — `readily_available_water_mm()` exposes RAW = p × TAW (FAO-56 Eq. 83); nothing decides on it yet | `core/include/paddock/core/SoilWater.hpp` |
| §13–15 Grazing MVP: herd, demand, allocation, removal, residual, deficit, rotation | **Built and exceeded** | `core/src/Grazing.cpp`, `core/src/Farm.cpp`, `core/src/Farmer.cpp` |
| §19 Paddock-level spatial state | **Built** — per-cell soil water, pasture, slope and aspect, masked into paddocks | `core/src/FarmletGrid.cpp`, `core/src/PaddockMask.cpp` |
| §26 Metrics from the core, not the GUI | **Half built** — `RunSummary` carries the series and the closing stocks; there is no annual metrics type | `config/include/paddock/config/ScenarioRun.hpp` |
| §28 Side-by-side comparison | **Half built** — `render_comparison_report()` takes two runs already | `config/include/paddock/config/ScenarioReport.hpp` |
| §29 Map modes | **Partly built** — cover, soil water, water stress, legume fraction, stock location | `app/src/gui/MapWindow.cpp` |
| §30 Timeline animation with play, pause and scrub | **Built** | `app/src/gui/MapWindow.cpp` |
| §35.3 Explainability: growth decomposed into factors | **Built in the model, not surfaced** — `PastureGrowth` carries the temperature, water and nitrogen factors and the intercepted PAR | `core/include/paddock/core/Pasture.hpp` |
| §35.5 Core independent of Qt and VTK | **Built and enforced** by a CI gate, not only by convention | `scripts/check-dependency-direction.sh` |
| §35.6 Reproducible scenarios | **Built and exceeded** — every input is SHA-256 pinned in the bundle and checked on load | `config/src/ScenarioConfig.cpp` |
| §36 Unit tests for every subsystem | **Built** — 457 tests, run on five platforms | `tests/` |
| §37 Layer 2 conservation checks | **Built and enforced** — dry matter, nitrogen and water budgets close or the build fails | `tests/conservation/` |
| §38 Daily CSV logging | **Half built** — `paddock scenario run --csv` | `app/src/main.cpp` |

## What is genuinely missing

This is the real work, and it is smaller than the document implies.

1. **The irrigation decision layer.** §7's separation — need, policy, capability
   — does not exist. Water can be applied; nothing decides to apply it. This is
   the next milestone and it is correctly identified as such.
2. **Efficiency and volume.** §10's mm → m³ → ML conversion, application
   efficiency, and the water productivity metric (kg DM per m³) that §11 calls
   the useful one.
3. **A scenario runner.** §27's `FarmScenario` and the machinery to run several
   configurations of one farm and difference them. The report side of this
   already exists for two runs; the running and differencing does not.
4. **An annual metrics type.** §26 is right that these belong in the core. Today
   the closing stocks are in `RunSummary` and the annual aggregates are computed
   where they are printed.
5. **Water source constraints** (§25) — the roadmap puts these later and that is
   correct.
6. **Paddock inspector** (§31) and the remaining map modes (§29): daily growth,
   irrigation status, days since grazing.

## Three places where following the roadmap literally would be a regression

The document says in §40.5 not to redesign working components without a clear
reason, and in §39 to reuse what exists. These three are where its own
illustrative code conflicts with that instruction.

**§6's soil water pseudocode is simpler than what is built, and the difference
is the part that matters.** The sketch subtracts crop ET and caps at field
capacity. The implementation applies FAO-56's stress coefficient Ks first, so
evapotranspiration falls as the profile dries and a drought actually bites; it
also moves the depletion fraction with evaporative demand (FAO-56 Table 22),
so a hot day dries the readily available water sooner than a mild one.
Replacing it with the sketch would delete the mechanism that makes irrigation
worth simulating in the first place. **Keep the existing balance.**

**§14's `Herd` stores a daily dry matter requirement as a field.** The built
model derives demand from the animal's energy requirement each day — basal,
chewing, movement and activity, from the OVERSEER technical manual — which is
why demand responds to liveweight, pasture mass, slope and the gain the farmer
is feeding for. A stored constant would discard that. It would also reintroduce
a bug fixed on 19 August: demand computed from the wrong quantity left a mob on
its opening weight for a year. **Keep the energy model; a herd is the existing
`Mob`.**

**§39's directory tree would be churn.** The repository is already layered
`core` → `config` → `gis`/`viz` → `app`, with the dependency direction enforced
by a build gate. The proposed tree splits `core` into eleven directories and
gains nothing the current layering does not already give. **Adopt the concepts,
not the file layout.**

## The risk the roadmap does not mention

§12 and §17 present result tables — 9.8 t DM/ha rain-fed against 15.4
irrigated, water productivity 1.58 kg DM per m³ — and §32 makes them the demo
story. **This project cannot produce those numbers honestly yet.**

The sward and soil definitions in every shipped bundle are marked PLACEHOLDER
in `docs/verify.md`. They are internally consistent and they are not calibrated
against New Zealand pasture measurements. A rain-fed versus irrigated
comparison built on them is valid as a *difference* — same model, same weather,
one input changed — and its absolute yields are not quotable. So:

- The comparison can be built now, and it will be informative about direction
  and about diminishing returns, which is what §17 says is the point.
- Every number it produces must be labelled until the sward is calibrated.
- **Calibrating the sward against published New Zealand growth data is a
  prerequisite for the demo in §32, not an optional Layer 4 as §37 implies.**

This is the single largest gap between what the roadmap wants to show and what
the code can defend, and it is a data problem rather than a code problem.

## Plan

Each stage is independently buildable and testable, per §40.9.

### Stage 0 — land the current work

PR #27 carries the weather integration §4 and §33 ask for: real daily weather
from Open-Meteo, the terrain the growth model runs on, and the day's conditions
on screen. Merge before opening new scope, as §33 says.

### Stage 1 — irrigation decision layer

The smallest extension that satisfies §42.

- `IrrigationPolicy`: trigger depletion fraction, target refill fraction,
  maximum single application, minimum return interval.
- `IrrigationSystem`: application efficiency, maximum application rate.
- `IrrigationManager`: reads soil state, asks the policy, clips to the system,
  returns a decision. It decides; it does not apply. Per §22, the soil model
  only reports that it is dry.
- Volume: mm × ha × 10 = m³, with the conversion tested as §36 asks.
- Annual metrics: depth, volume, event count, mean event depth, water stress
  days.

Done when the same farm runs with irrigation off and on, and the water budget
closes in both.

### Stage 2 — rain-fed versus irrigated

- `FarmScenario` as configuration over one bundle, per §27.
- A runner that executes several scenarios and differences them.
- Water productivity as the difference in dry matter over the water applied.
- The existing `render_comparison_report` renders it.

Done when one command produces §12's table for a real farm, with the
placeholder caveat carried in the output rather than in someone's memory.

### Stage 3 — calibrate the sward

Not in the roadmap, and the demo depends on it. Compare modelled annual and
seasonal growth against published New Zealand pasture data, and either fit the
parameters or record the gap. Until this is done every yield is a shape rather
than a quantity.

### Stage 4 — surface it

- Comparison table in the GUI.
- Paddock inspector (§31) — the per-cell state it wants already exists.
- Map modes: daily growth, irrigation status, days since grazing.
- Growth explainability (§35.3) — the factors are already computed and thrown
  away.

### Stage 5 — constraints and scenario space

Water source allocation (§25), stocking rate comparison (§18), dry year against
wet year. All of it is configuration over the same engine, which is what §27
asks for and what the current bundle format already supports.

## What stays out

§34's non-goals are agreed and need no argument. Two additions specific to this
codebase:

- **Do not wire wind into the model to make it look used.** It is carried and
  read by nothing (`docs/verify.md` E12). It belongs in evaporative demand or
  cold stress, and the shipped series supplies a daily maximum at 10 m, which
  is the wrong statistic at the wrong height for either.
- **Do not put a ceiling on liveweight without a source** (E13). The model will
  currently feed a ewe past mature weight because nothing forbids it.
