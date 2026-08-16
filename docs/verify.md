# Verification tracker

Every model parameter cites a published source in its data file, or is marked
`# PLACEHOLDER — verify against <source>` and listed here until it does.

**M1 introduces no model parameters.** The kernel holds types, not numbers, so
this table is a statement of what M2 onwards must not skip.

## Open items

| # | Item | Needed by | Source to check | Status |
|---|---|---|---|---|
| 1 | Pasture growth: base and optimal temperatures, seasonal growth rates, annual DM yield by region | M2 | DairyNZ, AgResearch | open |
| 2 | Soil water: profile available water by S-map soil class, drainage class | M2 | Manaaki Whenua S-map | open |
| 3 | Intake, stocking rates, stock-unit conversions | M3 | Beef+Lamb NZ, DairyNZ | open |
| 4 | Facial eczema: spore-count thresholds and warm-wet trigger conditions | M4 | Veterinary and extension material | open |
| 5 | Grass grub: degree-day development model | M4 | AgResearch literature | open |
| 6 | Nitrogen leaching: regulatory thresholds | M4 | Current Regional Council rules | open |
| 7 | LINZ, NIWA and Manaaki Whenua licence terms and access methods | M2-M3 | Dataset licences | open |

Item 7 also gates the repository licence and what may be redistributed with a
release, so it is worth settling before M2 rather than at M5.

## Engineering caveats

Recorded here because they affect whether a result can be trusted, not because
they are parameters.

| # | Caveat | Milestone |
|---|---|---|
| E1 | `std::uniform_real_distribution` and friends are implementation-defined: the same seed reproduces bit-for-bit on one platform and standard library, not across them. Core will need its own distribution transforms before cross-platform golden baselines mean anything. See [ADR 0005](adr/0005-deterministic-rng.md). | M2 |
| E2 | The conservation suite currently exercises the ledger, not agronomy. Every process added in M2 must declare which budget lines it touches and report its flows, or the gate silently proves less than it appears to. | M2 |
| E3 | Coordinate transforms have no round-trip test yet because `gis/` has no PROJ dependency yet. The 1 mm control-point requirement lands with the first transform. | M3 |
