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

## Terrain

| Parameter | Value | Source | Used in |
|---|---|---|---|
| Slope and aspect from a DEM | Horn's 3×3 finite difference | [Horn (1981), Proc. IEEE 69(1):14–47](https://doi.org/10.1109/PROC.1981.11918); the default in `gdaldem` and ArcGIS | `core/Topography.cpp` |
| Radiation on a slope | Numerical integration of the surface-normal / sun dot product | Method of [Allen, Trezza & Tasumi (2006), Ag & Forest Met 139:55–73](https://doi.org/10.1016/j.agrformet.2006.05.012), integrated numerically rather than analytically | `core/Solar.cpp` |

## Livestock energy (task #23)

The equations below are quoted from one document, cited here once and referred
to as **TMC** in the tables:

> Wheeler, D M (2018). *Animal metabolisable energy requirements.* Technical
> Manual for the description of the OVERSEER® Nutrient Budgets engine, version
> 6.3.0. AgResearch Ltd for OVERSEER Limited, June 2018. ISSN 2253-461X.
> [PDF](https://assets.ctfassets.net/bo1h2c9cbxaf/7eDPxk2KcUN3oMpe25Xd76/8e0b6c4b1c338e4301fc0ed38fda6d0f/TMC_Animal_Metabolisable_Energy_Requirements_v6.3_June_2018.pdf)
> — SHA-256 `f83171454056ecdd9e8e801e3958b9cbc387109c195dbebf0b34ebee7ffa016a`,
> 628 523 bytes, retrieved 18 August 2026.

The hash is here for the same reason the snapshot scripts record one: an
equation number is only traceable if the document it points into is the one that
was read. The PDF itself is not committed — see the licence note at the end of
this section.

TMC is a secondary source that states its own primary ones, and both are given
below. Where the primaries disagree, the disagreement is recorded rather than
resolved.

### Maintenance

| Quantity | Value | Source | Used in |
|---|---|---|---|
| Gross energy of feed | 18.4 MJ kg⁻¹ DM | CSIRO (2007), TMC Eq. 2 | not yet used |
| Energy density qm | dietME / GE | CSIRO (2007), TMC Eq. 2 | not yet used |
| Efficiency of ME for maintenance km | 0.35 qm + 0.503 (milk diets); 0.85 otherwise | CSIRO (2007), TMC Eq. 5–6. CSIRO's other published form, km = 0.02 (MJ ME/kg DM) + 0.5, is what Frater et al. quote | not yet used |
| Efficiency of ME for gain kgf | 0.035 × dietME × flegume × ftime (temperate pasture); 0.042 × dietME + 0.006 (tropical) | CSIRO (2007), TMC Eq. 9 and 11 | not yet used |
| Basal net energy | 0.28 × K × S × M × AgeFactor × lwt^0.75 MJ day⁻¹ | **Nicol & Brookes (2007), equation 1**, restated as TMC Eq. 13 | not yet used |
| Species factor K | sheep 1.0, dairy 1.4, dairy replacements 1.4, beef 1.4, deer 1.7, dairy goats 1.25 | TMC Eq. 13, following CSIRO (2007). **Nicol & Brookes use 1.0 sheep, 1.3 beef, 1.5 dairy, 1.4 deer** — see below | not yet used |
| Sex factor S | 1.15 entire males; 1.075 mixed-sex mobs; 1.0 females and castrated males | TMC Eq. 14 | not yet used |
| Milk factor M | 1 after weaning; 1 + (0.26 − Mage × age/7) before, Mage = 0.26 / weeks suckled (0.015 dairy and beef, 0.010 otherwise) | SCA (1994), TMC Eq. 15–16. **Nicol & Brookes did not include M** | not yet used |
| Age factor | exp(−0.00008 a), a in days, floor 0.84 | Freer et al. (2006), TMC Eq. 17. Frater et al. quote exp(−0.03 A) with A in years; TMC records the two methods as differing by about 0.14% on average | not yet used |

**On K.** The two primaries genuinely disagree, and which one is picked changes
maintenance for beef cattle by about 8%. TMC §4.1 follows CSIRO for sheep, dairy
and beef and Nicol & Brookes for deer. This project has no basis yet for
preferring either, so the choice belongs in a data file with both values
recorded, not hard-coded.

**An independent check on the 0.28.** TMC notes that Simpson (1978b) reported
values for maintenance ME per kg lwt^0.75 of 0.40 for sheep and 0.55 for cattle.
Net 0.28 divided by a km of about 0.7 gives 0.40 — Simpson's sheep figure, from
a different author two decades earlier. That is a validation target rather than
a regression pin, and it is worth asserting when the code exists.

### Grazing, and why it depends on the terrain

These are the equations that connect livestock to the slope and aspect work
already merged (#19, #20), which is why they matter beyond bookkeeping.

| Quantity | Value | Source |
|---|---|---|
| Chewing | NEchew = lwt × SpGraze × DMintake × (0.9 − digest/100) | TMC Eq. 18 |
| Dry matter intake | DMintake = MEintake / dietME | TMC Eq. 19 |
| Grazing coefficient SpGraze | 0.0025 for dairy and beef | TMC Eq. 20 |
| Movement | NEmove = 0.0026 × lwt × SlopeMoveFactor × (TSR / SD) / fmove | TMC Eq. 21 |
| Pasture-mass term | fmove = 0.000057 × PastureMass × 1000 + 0.16 | TMC Eq. 22 |
| **Slope term** | SlopeMoveFactor = 1 + tan(slope) | TMC Eq. 23 |
| Activity | NEactivity = lwt × (0.0026 × Hkm + 0.028 × Vkm) | TMC Eq. 24 |

`SlopeMoveFactor = 1 + tan(slope)` is the whole link: it is 1.00 on the flat,
1.18 at 10°, 1.36 at 20° and 1.58 at 30°. The vertical component of Eq. 24 costs
about eleven times the horizontal one per kilometre. A farmlet on a steep face
therefore pays twice over: the pasture on it grows against a slope radiation
ratio from `SlopeRadiationTable`, and the stock on it spend more energy walking.

### Liveweight change

| Quantity | Value | Source |
|---|---|---|
| Net energy of liveweight change | NElwt = lwtchange × 0.92 × EVG (0.92 = liveweight to empty body weight) | TMC Eq. 43 |
| Energy value of gain | EVG = (6.7 + R) + (k1 − R) / (1 + exp(−6 (Z − 0.4))), k1 = 16.5 large lean cattle breeds, 20.3 otherwise | TMC Eq. 44 |
| Maturity | Z = lwt / SRW | TMC Eq. 45 |
| Rate adjustment | R = (lwtchange × 1000 × 0.92) / (4 SRW^0.75) − 1 | SCA (1994), used by Nicol & Brookes (2007), TMC Eq. 46 |

This is the gap the previous revision of this file recorded as open. It is now
closed from a primary-stating source, so the model no longer has to
reverse-engineer a gain cost from a single worked example.

Evaluating it as a sanity check: at R ≈ 0, EVG is about 11.4 MJ/kg empty body
gain for a young animal (Z = 0.2) and about 26.5 MJ/kg near mature weight
(Z = 1.0). Dividing by an efficiency of gain of 0.4–0.5 puts ME between roughly
21 and 60 MJ per kg of liveweight gain. A figure of "25–55 MJ ME/kg" is widely
attributed to Nicol & Brookes in secondary summaries; that range is consistent
with these equations, but **it is not cited here**, because it was found in a
search result rather than read in a document.

### Validation target

Frater, Howarth & McEwen,
[J. NZ Grasslands](https://www.nzgajournal.org.nz/index.php/JoNZG/article/download/477/105/2053),
Table 1, for a weaned lamb of 28 kg gaining 100 g/day at 100 days old on a
10.5 MJ ME/kg DM diet, with grazing costed at maintenance + 15%:

| Model | MJ ME/day | Intake (kg DM/day) |
|---|---|---|
| Nicol & Brookes (2007) | 9.72 | 0.93 |
| CSIRO (2007) | 9.71 | 0.92 |
| OVERSEER | 8.77 | 0.83 |

Every term needed to reproduce 9.72 is now in the tables above, so this is a
test to write against the implementation rather than an open literature item.
The three models differing by 10% on the same animal is itself the useful fact:
it sets the accuracy this part of the model can honestly claim.

### What is still not sourced

- **Nicol & Brookes (2007)** itself — NZ Society of Animal Production Occasional
  Publication No. 14, pp. 151–172 — has not been read. Every attribution to it
  above is TMC's, and is marked as such. It is not freely available online.
- **The Massey thesis** listed in an earlier revision is unreachable from this
  environment: `mro.massey.ac.nz` refuses the TLS handshake, and curl,
  PowerShell and an HTTP fetch all failed at the network layer rather than on
  content. It is not cited above.
- **Standard reference weight (SRW)** by breed and class, which Eq. 45 and 46
  both need, is in a different TMC chapter that has not been retrieved.

### Licence note

TMC's copyright statement permits copying and use of the report and the
information in it, on condition that the use does not mislead as to its
contents, that any copy carries the disclaimer in full, and that neither the
report nor its contents are used "in connection with any promotion, sales or
marketing of any goods or services". Equations are facts and are not restricted
by copyright in any case, but the last clause is a real constraint on a project
that may become commercial, and it belongs with the Open-Meteo and VCSN terms in
open item 7 rather than being discovered later. The PDF is therefore not
committed; the URL and hash above are enough to retrieve the same document.

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
| 3 | Intake, stocking rates, stock-unit conversions | M3 | Beef+Lamb NZ, DairyNZ | energy equations sourced (see Livestock energy); standard reference weight by breed and class still needed |
| 4 | Facial eczema: spore-count thresholds and warm-wet trigger conditions | M4 | Veterinary and extension material | open |
| 5 | Grass grub: degree-day development model | M4 | AgResearch literature | open |
| 6 | Nitrogen leaching: regulatory thresholds | M4 | Current Regional Council rules | open |
| 7 | LINZ, NIWA and Manaaki Whenua licence terms and access methods; Open-Meteo and VCSN terms; the OVERSEER technical manual's no-promotion clause | M2-M3 | Dataset and document licences | open |

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
