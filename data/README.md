# data/

Configuration is data first and UI second. Every simulator object is described
by validated TOML here; GUI panels, when they arrive, read and write these same
files and nothing else.

| Directory | Holds |
|---|---|
| `species/` | Animal class definitions: the energy parameters an animal is driven by |
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

## Adding an animal class

`data/species/` is read by scanning it, on the same terms as `farms/`: a species
is a file, `name` must be unique, and no code changes. A definition supplies the
energy parameters directly, so a ewe and a dairy cow differ only in numbers.

**Every number carries where it came from.** Each energy parameter is an inline
table with a value, a status and a citation:

```toml
species_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
standard_reference_weight_kg = { value = 65.0, status = "verify" }
```

The four statuses run from strongest to weakest:

| Status | Means |
|---|---|
| `direct` | Explicitly stated in the cited source |
| `derived` | Arithmetic on values the source states, and nothing more |
| `verify` | The source looks as though it has it; the reading needs confirming |
| `placeholder` | An engineering value with no evidence behind it |

There is no default. That is the point: defaulting to `direct` would launder a
guess into a published figure, and defaulting to `placeholder` would let a real
citation go unrecorded. A `direct` or `derived` value must name a source in
[calibration/livestock/sources.toml](calibration/livestock/sources.toml), and a
test fails if it names one that is not there — a dangling citation reads as
evidence and is not.

Standard reference weight is `verify` on every definition shipped today, and it
drives the energy value of gain, so no absolute liveweight gain from this model
is quotable yet. Comparisons that hold it fixed are.

`cattle-dry-cow.toml` is named for the dry cow it actually models — lactation is
not implemented, so there is no milking class to define.

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
