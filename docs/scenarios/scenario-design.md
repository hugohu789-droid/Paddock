# Scenario design

What a scenario is, what makes one reproducible, and what makes a comparison
between two of them mean anything.

## A scenario is a directory

Not a config file with paths pointing into a shared pool. One directory holds
the manifest and everything the manifest names:

```
data/scenarios/lincoln-lurdf/
├── scenario.toml                     the manifest
├── soil.toml                         the soil this farm runs on
├── sward.toml                        the species and their parameters
├── weather.toml                      which weather source, and how to read it
├── weather-2023.csv                  the recorded year itself
└── weather-2023.csv.provenance.json  where that file came from
```

## The manifest

`scenario.toml` carries, in sections:

| Section | What it fixes |
|---|---|
| `[scenario]` | Name, description, engine version, master seed |
| `[run]` | The date range — a New Zealand farm year, 1 July to 30 June |
| `[weather]` | The source, and the SHA-256 of the file it reads |
| `[soil]`, `[sward]` | The soil and species definitions, and their hashes |
| `[initial_state]` | What the farm starts the year holding |
| `[grid]` | Origin in NZTM2000, cell size, extent |
| `[terrain]` | Flat, a formula, or a LiDAR snapshot with its hash |
| `[[mob]]` | Stock: class, head, opening liveweight |
| `[[grazing_period]]` | A calendar, for runs that follow one |
| `[management]` | The farmer's rules: cover floor, rotation threshold, graze and spell days, whether feed may be bought |

## Reproducibility

**Every input is hashed and every hash is checked before the run starts.** If a
weather file, a soil definition or an elevation snapshot has changed since the
bundle recorded it, the run is refused rather than quietly producing different
numbers under the same name.

Together with an explicit master seed and injected random engines, that is what
makes a bundle a claim rather than a starting point: the same bundle on the same
engine version produces the same output, bit for bit, and a property test
asserts it.

**Snapshots are not committed.** A LiDAR tile is 35 MB and belongs to LINZ, not
to this repository. The bundle records the hash and the script that fetches it,
so the file can be recovered and verified but never silently substituted.

## What makes a comparison honest

The comparison the project exists to make is one variable at a time:

```
Same farm
Same weather
Same stock
Same stocking rate
Only the management differs
```

Two things in the implementation protect that:

**Every arm is re-run, not remembered.** A stored result would go stale the
moment the bundle or the weather changed underneath it, and a table of five
answers from five versions of the model is worse than no table. A year over one
of these farms is under half a second, so there is nothing to save by keeping
them.

**Every arm goes through the same engine.** Scenario A and scenario B are the
same code path with different settings, so a parameter that is wrong is wrong
identically on both sides and cancels. This is why the verification tracker can
say comparisons are sound while absolute yields are not.

The comparison table names **what differed** before it reports any metric, so a
reader can see which single setting was turned on rather than taking it on
trust.

## Designing a scenario worth comparing

- **Change one thing.** Two changes and the table cannot attribute the
  difference to either
- **Give it the whole year.** Irrigation that pays for itself in February can
  cost cover in October
- **Name it for the question.** "Irrigated" and "Rain-fed" carry what the
  scenario was for; "Scenario 2" carries nothing, and a table of five of those
  cannot be read
- **Keep the stocking rate fixed** unless stocking rate *is* the variable —
  otherwise a feed difference is really a demand difference

## Where scenarios come from

Two ways, and they produce the same thing:

- **A bundle on disk**, run from the command line or opened in the application
- **The setup panel**, which edits the stock and the management of a loaded
  bundle and keeps the result in the scenario list. Everything the bundle hashes
  — weather, soil, sward, ground — is left exactly as loaded, so a run started
  this way is still the bundle's run with a different mob or a different rule on
  it

## See also

- [System architecture](../architecture/system-architecture.md) — how a scenario
  becomes a run
- [Data and provenance](../data.md) — where the inputs come from
- [Verification tracker](../validation/verify.md) — what the outputs may be
  quoted for
