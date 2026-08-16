# data/

Configuration is data first and UI second. Every simulator object is described
by validated TOML here; GUI panels, when they arrive, read and write these same
files and nothing else.

| Directory | Holds |
|---|---|
| `species/` | Animal and pest definitions: intake, liveweight, reproduction, habitat |
| `pastures/` | Pasture species and mixes: ryegrass, white clover, their coupling |
| `diseases/` | Disease and pest process definitions: triggers, thresholds, effects |
| `calibration/` | Measured reference series as CSV, with source citations, used by the validation tests |
| `farms/` | Farm descriptions: boundaries, paddocks, soils, infrastructure |
| `scenarios/` | Scenario bundles: configs, master seed, data snapshot hashes, engine version |

## Two rules

**Every parameter cites its source.** A value taken from DairyNZ, AgResearch or
Beef+Lamb NZ carries the citation in a comment beside it. A value that has not
been checked yet is marked

```toml
# PLACEHOLDER — verify against <source>
```

and listed in [../docs/verify.md](../docs/verify.md) until it is.

**Bulk datasets are never committed.** LINZ, NIWA and Manaaki Whenua downloads
live in `data/snapshots/`, which is gitignored. What is committed is the fetch
script and the content hash, so a scenario bundle stays reproducible without the
repository carrying gigabytes of raster.

M1 adds no parameters: the kernel holds types, not numbers. The directories are
here so that M2 has nowhere else to put them.
