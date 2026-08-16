# Verification tracker

Every model parameter cites a published source in its data file, or is marked
`# PLACEHOLDER — verify against <source>` and listed here until it does.

## Parameters in the model

Every number below is in the code or in a test fixture today, with the source it
came from. Anything not on this list and not marked PLACEHOLDER is a bug.

| Parameter | Value | Source | Used in |
|---|---|---|---|
| Solar constant Gsc | 0.0820 MJ m⁻² min⁻¹ | [FAO-56 Eq. 21](https://www.fao.org/4/x0490e/x0490e07.htm) | `core/Solar.cpp` |
| Inverse Earth-Sun distance dr | 1 + 0.033 cos(2πJ/365) | FAO-56 Eq. 23 | `core/Solar.cpp` |
| Solar declination δ | 0.409 sin(2πJ/365 − 1.39) | FAO-56 Eq. 24 | `core/Solar.cpp` |
| Sunset hour angle ωs | arccos(−tan φ tan δ) | FAO-56 Eq. 25 | `core/Solar.cpp` |
| Radiation to evaporation | 0.408 mm per MJ m⁻², λ = 2.45 MJ kg⁻¹ | FAO-56 Eq. 20 | `core/Solar.cpp` |
| Hargreaves ETo | 0.0023 (Tmean + 17.8) √(Tmax − Tmin) Ra | FAO-56 Eq. 52 | `core/SoilWater.cpp` |
| Total available water TAW | 1000 (θFC − θWP) Zr | [FAO-56 Eq. 82](https://www.fao.org/4/x0490e/x0490e0e.htm) | `core/SoilWater.cpp` |
| Water stress Ks | (TAW − Dr) / (TAW − p·TAW) | FAO-56 Eq. 84 | `core/SoilWater.cpp` |
| Depletion adjustment | p = p_table + 0.04 (5 − ETc), clamped to [0.1, 0.8] | FAO-56 Table 22 note | `core/SoilWater.cpp` |
| Depletion fraction p, grazed pasture | 0.60 | FAO-56 Table 22 (rotated and extensive grazing) | test fixtures |
| Crop coefficient Kc, rotated grazing | 0.85–1.05 mid-season; 0.95 used | [FAO-56 Table 12](https://www.fao.org/4/x0490e/x0490e0b.htm) | test fixtures |

The two FAO-56 worked examples for extraterrestrial radiation and daylight hours
(Examples 8 and 9, 3 September at 20°S: Ra = 32.2 MJ m⁻² day⁻¹, N = 11.7 h) are
asserted directly in `tests/unit/SolarTest.cpp`, so the implementation is checked
against the source rather than against itself.

## Open items

| # | Item | Needed by | Source to check | Status |
|---|---|---|---|---|
| 1 | Pasture growth: base and optimal temperatures, seasonal growth rates, annual DM yield by region | M2 | DairyNZ, AgResearch | open |
| 2 | Soil water: profile available water (theta_FC, theta_WP, rooting depth) by S-map soil class, drainage class | M2 | Manaaki Whenua S-map | open - the FAO-56 formula is implemented, the per-soil inputs are not yet real |
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
| ~~E1~~ | **Closed in M2.** `std::` distributions are implementation-defined, so the same seed gave different numbers on different standard libraries. Core now implements its own — see [ADR 0007](adr/0007-own-distributions.md). Uniform deviates are bit-identical across platforms; normal, exponential and gamma agree to within four units in the last place, limited by libm's rounding of `log`, `sqrt` and `pow`. Golden vectors are asserted on all three CI platforms. | M2 |
| E2 | The conservation suite currently exercises the ledger, not agronomy. Every process added in M2 must declare which budget lines it touches and report its flows, or the gate silently proves less than it appears to. | M2 |
| E3 | Coordinate transforms have no round-trip test yet because `gis/` has no PROJ dependency yet. The 1 mm control-point requirement lands with the first transform. | M3 |
| E4 | The synthetic weather generator draws wet days independently, so it has no wet spells and cannot produce a realistic drought. Keeping each day keyed by its date is what makes any subrange of a run reproducible, and a Markov chain would need an arbitrary anchor. Real-year replay is unaffected. See [ADR 0008](adr/0008-weather-sources.md). | M4, with the drought scenario |
| E6 | FAO-56 says Eq. 52 (Hargreaves) should be checked against Penman-Monteith in each new region before it is trusted. Paddock uses it because CliFlo stations near a farm reliably report temperature and often nothing else; the regional check against a station that does report humidity and wind is outstanding, and belongs with the T3 validation gate. | M2, with T3 |
| E5 | The CliFlo column mappings in `scripts/cliflo-snapshot.py` are guesses until checked against a real export of each datatype. The script prints the headings it found and accepts `--column HEADING=field`, so a wrong guess is visible rather than silent. | M2, on first real download |
