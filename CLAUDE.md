# CLAUDE.md — Paddock

Paddock is a spatially explicit pastoral farm simulator for New Zealand
conditions: terrain, weather, soil water, pasture growth, livestock, pests,
diseases and management decisions coupled on real NZ geospatial data (LINZ,
NIWA, Manaaki Whenua). It is both a verifiable agricultural systems model and
a playable farm-management simulation.

Goals, in order:
1. Flagship portfolio project for NZ agritech employers (AgResearch, Manaaki
   Whenua, NIWA, DairyNZ, LIC, Lincoln Agritech, Rezare, Figured, Halter,
   Trimble Christchurch, Eagle Technology).
2. A potential low-cost product for small farms.

Everything in this repository — code, comments, commits, docs, issues, ADRs,
devlogs — is in English.

Repo description (set on GitHub):
```
A spatially explicit pastoral farm simulator — terrain, weather, pasture growth,
livestock and pests coupled on real NZ geospatial data. C++17 core, Qt/VTK 2D+3D.
```

---

## Non-negotiable principles

1. **Core has zero external dependencies.** `core/` is pure C++17 + stdlib.
   No GDAL, no Qt, no VTK, no network. No arrow in the dependency graph ever
   points out of core. A machine with only a compiler must be able to build
   and run the entire scientific test suite.
2. **Determinism.** Every random draw comes from an explicitly injected
   engine. Each subsystem holds its own `std::mt19937_64` seeded by derivation
   from the master seed. Random numbers are keyed by **entity ID, never by
   iteration order** (so future parallelisation cannot change results). No
   global `rand()`, no wall-clock time in the core. Same seed → bit-level
   identical output, asserted by a test.
3. **Conservation.** A closed synthetic farm run for 365 simulated days must
   balance dry matter, water, and nitrogen to within 1e-9. This suite needs no
   reference data and runs on every commit. It is never skipped, weakened, or
   marked flaky. Every new process must declare which budget lines it touches.
4. **Species, pastures, diseases and pests are data, not classes.** No
   inheritance trees (`Animal → Ruminant → Sheep` is forbidden — a deer is
   both farmed stock and a wild pest in NZ, and a tree cannot express that).
   Entities are composed from components (`Position`, `Grazer`, `Liveweight`,
   `Health`, `Reproduction`, `Owned`, `Labour`). Definitions live in TOML
   under `data/`. Extending to a new species or pest means adding a data
   file, not a class.
5. **Calibrate against published data; never invent numbers.** Model
   parameters (base temperatures, growth rates, intake, spore thresholds)
   must come from DairyNZ / AgResearch / Beef+Lamb NZ or similar sources,
   stored as CSVs in `data/calibration/` with source citations, and asserted
   by tolerance-band validation tests. Any placeholder value must be marked
   `# PLACEHOLDER — verify against <source>` in the data file and tracked in
   `docs/validation/verify.md`.
6. **Config as data first, UI second.** Every simulator object is
   configurable via validated TOML with line-precise error messages. GUI
   configuration panels come later and only read/write the same files. Never
   build a config UI for something without a validated file format.
7. **AI at the edges, never in the core.** LLM features live in an optional
   `ai/` module (build flag `PADDOCK_WITH_AI`, off by default in CI). They
   generate configs, explain results, and write reports. Every AI output is
   schema-validated before entering the system, and AI never mutates
   simulation state directly.

---

## Tech stack

- **Language:** C++17 throughout.
- **Build:** CMake with `CMakePresets.json` (`cmake --preset default` must
  succeed on a clean machine). Dependencies pinned via `vcpkg.json` manifest:
  GDAL, PROJ, GEOS, VTK, Qt6, gtest, google-benchmark, toml++, spdlog
  (+ libcurl and a JSON library for the optional `ai/` module).
- **GUI:** Qt6. **Visualisation:** VTK (2D map view + 3D terrain view;
  livestock as `vtkGlyph3D` instances).
