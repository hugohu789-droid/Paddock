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
| `farms/` | Farm descriptions: identity, location, and where the boundaries come from |
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

## Adding a farm

`data/farms/` is read by scanning it, so a farm is a file and nothing else. Drop
in a `.toml`, and `load_farms()` picks it up; no code, test or list needs
editing. The only rule the loader enforces across files is that `name` is
unique, because that is what a scenario refers to a farm by.

Each farm says where its paddock boundaries come from, and the three kinds are
three different claims about provenance:

| `kind` | Means | Committed? |
|---|---|---|
| `synthetic` | Rectangles generated over a declared extent | Yes - it is generated, and never mistaken for a survey |
| `inline` | Paddock polygons listed in the file itself | Yes - this is the form a boundary editor writes |
| `geopackage` | A LINZ-derived snapshot, by path, layer and SHA-256 | No - the snapshot is gitignored; the hash is what makes the reference reproducible |

All three farms shipping today are `synthetic`, and each says so in its own
comments. Replacing one with real boundaries is a change to its `[boundary]`
section and nothing else.

The boundary editor of task #22 does not exist yet. The format it will read and
write does, and `tests/config/FarmConfigTest.cpp` holds it to that, so the
editor when it arrives is a way of writing these files rather than a second
place a farm can be defined.
