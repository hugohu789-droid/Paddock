# Data and provenance

Where Paddock's inputs come from, how they are fetched, and what stops one
quietly turning into another.

The layout of `data/` and the two rules that govern it — every parameter cites
its source, no bulk dataset is committed — are in
[data/README.md](../data/README.md). This document is about where the outside
data comes from and how it gets in.

## Sources

| Data | Source | Used for |
|---|---|---|
| Elevation (LiDAR DEM) | LINZ Data Service / Toitū Te Whenua | Slope, aspect, the 3D surface, the cost of walking ground |
| Parcels and cadastre | LINZ | Farm boundary and paddock polygons |
| Climate | NIWA CliFlo, and Open-Meteo for a quick start | Daily rain, temperature, radiation, wind — real-year replay |
| Soils | Manaaki Whenua S-map / LRIS | Water-holding capacity, drainage class |
| Livestock and pasture parameters | DairyNZ, AgResearch, Beef+Lamb NZ, CSIRO | Energy requirements, growth parameters, stock-unit conversions |

Every one of these carries a licence, and the licence travels with the file: see
the provenance JSON written beside each snapshot.

## One port, three adapters

Every source is reached through the same interface:

```
DataSource
  describe()         → name, licence, coverage, cadence
  test_connection()  → ok, or an actionable error
  fetch(query)       → typed records + provenance
```

with three implementations behind it: **synthetic** (a generator), **file** (a
local snapshot), and **live** (the real service). A run cannot tell which it is
talking to, which is what lets the same scenario be developed against synthetic
weather and then replayed against a recorded year.

Connection testing is a command, not a dialogue:

```bash
paddock source test data/scenarios/lincoln-lurdf/weather-2023.csv
paddock source test data/snapshots/lincoln-dem-1m.tiff
```

It answers the only question worth asking before a run: can this source deliver
data, and if not, what should I do about it.

## What may be redistributed, and what may not

Citing a source and shipping it are different permissions. This project tracks
both, and the second is the one that decides what is allowed into the
repository. The reasoning is open item 7 in the [verification
tracker](validation/verify.md); the conclusions are:

| Source | Licence | May it be committed or shipped? |
|---|---|---|
| LINZ (elevation, parcels) | CC BY 4.0 | **Yes**, with attribution — including derived work such as a rendered farm surface. Not committed anyway, because it is bulk |
| Open-Meteo (ERA5 reanalysis) | CC BY 4.0 | **Yes**, with attribution to Open-Meteo and to the Copernicus Climate Change Service |
| NIWA CliFlo / DataHub | NIWA terms | **No** — the licence forbids passing the data to a third party. Fetch it yourself; the bundle records only its hash |
| Manaaki Whenua S-map Online | CC BY-NC-ND 3.0 NZ | **No** — non-commercial and no distribution of derivatives |
| Published reference tables (DairyNZ, Beef+Lamb NZ, CSIRO) | Per publication | Values are cited with their source and URL; the documents themselves are not committed |

Two of those are hard limits rather than preferences: **no NIWA series and no
S-map soil data may be committed or shipped, whatever their hashes say.** A
scenario that depends on either is reproducible by hash and fetch script, not by
redistribution.

The audit that backs this up, and can be repeated:

```bash
git log --all --diff-filter=A -- 'data/snapshots/*'      # must print nothing
git ls-files data/                                       # only TOML, CSV, JSON, Markdown
```

Attribution for the two files that *are* committed under a CC BY licence:

> Weather data by Open-Meteo.com, licensed under CC BY 4.0. Generated using
> Copernicus Climate Change Service information (ERA5).

> Elevation behind the screenshots: Canterbury – Selwyn LiDAR 1 m DEM (2023),
> Toitū Te Whenua LINZ, CC BY 4.0; licensor Environment Canterbury, produced by
> Landpro.

## Snapshots

Bulk downloads live in `data/snapshots/`, which is gitignored. What is committed
is the fetch script and the content hash. A snapshot is therefore recoverable
and verifiable, but never silently substituted — a scenario bundle records the
SHA-256 of every input and refuses to run when one has moved.

The scripts, each writing a provenance file beside its output:

| Script | Fetches |
|---|---|
| `scripts/nz-elevation-snapshot.py` | LINZ elevation tiles for a point and collection |
| `scripts/linz-snapshot.py` | Parcels and other LINZ vector layers |
| `scripts/cliflo-snapshot.py` | NIWA CliFlo station series (registration required) |
| `scripts/open-meteo-snapshot.py` | A recorded year with no account needed |

For example, the elevation behind the Lincoln bundle:

```bash
python scripts/nz-elevation-snapshot.py --lon 172.470 --lat -43.641 \
  --collection canterbury/selwyn_2023 \
  --out data/snapshots/lincoln-dem-1m.tiff
```

That tile is Canterbury – Selwyn LiDAR 1 m DEM (2023), flown 23 March to 3 May
2023, already in NZTM2000, CC BY 4.0, licensor Environment Canterbury, produced
by Landpro, hosted by Toitū Te Whenua LINZ. It is a **DEM and not a DSM** —
bare earth, without the shelter belts and the woolshed roof — because grass
grows on the ground and not on the buildings.

## Coordinates

All internal computation is in **NZTM2000 (EPSG:2193)**, in metres. PROJ handles
WGS84 ↔ NZTM at the boundary, and a round-trip test holds known control points
to under a millimetre. GDAL appears only inside `gis/`; core sees rasters and
polygons in its own types.

## Calibration data

Measured reference series live in `data/calibration/` as CSV with their
citations, and the validation tests compare the model against them inside
tolerance bands, uploading comparison plots as CI artifacts. What each of those
comparisons currently shows — including where the model is off and by how much —
is in the [verification tracker](validation/verify.md).

## What is not settled

- S-map soils are **not yet wired in**: the shipped bundles carry soil
  definitions marked PLACEHOLDER where a real lookup should be
- Which parcels make up a farm is **left open on purpose**: the open cadastre
  carries no owner, and choosing parcels on area alone would put an invented
  boundary behind a surveyed-looking outline
- Licence terms for redistribution of each dataset are tracked as open items,
  which is why nothing bulk is committed
