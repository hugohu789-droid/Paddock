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
| Legume N fixation | 25 kg N per t legume DM | Published NZ range 20–28 kg N/t ([review](https://rsnz.onlinelibrary.wiley.com/doi/full/10.1080/00288233.2023.2198719)) | test fixtures |
| NZ pasture production, dairy | ~14 t DM/ha/yr, over 20 irrigated | [Chapman et al. 2025](https://rsnz.onlinelibrary.wiley.com/doi/full/10.1080/00288233.2024.2425071) | T3 validation target |
| Temperate grass cool limit | ~4.4 C | same review | test fixtures |
| Measured seasonal growth, Lincoln P21 Low N (154 kg N/ha) and Woodlands (0 kg N/ha) | monthly kg DM/ha/day | [DairyNZ South Island averages, 2020](https://www.dairynz.co.nz/media/oxdi5vou/average-pasture-growth-data-south-island-2020-v1.pdf) | `data/calibration/`, T3 gate |

The two FAO-56 worked examples for extraterrestrial radiation and daylight hours
(Examples 8 and 9, 3 September at 20°S: Ra = 32.2 MJ m⁻² day⁻¹, N = 11.7 h) are
asserted directly in `tests/unit/SolarTest.cpp`, so the implementation is checked
against the source rather than against itself.

The example definitions under `data/` (`weather/`, `soils/`, `pastures/`) are
**PLACEHOLDER throughout** and marked as such in the files themselves. They
exist so the format has worked examples and so a schema change fails a test;
they are not a description of any real site, soil or sward.
| Slope and aspect from a DEM | Horn's 3×3 finite difference | [Horn (1981), Proc. IEEE 69(1):14–47](https://doi.org/10.1109/PROC.1981.11918); the default in `gdaldem` and ArcGIS | `core/Topography.cpp` |
| Radiation on a slope | Numerical integration of the surface-normal / sun dot product | Method of [Allen, Trezza & Tasumi (2006), Ag & Forest Met 139:55–73](https://doi.org/10.1016/j.agrformet.2006.05.012), integrated numerically rather than analytically | `core/Solar.cpp` |
| Gross energy of feed | 18.4 MJ kg⁻¹ DM | CSIRO (2007), quoted as Eq. 2 in the [OVERSEER ME review](https://www.overseer.org.nz) | not yet used |
| Energy density qm | dietME / GE | CSIRO (2007), OVERSEER review Eq. 2 | not yet used |
| Efficiency of ME for maintenance km | 0.35 qm + 0.503 (milk diets); 0.85 otherwise | CSIRO (2007), OVERSEER review Eq. 5–6. Note CSIRO's other published form, km = 0.02 (MJ ME/kg DM) + 0.5, is what Frater et al. quote | not yet used |
| Efficiency of ME for gain kgf | 0.035 × dietME × flegume × ftime (temperate pasture); 0.042 × dietME + 0.006 (tropical) | CSIRO (2007), OVERSEER review Eq. 9 and 11 | not yet used |
| Basal net energy | 0.28 × K × S × M × AgeFactor × lwt^0.75 MJ day⁻¹ | **Nicol & Brookes (2007), equation 1**, via OVERSEER review Eq. 13 | not yet used |
| Species factor K | 1.0 sheep, 1.4 dairy and beef (CSIRO 2007). Nicol & Brookes use 1.3 for beef | OVERSEER review, §4.1 — the two sources disagree and the disagreement is recorded rather than resolved | not yet used |
| Age factor | exp(−0.03 A), A in years | Frater, Howarth & McEwen, [J. NZ Grasslands](https://www.nzgajournal.org.nz/index.php/JoNZG/article/download/477/105/2053), Table 2. OVERSEER uses exp(−0.00008 a) with a in days | not yet used |
| Grazing activity allowance | Maintenance ME + 15% | Frater et al., stated as the "crude adjustment" used when comparing models | not yet used |

### Livestock energy, still incomplete (task #23)

The maintenance side is sourced above. Two things are **not**, and the model
must not pretend otherwise:

- **The net energy content of liveweight gain.** It can be reverse-engineered
  from the worked example below - 9.72 MJ ME/day total, of which maintenance
  plus grazing accounts for about 5.5, leaving roughly 42 MJ ME per kg of gain -
  but that is fitting a curve to one point, not citing a source. It needs
  Nicol & Brookes (2007), NZ Society of Animal Production Occasional
  Publication No. 14, pp. 151-172, which is not freely available online.
- **The sex factor S and the milk factor M** of equation 13. The OVERSEER review
  critiques them without restating them.

**Validation target, for when those arrive.** Frater et al. Table 1 gives, for a
weaned lamb of 28 kg gaining 100 g/day at 100 days old on a 10.5 MJ ME/kg DM
diet, with grazing costed at maintenance + 15%:

| Model | MJ ME/day | Intake (kg DM/day) |
|---|---|---|
| Nicol & Brookes (2007) | 9.72 | 0.93 |
| CSIRO (2007) | 9.71 | 0.92 |
| OVERSEER | 8.77 | 0.83 |

Reproducing 9.72 is the test to write once the gain equation is in hand.

## What each numeric assertion in the tests actually rests on

Not every number in a test is evidence of the same kind, and treating them alike
is how a project convinces itself it has validated something it has only pinned.
The suites here fall into three kinds, and the comments say which:

- **Validation** - compared against a value published by someone else. The
  NZTM control points come from LINZ's own conversion service; the false easting
  of 1 600 000 m is LINZ's definition; the level-ground radiation case is FAO-56
  Eq. 21.
- **Verification** - checked against arithmetic a reader can repeat. Slope and
  aspect on a tilted plane are closed-form; a 200 m by 200 m block at 10 m cells
  is 400 cells; the equinox equivalent-latitude identity follows from solar
  geometry and holds whatever this code does.
- **Regression pins** - numbers that came out of this implementation, kept so
  that a change has to be deliberate. They are labelled as pins in the tests
  that use them, and they are **not** evidence that the value is right. The
  midwinter slope ratios of about 2.00 and 0.06, the 0.9 floor on a midsummer
  tilt, and the 0.1 ha bound on rasterisation error are all of this kind.

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
| E8 | **No unfertilised Canterbury reference series.** T3 validates the seasonal *distribution* against Woodlands, the only site on DairyNZ's sheet with no nitrogen fertiliser - but Woodlands is Southland (46.4 S), not Canterbury (43.5 S). The Canterbury sites are the right climate and the wrong management (154-330 kg N/ha). Neither is both. A proper target is an unfertilised Canterbury trial such as the Winchmore dryland series; until then the magnitude comparison carries this caveat. | M3 |
| E7 | **The pasture growth parameters are fixtures, not a calibration.** Radiation use efficiency, extinction coefficient, specific leaf area, senescence and residual are the right shape and order of magnitude, but the published ranges are wide and often whole-plant (RUE near 2 g DM/MJ usually includes roots; this model grows only what an animal eats). **T3 now measures the gap rather than assuming it.** Over twenty simulated years the model produces 9.8 t DM/ha/yr against 11.0 t measured at the unfertilised Woodlands site and 16.3 t at fertilised Lincoln, and its seasonal curve correlates 0.97 with the unfertilised site and 0.91 with the fertilised one. The shape is right; the magnitude is 11% low against the closest-managed site. No growth figure from this model should be quoted until the parameters themselves are sourced. | M2 shape done, magnitude M3 |
| E5 | The CliFlo column mappings in `scripts/cliflo-snapshot.py` are guesses until checked against a real export of each datatype. The script prints the headings it found and accepts `--column HEADING=field`, so a wrong guess is visible rather than silent. | M2, on first real download |