- **Tests:** gtest; google-benchmark for performance baselines.
- **Formats:** raster GeoTIFF, vector GeoPackage (never shapefile),
  configs TOML, exports GeoTIFF/CSV (verifiable in QGIS and ParaView).

## Module layout and dependency rule

```
core/    pure C++17 simulation kernel — Raster<T> (georeferenced), Polygon,
         Entity + components, SimulationClock, RNG engines, budget ledger
gis/     GDAL · PROJ · GEOS — reads DEM/cadastre/waterways, feeds core
         internal raster/polygon types; core never sees GDAL types
viz/     VTK — reads core snapshots
app/     Qt6 — orchestrates core, hosts 2D/3D views, scenario editor
ai/      optional — Anthropic API features; depends on core snapshots only
tests/   gtest + benchmarks — asserts directly against core
data/    species/ pastures/ diseases/ calibration/ farms/ scenarios/
docs/    adr/ (one file per decision) · devlog/ (one per milestone) ·
         setup.md (clean-machine build, both editors) · verify.md
```

Dependency direction: `gis → core`, `viz → core`, `app → core`, `ai → core`,
`tests → core`. **Nothing points out of core.**

## Geospatial conventions

- All internal computation in **NZTM2000 (EPSG:2193)**, metres. PROJ handles
  WGS84 ↔ NZTM at the boundary. Coordinate transforms have unit tests:
  round-trip error at known control points < 1 mm.
- Raster resolution 5–10 m (a 200 ha farm ≈ 20,000 cells at 10 m — daily
  stepping is cheap).
- GDAL appears only inside `gis/`.

## External data sources

| Data | Source | Used for |
|---|---|---|
| DEM / LiDAR | LINZ Data Service | slope, aspect, flow, 3D terrain |
| Parcels / cadastre | LINZ | farm boundary → paddock polygons |
| Waterways | LINZ Topo50 | riparian buffers, drainage, N pathways |
| Soils | Manaaki Whenua S-map / LRIS | water-holding capacity, drainage class |
| Climate | NIWA CliFlo (free with registration) | daily rain, temp, radiation, wind — real-year replay |
| Land cover | LCDB | pasture vs forest vs scrub (possum habitat) |

Every source is wrapped in a common port:

```
DataSource:
  describe()         -> name, licence, coverage, cadence
  test_connection()  -> ok | actionable error
  fetch(query)       -> typed records + provenance
```

Each port ships three adapters: **synthetic** (generator), **file** (local
snapshot), **live** (real service). The synthetic weather generator and CliFlo
replay are switchable behind the same interface. `test_connection` is exposed
as a CLI command (`paddock source test <name>`) — and only later, if ever, as
a GUI panel. Every live fetch is cached to a local snapshot with a content
hash; bulk datasets are never committed (commit fetch scripts + hashes).

## Scenario reproducibility

A scenario is a single bundle: all TOML configs + master seed + content hashes
of every data snapshot used + engine version. `paddock scenario run <bundle>`
reproduces output bit-for-bit on the same engine version; export/import
round-trips bundles. Model parameter sets are versioned TOML with metadata
(name, version, literature references, budget lines touched) and use the same
export/import mechanism. Golden regression baselines are pinned bundles.

---

## Testing — four classes plus one property

```
Unit          pure functions: temperature response, water balance step,
              intake calculation, NZTM round-trip
Conservation  closed system, 365 days, DM/water/N budgets close to 1e-9
              ← strongest class: no reference data needed, catches most bugs
Validation    tolerance-band comparison against DairyNZ/AgResearch measured
              seasonal growth curves; auto-generates comparison plots as CI
              artifacts
Regression    golden scenario (fixed farm + fixed weather year + fixed seed),
              key metric series compared point-by-point against baseline;
              failure outputs a diff plot
Property      same seed twice → bit-identical results
```

## Automation — six gates

