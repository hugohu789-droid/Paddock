# ADR 0012 — LINZ data: read from snapshots in gis/, fetched by a script that holds no secret

- **Status:** accepted
- **Date:** 2026-08-17
- **Milestone:** M3

## Context

M3 needs LINZ elevation, cadastre and waterway data behind the same port every
other source uses: `describe()`, `test_connection()`, `fetch(query)`, with three
adapters — synthetic, file and live (`CLAUDE.md`).

[ADR 0008](0008-weather-sources.md) settled the same question for weather and
its two constraints apply unchanged:

1. **Core takes no dependencies**, and core is where the simulation runs.
2. **A run must be reproducible.** If a simulation can reach the network
   mid-run, the same bundle produces different results on different days and a
   golden baseline means nothing.

What differs is how access works, and it differs enough to change one
conclusion. The two services were checked rather than assumed:

| | NIWA CliFlo | LINZ Data Service |
|---|---|---|
| Credential | Username and password | API key |
| Programmatic access | None offered — CliFlo is a web form | Documented: WFS, WMTS, WMS |
| Cost | Free, 2-year subscription, 2 000 000 rows | Free with a registered account |
| Without an account | Reefton EWS only | — |

LINZ states that an API key ["allows you to access your LDS account without you
having to provide your password"](https://www.linz.govt.nz/guidance/data-service/linz-data-service-guide/web-services/creating-api-key),
and that "all web services providing data require an API key". Its guidance
describes no way to restrict a key — no IP allow-list, no referrer check — so a
leaked key is a leaked account with no mitigation short of revoking it.

## Decision

**`gis/` reads local snapshots. A separate script fetches them. Neither the
simulator nor CI ever holds a LINZ key.**

- **File adapters live in `gis/`**, not core, because reading a GeoTIFF or a
  GeoPackage needs GDAL: `GeoTiffElevationSource` and `GeoPackageParcelSource`
  implement the core ports and hand back `Raster<double>` and `Polygon`.
- **Synthetic adapters stay in core** (`SyntheticElevationSource`,
  `SyntheticParcelSource`), so the scientific suite still runs on a machine with
  only a compiler.
- **The live adapter is `scripts/linz-snapshot.py`, not a class.** It reads the
  key from the `LINZ_API_KEY` environment variable, writes a snapshot under the
  gitignored `data/snapshots/`, and records the SHA-256 a scenario bundle pins.
- **GeoTIFF for rasters, GeoPackage for vectors**, as `CLAUDE.md` specifies, and
  never shapefile. `gdal_driver_available()` asserts both are compiled in, so a
  GDAL missing one fails a test rather than the first file it opens.

### Where this departs from ADR 0008, and why

ADR 0008 kept downloading manual: "a script that drives someone's login is a
script that eventually does it without them noticing." That reasoning holds for
CliFlo, which has no interface but a login — and the check above confirms it:
there is no CliFlo API to use instead.

LINZ is different in kind. An API key *is* the mechanism LINZ provides for
programmatic access; using it is the intended path, not a way around a login. So
`linz-snapshot.py` downloads for real. The constraint that survives is the one
that mattered: **the fetch is a separate step that produces a hashed file, and
the simulation reads only the file.**

### Handling the key

- Read from the environment, never from a file in the repository, never from a
  command-line argument (arguments are visible to other processes and land in
  shell history).
- Absent or empty is an error that says where to create a key and how to set it,
  not a silent fallback to an anonymous request that fails later with a 401.
- **Never printed.** Not in logs, not in error messages, not in the provenance
  written beside the snapshot. The snapshot records the layer, the extent, the
  date and the hash — enough to reproduce the fetch, nothing that authorises it.
- Not configured as a CI secret. CI verifies hashes; it has no reason to fetch,
  and a key that does not exist in CI cannot leak from it.

## Consequences

- No simulation run touches the network, by construction rather than by
  convention — the property ADR 0008 established, kept.
- A collaborator without a LINZ account can still build, test and run
  everything: the synthetic adapters cover the whole suite, and a bundle they
  cannot re-fetch fails on a missing snapshot with the script's name in the
  message.
- Snapshots are not committed. The script and the hashes are, so a bundle is
  reproducible by anyone with an account of their own.
- Two of the three adapters are testable without any LINZ data at all. The file
  adapters are tested against fixtures the tests write with GDAL and read back,
  which keeps binary files out of the repository and makes the assertion a
  round trip rather than a comparison against a blob nobody can inspect.
- **Attribution is a requirement, not a courtesy.** LINZ data is CC BY 4.0, and
  the licence is carried in `SourceDescription::licence` so that
  `paddock source test` prints it: "Sourced from the LINZ Data Service and
  licensed for re-use under the Creative Commons Attribution 4.0 International
  licence."