| Gate | Trigger | Contents |
|---|---|---|
| T0 | on save | `.editorconfig` + `.clang-format` format-on-save; `.clang-tidy` in-editor; `CMakePresets.json` shared by all editors/CLI |
| T1 | pre-commit | format check, line-ending check (`core.ignorecase=false` + `.gitattributes`), fast core unit subset (< 10 s) |
| T2 | every PR | 3-platform matrix build (incl. GUI); full core tests; 1-year integration run; conservation assertions; deterministic replay; clang-tidy; Linux Debug with ASan + UBSan |
| T3 | PR + nightly | validation vs measured data with tolerance bands; comparison plots uploaded as artifacts |
| T4 | nightly | golden regression; performance benchmarks (raster size × herd size); coverage upload |
| T5 | on tag | 3-platform builds → windeployqt/macdeployqt → package with sample farm dataset → GitHub Release; Doxygen to GitHub Pages |

CI matrix: core-only on all three platforms (fast, no external deps; ASan on
Linux); full-GUI on all three (vcpkg binary cache); validation/regression on
Linux only.

---

## Milestones

Do not start a milestone before the previous one's acceptance passes.
**M1–M3 (~4 weeks) is the minimum submittable version.**

**M1 — Skeleton and automation, before any business logic (~5 days).**
Repo bootstrap: `main`, `.gitattributes`, `core.ignorecase=false`,
clang-format/tidy, editorconfig, PR + squash flow; CMakePresets + vcpkg.json;
four module skeletons with core at zero deps; T0–T2 live (placeholder tests
are fine — the pipeline must be real); core types: georeferenced `Raster<T>`,
`Polygon`, `Entity` + components, `SimulationClock`, explicit RNG engines.

*Editor parity (part of M1, not optional):* `CMakePresets.json` is the single
source of build truth — Qt Creator, VS Code and the CLI all consume it, and no
editor keeps its own build configuration. Presets set the vcpkg toolchain file,
use Ninja, and enable `CMAKE_EXPORT_COMPILE_COMMANDS`. `CMakePresets.json` is
committed; `CMakeUserPresets.json` (machine-local paths such as vcpkg root) is
gitignored. Both editors use **clangd** as the language server against the
generated `compile_commands.json`, so in-editor diagnostics match what the T2
clang-tidy gate reports. Format-on-save is wired in both (VS Code via the
clangd/clang-format extension, Qt Creator via Beautifier + ClangFormat), both
pointing at the repository `.clang-format`. Commit a minimal `.vscode/`
(extension recommendations + clangd settings) and document the Qt Creator
import steps in `docs/setup.md`.

*Accept:* empty project, but 3-platform CI green, format/static analysis
automatic, and `cmake --preset default` succeeds first try on a clean machine.
The same preset configures, builds and runs the tests from all three entry
points — Qt Creator, VS Code, and the command line — with identical clang-tidy
output in each.

**M2 — Pasture + weather + soil water (~7 days).**
Weather driver (CliFlo real-year replay + synthetic generator, switchable
behind the DataSource port with `test_connection` CLI); soil water bucket,
ET, drainage; pasture growth (ryegrass + white clover with N-fixation
coupling); conservation tests for DM/water/N; validation vs DairyNZ seasonal
curves; 2D map view with colour scale and timeline playback; scenario bundle
format + deterministic replay test.
*Accept:* real year of weather in → seasonal growth curve matches measured
shape; three budgets close; T3 gate live with comparison plots.

**M3 — Real farm GIS + livestock (~8 days).**
`gis/`: GDAL reads DEM/parcels/waterways; PROJ transforms with round-trip
precision tests; slope/aspect → growth modifiers; paddock polygons → raster
masks, with polygon drawing/editing on the 2D map to define paddocks and the
simulated area; livestock agents (intake, liveweight, grazing distribution)
fully driven by species TOML; rotational grazing via farmer agent; 3D terrain
view (DEM relief + pasture colouring + livestock glyphs).
*Accept:* load a real NZ farm from LINZ data, run a full year, 2D/3D views,
stable grazing–regrowth loop. **Resume-ready from here.**

**M4 — Pests, diseases and environmental metrics (~7 days).**
Data-driven disease/pest framework; first six scenarios: wild deer boundary
crossing, facial eczema (warm-wet → spore count → liver damage), grass grub,
drought, nitrogen leaching (annual total vs regulatory threshold → compliance
report), supplementary feed budget; metrics time-series panel; T4 golden
baselines + benchmarks live. Each scenario ships as a reproducible bundle
plus a devlog entry.
*Accept:* warm-wet year triggers facial eczema with visible production drop;
drought year triggers destocking; N-leaching annual report exportable.

**M5 — Management layer and delivery (~6 days).**
Market prices, feed budgeting, worker task scheduling, annual accounts;
save/load; scenario editor; exports (GeoTIFF/CSV verifiable in QGIS/
ParaView); T5 release pipeline; README rewrite (GIFs + validation results
table + download links); Doxygen to GitHub Pages.
*Accept:* a stranger downloads the installer, loads the sample farm, and
watches a year of weather-driven farm life within 30 seconds.

**M6 (optional, post-delivery) — AI features (`ai/` module).**
(1) Natural language → scenario TOML, schema-validated before use.
(2) Weekly farm report writer from simulation outputs. (3) "Explain this
result" (e.g. why pasture cover dropped). (4) Column-mapping assistant for
user CSV import. API key from environment only; module excluded from default
CI build.

---

## Working agreements for Claude Code

- Small, focused changes. Conventional commits (`feat:`, `fix:`, `test:`,
  `docs:`, `chore:`), imperative mood, no emoji. English only.
- Every feature lands with tests. Conservation, determinism and validation
  gates are never bypassed to make a PR green.
- Record each significant design decision as a short ADR in `docs/adr/`.
- At the end of each milestone, write a devlog entry in `docs/devlog/`:
  what shipped, one concrete problem solved, one NZ-domain insight (a CliFlo
  data quirk, an NZTM pitfall, a calibration finding). Plain English, under
  300 words — it doubles as LinkedIn post material.
- Prefer adding a data file over adding a code path. If a change wants a new
  entity subclass, stop and re-express it as components + data.
- Every model parameter cites its source in the data file or is explicitly
  marked PLACEHOLDER and listed in `docs/validation/verify.md`.
- Never commit secrets, API keys, or bulk downloaded datasets.
- Every new source file must include the GPL-3.0 SPDX license header. Do not
  accept external code without verifying CLA compliance.
- When unsure between simple and clever, choose simple and note the
  alternative in an ADR.

## Out of scope for v1 (do not build)

- Livestock hardware / IoT ingestion; mobile apps
- Authentication, multi-tenancy, billing
- ML training pipelines or ML model management
- Elaborate per-object GUI configuration panels (validated TOML is the v1
  interface; GUI panels only where a milestone explicitly lists them)
- Web frontend (this is a Qt desktop application)
- Parallelism inside a simulation run (threads, OpenMP, parallel algorithms in
  core). Single-threaded stepping is ~1s per simulated year at target scale,
  and parallel floating-point reductions would break the 1e-9 conservation
  assertions and golden baselines. Ensemble and parameter-sweep workloads are
  handled by running independent processes, which needs no core changes. Keep
  the door open — entity-ID-keyed RNG, double-buffered state, per-partition
  ledgers merged in fixed order — but do not implement threading.

## Items to verify before hardcoding (tracked in docs/validation/verify.md)

- Pasture growth parameters (base/optimal temps, seasonal rates, annual DM
  yield) → DairyNZ, AgResearch
- Intake, stocking rates, stock-unit conversions → Beef+Lamb NZ, DairyNZ
- Facial eczema spore-count thresholds and warm-wet trigger conditions →
  veterinary / extension material
- Grass grub degree-day development model → AgResearch literature
- Nitrogen leaching regulatory thresholds → current Regional Council rules
- LINZ / NIWA / Manaaki Whenua dataset licence terms and access methods
