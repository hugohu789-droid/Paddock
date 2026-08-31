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
| Hill country slope classes | easy 15–20°, steep 25–30° | [Gillingham, Gray & Smith (1998), Proc. NZ Grassland Association 60](https://www.nzgajournal.org.nz/index.php/ProNZGA/article/view/2319) | `tests/unit/SolarSlopeTest.cpp` |

**A field check on the slope radiation model.** Gillingham et al. measured
pasture growth on a southern Hawke's Bay hill farm across north and south
aspects at both slope classes, and report that "pasture growth on north-facing
steep slopes in winter was greater than on any areas within the south aspect at
this time" — while soil moisture was *higher* on the south aspect throughout.
The north aspect therefore won its winter advantage despite being drier, which
leaves radiation to explain it.

`SolarSlopeTest.MidwinterRadiationRanksHawkesBayAspectsAsGillinghamMeasuredThem`
checks that the model ranks those four classes the same way. It is validation
rather than a pin: the ordering comes from a field trial by other authors.

It also records where the model and the measurement *disagree*. On the north
aspect, radiation alone favours the steeper face, because a steeper slope meets
the low winter sun more squarely; Gillingham still measured easy above steep
there. Light is not the whole story on the sunny side — soil depth and moisture
are — and the test says so rather than hiding it. Their numeric growth rates
are in bar charts, not tables, so only the ordering could be extracted.

## Example farms

The farm set is open: `data/farms/` is scanned, not enumerated, so what is
recorded here is the provenance of each file's numbers rather than a list the
code depends on.

| Parameter | Value | Source | Used in |
|---|---|---|---|
| Massey Dairy 4 effective area | 221 ha | [Massey University, Dairy 4 farm page](https://www.massey.ac.nz/about/massey-subsidiaries-and-commercial-ventures/massey-farms/dairy-farm-4/), "Effective Area: 221 hectares", retrieved 18 August 2026 | `data/farms/massey-dairy-4.toml`, asserted in `tests/config/DataFilesTest.cpp` |
| Massey Dairy 4 subdivision | approximately 80 paddocks of 1.5–3.5 ha | same page: "approximately 80 x 1.5-3.5 hectare paddocks all with race access" | same; also the source of the 2.5 ha default in `SyntheticParcelSource` |
| Massey Dairy 4 soils | Tokomaru silt loam, some Ohakea silt loam on lower terraces | same page | not yet used — this is what the S-map lookup should return for this farm |
| Farm centre coordinates, all three farms | NZTM2000 | LINZ Concord conversions, via the control point table in `tests/gis/ProjectionTest.cpp` | `data/farms/*.toml`, all marked `location_verified = false` |

**On the farm centres.** Each of the three files takes its coordinates from a
LINZ-converted control point for the *locality* - Ruakura, the Canterbury
Plains, Palmerston North - and none of them is a surveyed farm centroid. They
are right to within a few kilometres, which is right for solar geometry and
wrong for a fence. Every file says so in its `source` field and carries
`location_verified = false`; see open item 8.

**On the Massey extent.** The rectangle is not the farm's outline and is not
claimed to be. What is claimed, and tested, is that it encloses the published
221 ha and subdivides into 80 paddocks averaging 2.7625 ha - the published
count and the published mean. The generator's target size of 2.5 ha is tuned to
achieve that and is labelled as tuned in the file, because feeding in the
published mean instead yields 70 paddocks of 3.16 ha, which matches neither.

## Grazing systems (task #25)

The systems worth comparing are not a design choice: New Zealand hill country
extension literature describes three, and names what separates them.

> Smith, M E and Dawson, A D (1976). *Hill country grazing management.*
> Proceedings of the New Zealand Grassland Association, volume 38. MAF Advisory
> Services Division, Hamilton.
> [PDF](https://www.nzgajournal.org.nz/index.php/ProNZGA/article/view/1469)
> — running heads in the PDF put it at pages 47–54; the exact range is not
> stated on the article page, so it is not cited here.

| System | Definition, from the paper | Expected effect |
|---|---|---|
| Set stocking | "animals graze the pasture almost continuously"; spelling happens but "the process is uncontrolled and often highly selective" | Erect grasses and clovers suffer, browntop persists; "lower production and utilization" |
| The "shuffle" | An attempt at rotation with too few paddocks: "with a limited number of paddocks and five or six mobs, the effect must be to lengthen the grazing period or shorten the spelling period" | "little improvement on set-stocking" |
| True rotational | "the pasture is grazed for a short time and spelled for a long time" | Erect grasses and palatable legumes encouraged; more leaf area in winter, deeper roots in summer |

**Quantified guidelines, same source.** Graze no more than **3 days** with the
major mob. Minimum spell for the Hamilton region: **spring 12 days, summer 35,
autumn 35, winter 35**; summer may fall to 25 days "only if substantial rain
falls", and colder regions need "40 days plus" in winter. Context: hill country
farms then had **15–25 major grazing paddocks and 5–6 mobs**.

**The shuffle should emerge, not be implemented.** The paper defines it as what
happens when rotational intent meets an under-subdivided farm — too few
paddocks for too many mobs forces either longer grazings or shorter spells. A
model that carries paddock count, mob count and the two rotational parameters
will produce it on its own. Coding it as a third mode would be modelling the
symptom instead of the cause, and would lose the thing a farmer actually wants
to know: how many paddocks their farm needs before rotation starts paying.

### Validation target

Table 1 of the same paper, a Tauranga property running both systems at the same
stocking rate of 15.6 SU/ha:

| Hogget measure | Rotational | Set stocking | Difference |
|---|---|---|---|
| Liveweight, November (kg) | 45.5 | 38.5 | +7.0 |
| Liveweight, February (kg) | 50.5 | 44.6 | +5.9 |
| Fleece weight, March–October (kg) | 2.28 | 1.73 | +0.55 (+32%) |

The paper adds that this weight difference at mating implies "about a 12%
advantage in lambing percentage".

**How much this target is worth.** Less than the Gillingham and Frater ones, and
the difference should not be quietly forgotten. This is a 1976 advisory paper
arguing for a system its authors recommend; Table 1 is one property, not a
replicated trial, and the February figure is confounded because both groups were
rotationally grazed from October. It is a plausible magnitude to reproduce — a
few kilograms of hogget liveweight, not tens — rather than a number to hit. A
model that showed set stocking ahead, or showed a 30 kg gap, would be wrong;
one that shows 4 kg is not.

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
| Efficiency of ME for maintenance km | **0.85 on milk diets; 0.35 qm + 0.503 otherwise** | CSIRO (2007), TMC Eq. 5 and 6. CSIRO's other published form, km = 0.02 (MJ ME/kg DM) + 0.5, is what Frater et al. quote | not yet used |
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
Take a pasture diet of 10.5 MJ ME/kg DM: qm = 10.5 / 18.4 = 0.571, so Eq. 6
gives km = 0.35 × 0.571 + 0.503 = 0.703, and the net 0.28 becomes
0.28 / 0.703 = **0.40 MJ ME/kg lwt^0.75** — Simpson's sheep figure, from a
different author two decades earlier, with no free parameter in between. That
is a validation target rather than a regression pin, and it is worth asserting
when the code exists.

The arithmetic also settles which way round Eq. 5 and 6 go: the 0.85 belongs to
milk diets and the qm expression to everything else. Reading them the other way
gives km = 0.85 on pasture and a maintenance requirement of 0.33, which matches
nothing published.

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

### What is implemented, and what is not

`core/AnimalEnergy.cpp` implements the maintenance chain (TMC Eq. 2, 5–6, 9,
13, 14, 17), the grazing terms (Eq. 18, 20–24), liveweight change (Eq. 43–46,
81) and the iterative solution of the chewing circularity (Eq. 52, 54–55).

The inverse - what a given intake does to an animal's weight - is also
implemented, and it is the direction a simulation needs: the manual answers
"to grow this fast, what must it eat", and a farm needs "having eaten this, how
fast does it grow". The efficiency is asymmetric and the asymmetry is the
manual's: TMC Eq. 8 gives km / 0.8 for a non-lactating animal losing weight
against kgf for one gaining, so mobilising tissue is more efficient than
depositing it and the same energy gap moves the animal further down than up.

Two readings there are this project's rather than the manual's, both flagged in
the code. TMC Eq. 54 charges a tenth of the production cost to maintenance;
that share is not charged on a deficit here, because an animal losing weight is
not producing and the manual does not cover the case. And **starvation is not
modelled**: an underfed animal loses weight indefinitely, with no floor and no
mortality, so a run that reaches implausible weights is reporting a feed budget
that does not work rather than an animal that survived it.

**Not implemented, and a farm model is incomplete without them:**

| Missing | Equations | Consequence |
|---|---|---|
| Lactation | TMC Eq. 33–35 | There is no way to represent a cow in milk. A "dairy cow" in the model today is a dry cow, and eats roughly half what a milking one would |
| Pregnancy | TMC Eq. 26–32 | No cost of carrying a calf or lamb, so late-gestation demand is absent |
| Wool and velvet | Eq. 7, §4.8–4.9 | Sheep and deer are cheaper to keep than they are |
| Pre-weaned animals | §5.2.4, Eq. 15–16 | The milk factor M is fixed at 1, which is only correct after weaning |
| Cold, heat, wet and wind | §4.10 | No weather cost on the animal, only on the pasture |

The seasonal temperate-pasture form of the efficiency of gain (Eq. 11, with
`flegume` and `ftime`) is also not used. TMC states it and then says it "has not
been implemented" in OVERSEER either; separately, the text of its `ftime` term
did not survive PDF extraction unambiguously — it reads
`ftime = 1 + 0.12 8 (lat * sin(0.0172 day)/40))`, with an unmatched bracket and
a space inside what is probably 0.128. Eq. 9 is used instead, which is
unambiguous and is what OVERSEER runs.

One reading is this project's and not the manual's: TMC gives km for milk diets
(Eq. 5) and for everything else (Eq. 6) but not for a diet that is part milk.
`DietQuality::maintenance_efficiency` blends the two by the milk fraction. That
is the obvious interpolation, and it is flagged here rather than presented as
sourced.

### Grazing: what the coupling does and does not do

`core/Grazing.cpp` takes a mob's intake from the energy chain above and removes
it from the sward, capped at what stands above each species' residual. Two
simplifications are large enough that a reader should meet them here rather
than discover them in a long run.

**Dung and urine are not returned.** Eaten dry matter and the nitrogen in it are
both recorded as outflows, so the budget closes exactly - but on a real paddock
most of that nitrogen is back within days. A multi-year run will therefore strip
nitrogen from the farm in a way a real one does not, and any nitrogen result
from such a run is wrong in a known direction. Fixing it means modelling
excretal return, which is a piece of work in its own right.

**Grazing is not selective.** Grass and legume are taken in proportion to what
each offers above its residual. Smith and Dawson (1976) report that set stocking
is "highly selective" and that clovers under it "tend to be overgrazed, existing
only as stunted plants" - so the species-composition half of their finding is
outside what this model can currently show. Quantifying a preference needs a
source this project does not yet have; see open item 10.

### Two errors a year-long run caught

Both were found by running `data/scenarios/canterbury-grazed` for a year, not by
any unit test, and both are recorded because the class of mistake matters more
than the fix.

**Demand must come from what an animal wants, not what it did.**
`daily_energy_requirement` answers "to change weight at this rate, what must it
eat", so feeding it a *realised* rate inverts the question. A mob that lost
weight overnight returned a negative production term, which reads as an energy
credit and shrank the requirement - so it ate less, lost more, and asked for
less again. A year took a mob from 55 kg to two grams on a farm whose mean cover
never fell below 2000 kg DM/ha and which reported feed-limited on two days out
of 366. Demand is now computed against holding weight. Appetite rising when an
animal goes short is real and is not modelled; that needs intake capacity, which
has its own literature.

**Set stocking is not "do not move".** Modelled as leaving a mob where it
stands, it confines the stock to one paddock - the opposite of the system. Smith
and Dawson (1976) say of lambing that "the whole of the farm area should be used
for grazing". The mob lost fifteen kilograms over seventy days on two hectares
while forty-seven other paddocks carried feed. A mob now holds a *set* of
paddocks: one under rotation, all of them under set stocking. That also makes
the agronomic point fall out on its own - under set stocking every paddock is
grazed every day, so none of them rests, which is why the source says the system
grows less.

### Calibrating against New Zealand industry sources

Six sources were fetched and read before anything was recorded against them, and
`data/calibration/livestock/sources.toml` holds the manifest with hashes. Three
findings came out of doing that rather than taking the numbers on trust.

**Cattle maintenance validates to 2%.** DairyNZ's *Lactating cow* page gives
maintenance ME by liveweight, stated as calculated at 11.0 MJ ME/kg DM. The
equations already in `core/AnimalEnergy.cpp` reproduce it across the whole
published range:

| Liveweight | DairyNZ | This model | Difference |
|---|---|---|---|
| 300 kg | 40 | 39.7 | −0.8% |
| 400 kg | 50 | 49.2 | −1.5% |
| 500 kg | 59 | 58.2 | −1.4% |
| 600 kg | 68 | 66.7 | −1.9% |

This is the cattle validation this file had been missing, and it is stronger
than the Simpson sheep check because it spans a range rather than a point.

**But only with the age factor set to one**, and that question is now answered:
the age term does apply to adult cattle. The OVERSEER ME review works an example
for a 500 kg animal at four years using it —
`MEm = ((0.36 W^0.75)/km) * exp(-0.00008 A)` — and discusses its effect for
animals of six years and more. So the 17% between the model and DairyNZ at
500 kg is a difference between a mechanistic framework and a practical table,
not evidence that the age term belongs only to growing stock.

**The floor is a different matter, and the two OVERSEER documents contradict
each other about it.** The technical manual states "Agefactor had a minimum
value of 0.84 (Freer et al., 2006)". The review asks why "the lower bound on
AgeFactor was not used as in the Freer et al. (2010) expression", notes that
CSIRO (2007) and Nicol and Brookes (2007) place no lower bound, and says the
omission "may allow ME requirements to drop too low for animals older than 6
years".

This implementation has the floor, which matches what the manual documents and
what the review argues for. It should not be described as what the reviewed
OVERSEER code does, because the review says that code lacks it. Open item 11 is
now about which variant to carry, not about whether the term applies.

**Sheep: the earlier 26% was a comparison against the wrong table.**

Beef + Lamb NZ publish two sheep maintenance tables, from two frameworks, in two
documents, and carry the same worked example in the same words with different
answers:

| Publication | Framework | 50 kg ewe at maintenance |
|---|---|---|
| A guide to feed planning for sheep farmers, Appendix 1.2 | Nicol and Brookes (2007) | 8.0 MJ ME/day |
| Making every mating count, Appendix 3.1 and Table 2.2 | Geenty and Rattray (1987) | 10.0 MJ ME/day |

The equations here descend from Nicol and Brookes by way of the OVERSEER manual,
so the first is the comparator. Against it the model is 1.9% low at 45 kg, not
26%.

**But the agreement is not clean, and what is left is more interesting than
either number.** The deviation grows with weight:

| Ewe | Nicol and Brookes | This model | Difference |
|---|---|---|---|
| 45 kg | 7.0 | 6.87 | −1.9% |
| 50 kg | 8.0 | 7.43 | −7.1% |
| 60 kg | 10.0 | 8.52 | −14.8% |
| 70 kg | 11.0 | 9.56 | −13.0% |

An earlier note here read an exponent out of that and said the table was steeper
in liveweight than W^0.75. **That was over-reading a rounded table.** Its steps
are 1.0, 1.0, 1.0, 0.5, 0.5 MJ - rounded to the half-megajoule, with a clear
change of slope at 60 kg. Fitting a power law gives 1.24 over 45-60 kg, 0.62
over 60-70, and 1.02 overall, which is three different answers from six rounded
points and means none of them.

What is left is a better question. The ME review quotes the Nicol and Brookes
maintenance equation in full:

    MEm = K.S.M (0.28 W^0.75 exp(-0.03A))/km + 0.1 MEp + MEgraze + Ecold

It carries **MEgraze** and **Ecold**, and this implementation has neither in its
maintenance figure - grazing cost is computed separately and cold is not
modelled at all. A published table of requirements for *grazing* ewes would
include them, and grazing cost scales with liveweight rather than with W^0.75,
which would put a model without it increasingly below the table as the animal
gets heavier. That matches the direction of what is seen. It is a hypothesis
with an equation behind it rather than a measurement, and testing it needs the
chapter itself.

The Geenty and Rattray table stays, as a separate framework rather than an
error. What its practical grazing allowance is made of needs the 1987 chapter,
which has not been read.

**One attribution in the brief was wrong, and it mattered.** The OVERSEER ME
review was cited for K = 1.4 for beef cattle. The review does quote OVERSEER's
values — 1.4, 1.4, 1.0, 1.4, 1.7, 1.25 — but its Recommendation 6 is to *change*
them: "Modify the value of Kantype in the BASAL equation to be 1.0 for sheep,
1.3 for British cattle breeds and 1.5 for beef and dairy cattle of dairy
origin." It also notes that OVERSEER's equation "is cited as derived from Nicol
and Brookes (2007), but uses the K values" of another source. So the K table is
a record of what OVERSEER does, not a value that review endorses — and the
disagreement recorded above now has an independent reviewer on the 1.3 side.

The same review gives a route for the cold-stress term this model lacks
(Recommendation 9): CSIRO (2007) equations, NIWA monthly climate, and Cottle and
Pacheco (2016) for fleece depth.

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

## What this model may be quoted for

The evidence is uneven, so this says plainly which outputs carry weight today
and which do not. It is the short version; the sections above are the working.

| Output | Standing | Why |
|---|---|---|
| Conservation of dry matter, water and nitrogen | **Sound** | A property of the bookkeeping, not of any parameter. Closes to 1e-9 over a grazed year |
| Deterministic replay | **Sound** | Same reason |
| Comparison between grazing systems | **Sound for direction; treat the size with care** | Both arms carry the same stock, the same parameters and the same structure, which is what makes the comparison worth more than either figure alone. It does not make the difference exact: the responses are non-linear, so a wrong parameter can bias the magnitude - by more in one arm than the other, because the arms sit at different points on the same curve |
| Direction of a seasonal or terrain effect | **Sound** | Checked against Gillingham's field trial for slope and aspect |
| Cattle maintenance requirement | **Sound to about 2%** | Reproduces DairyNZ across 300-600 kg |
| Sheep maintenance requirement | **Low by about 5%** | Against CSIRO (2007) at Adjabui et al.'s own 60 kg ewe, and 12% against Nicol and Brookes (2017). The **basal term agrees to 0.3%** - it is the same equation - and the remaining gap is the cost of grazing, walking and activity, which both published figures include. **This was 15% and 21% until the walking distance was supplied** (E10): TMC Eq. 24 was implemented and fed by nothing, so the model charged about a tenth of the published grazing cost and now charges about seven tenths of it. **CSIRO is the comparator, and the choice is not the flattering one being picked**: Adjabui et al. record that CSIRO accounts for chewing and ruminating inside km, while Nicol and Brookes add them separately on top of the same km, and that AgResearch (2016) asked the authors to correct that double count. This model uses km the CSIRO way, so measuring against the other figure would book somebody else's arithmetic as this project's error. Both are reported. The earlier 2% to 15% arrived at nearly the right number by the wrong route - a bare basal term subtracted from a grazing-inclusive published one. See `SheepMaintenanceAgainstAdjabuiWithGrazingIncluded` |
| **Carrying capacity for sheep** | **Overstated, by about 5%** | Follows directly from the line above: a 60 kg ewe on steep hill is modelled as needing 8.6 MJ ME/d where CSIRO implies 9.0, so a farm still looks able to carry more stock than it can - but by a twentieth rather than the sixth it was before the walking was charged |
| **Absolute liveweight gain** | **Not quotable** | Standard reference weight is `verify` on every species definition, and it drives the energy value of gain |
| Anything involving a milking cow | **Not modelled** | Lactation is absent; a "dairy cow" here is a dry cow |
| Nitrogen over more than a season | **Wrong in a known direction** | Dung and urine are not returned, so a long run strips the farm |

The sheep figure is the one to watch, and it is an **approximation carried
deliberately** rather than an unknown: the size and sign are measured, a test
holds them inside 2 percentage points of the figures above, and open item 12
names the chapter that would settle the rest. It narrowed from 15% to 5% when
the walking distance was supplied - the same test that pinned the old gap
pinned the new one, so the improvement is measured rather than asserted.
Work that compares scenarios is unaffected. Work that answers "how many ewes
will this farm carry" is closer than it was and still should not be published
from this model until item 12 closes.

## Open items

These are gaps in *evidence*. What is scheduled to be *built*, and in what
order, is in [backlog.md](../backlog.md) - which also marks which of these block
work and which do not.

| # | Item | Needed by | Source to check | Status |
|---|---|---|---|---|
| 1 | Pasture growth: base and optimal temperatures, seasonal growth rates, annual DM yield by region | M2 | DairyNZ, AgResearch | open |
| 2 | Soil water: profile available water (theta_FC, theta_WP, rooting depth) by S-map soil class, drainage class | M2 | Manaaki Whenua S-map | **partly routed.** Manaaki Whenua publishes profile available water, potential rooting depth and drainage class - through S-map where it has coverage, and the older Fundamental Soil Layers as a fallback it is replacing. What is *not* published per soil, as far as located, is separate volumetric field capacity and wilting point. That may not matter: this model's bucket takes total available water through the profile, which is what PAW already is, so splitting PAW into FC and WP to satisfy the shape of the ask would be inventing two numbers to reconstruct one. Two conditions on using any of it: S-map is nominally 1:50,000 and is not a site measurement, so a paddock figure must be labelled as a mapped estimate; and its licence forbids redistribution - see item 7 |
| 3 | Intake, stocking rates, stock-unit conversions | M3 | Beef+Lamb NZ, DairyNZ | energy equations sourced (see Livestock energy); standard reference weight by breed and class still needed |
| 4 | Facial eczema: spore-count thresholds and warm-wet trigger conditions | M4 | DairyNZ | **answered for the trigger and the thresholds; the production loss is still open.** All quoted from DairyNZ. **The weather trigger**, from the *Technical Series* (February 2012): conditions favour sporulation "when overnight minimum grass temperatures stay at or above 12C over four consecutive nights, and humidity is high e.g., with drizzly rain (4-6 mm/48 h), or when soil is kept moist by irrigation". January to May are the high-risk months, and outbreaks typically show "one or two small increases over several weeks, followed by a major rapid rise" - so the shape is a slow build then a spike, not a ramp. **The thresholds**, from *Facial Eczema - Management for New Zealand dairy herds*: 20,000 spores/g pasture is where regional counts trigger monitoring your own farm (and the ceiling above which fungicide is not applied), 30,000 is where a full zinc dose starts, and a programme stops only when farm counts are "consistently at 10,000 or less for three weeks" with cooler temperatures. Counting is noisy in a way a model should not pretend away: the same guide puts the test's variability at +/-10,000 spores/g between 0 and 50,000, rising to +/-45,000 above that, and paddocks on one farm can differ by 500,000. **The epidemiology**: "Only about 10% of affected animals show clinical signs, for every clinical case there will be 10 cows with sub-clinical FE", GGT "levels of 250 IU/L indicate moderate damage", and young spores carry more toxin so fresh sporulation is worse than an old standing count. **The production response, for dairy, is sourced and linear.** Cuttance, Mason and Laven (2021), *New Zealand Veterinary Journal* 69(4):201-210, blood-tested whole herds against individual milk-solid production and report that "an increase of 100 IU/L in GGT activity was associated with a decrease of 0.011 (95% CI = 0.010-0.012) kg MS/cow/day", with a GGT threshold of 40 IU/L giving the largest herd-level association at 6.14 kg MS/day/100 cows. Two things in the same paper keep a model honest about scale: the prevalence of cows over 40 IU/L ranged from 11% to 96% **between farms**, and individual GGT ran from 3 to 6001 IU/L. **Still open, and it is the half this project needs most**: no equivalent quantified response was found for sheep. The literature describes reduced daily gain, wool loss and lowered reproduction without numbers, and every scenario shipped here runs ewes - so the dairy curve is sourced and the sheep curve is not, and must not be borrowed from it. **For sheep the liver end is sourced even though the production end is not.** Morris, Smith and Hickey (2002), *New Zealand Veterinary Journal* 50(1):14-18, dosed Romney lambs with sporidesmin and regressed liver injury score on serum GGT: LIS = -2.96 (SE 0.38) + 0.89 (SE 0.07) x ln(GGT), R2 = 0.54, p<0.001, unchanged between FE-resistant and control sire lines. The New Zealand ram-breeding tolerance test is GGT measured 21 days after a measured sporidesmin challenge, with reactors taken as GGT above 55 IU/L. Experimental oral doses in the literature are 0.5, 1.0 and 3.0 mg sporidesmin/kg liveweight, 1.0 producing severe disease uniformly. Field guidance puts 100,000 spores/g as dangerous, notes 50,000 as dangerous over long grazing and 20,000 as damaging over an extended period, and puts clinical signs 10-18 days after intake. **The conversion between the two halves is sourced, and checking it is what settles how the middle must be modelled.** Fitzgerald, Collin and Towers (1998), *Letters in Applied Microbiology* 26(1):17-21, measured both quantities in the same New Zealand pasture trial: "maximum sporidesmin levels of 26 ng g-1 grass in treated pasture and 113 ng g-1 grass in untreated pasture ... when spore numbers had reached a maximum of 80,000 spores g-1 grass in the untreated plots and 50,000 spores g-1 grass in the treated plots". That is **1.41 pg sporidesmin per spore in the untreated pasture and 0.52 in the treated** - the same count carrying 2.7 times the toxin depending on which strains are present, so this is a central value with a documented reason to be wrong, not a constant of nature. The treated plots were seeded with atoxigenic strains, which is exactly the point of that trial.

**Working the chain through is what rules out the obvious model.** At 100,000 spores/g - the level field guidance calls dangerous - the untreated ratio gives 141 ng/g grass, and a 60 kg ewe eating 1.5 kg DM/day ingests 0.21 mg/day, or 0.0035 mg/kg liveweight/day. Accumulating the 1.0 mg/kg that produces severe disease as a single experimental dose would take **283 days at that rate, against the 10-18 days field guidance gives for clinical signs**. The gap is about sixteen-fold and does not close on its own. It is explainable - "g grass" may be fresh rather than dry weight, stock grazing low eat the dead litter at the sward base where spores concentrate rather than the paddock average, and counts run far above 100,000 - but every one of those is an unsourced assumption, and stacking them to force agreement is how a model acquires a number nobody can defend.

**So the acute single-dose figures must not be used as a cumulative threshold.** The dose to GGT step has to be calibrated empirically against the field spore-count thresholds instead, and labelled as an empirical index rather than presented as toxin mass balance. The two ends stay mechanistic and cited; the middle is fitted, and says so. **One more gap, recorded rather than corrected**: the trigger is stated on *grass* minimum temperature and this model carries only air temperature. On a clear still night the sward surface radiates and sits several degrees below the screen reading, so reading the air minimum misses marginal nights rather than inventing them - conservative in the direction that matters, and left that way because no published correction between the two has been located. Also unverified: a figure of about 10^5 spores/g dry pasture as outbreak level, seen only in secondary sources, and a 25% three-week herd production drop attributed to AgResearch trials on a commercial page with no citation |
| 5 | Grass grub: degree-day development model | M4 | AgResearch literature | open |
| 6 | Nitrogen leaching: regulatory thresholds | M4 | Current Regional Council rules | open |
| 7 | Licence and **redistribution** terms for every dataset and document in use | M2-M3 | Dataset and document licences | **narrowed, and it splits in two.** Citing a source and shipping it are different permissions, and this project had only been tracking the first. LINZ data is CC BY 4.0 with attribution and commercial use allowed, and the elevation collection states its own licence, so it may travel. Open-Meteo is CC BY 4.0, with the upstream model sometimes needing its own attribution. NIWA's DataHub licences **forbid passing the data to third parties**, and S-map Online is CC BY-NC-ND 3.0 NZ - non-commercial, no derivative distribution - so neither may be committed or shipped, whatever their hashes say. A correction to what this row used to assert: **no source has been found for an OVERSEER "no-promotion clause"**; the public Knowledge Base permits distributing a report in its entirety with its disclaimers intact, and the terms that matter are whatever the specific PDF in use carries. That claim should not have been written without one |

| 8 | Farm boundaries and centroids for the three example farms | M3 | LINZ NZ Primary Parcels, via `scripts/linz-snapshot.py` | **narrowed - the fetch works, the selection does not.** A real 6 km square of NZ Primary Parcels around Lincoln has been pulled from the LINZ Data Service (`layer-50772`, 5943 parcels, EPSG:2193 easting-first as returned, SHA-256 `b1ce4586d4b114506587b86bdda15981061a80ad021d46bc9063d172df2a3e1f`). Which of those parcels are the farm is open: see item 15. All three farms still ship with generated boundaries marked `location_verified = false`. Note this is now only the boundary: every shipped farm's *ground* is measured, from LINZ's open elevation, which needed no key and no parcel selection. The parcel snapshot did earn its keep here, though - it is what placed the demonstration block on farmland instead of on Christchurch |
| 9 | Where stock choose to graze on a paddock of varying slope: utilisation by slope class | M3, task #24 | Lambert and Gillingham on stock camps and nutrient transfer | open - Gillingham et al. (1998) gives pasture *production* by slope and aspect, but not the animals' *distribution* over it. The energy cost of walking a slope is sourced (TMC Eq. 23); the preference that follows from it is not |

| 10 | Grazing selectivity: how strongly stock prefer clover over grass, and how that differs between set stocking and rotation | M3, task #24 | NZ grazing behaviour literature | open - the direction is stated by Smith and Dawson (1976), the magnitude is not |

| ~~11~~ | ~~Which age-factor variant to carry~~ | M3 | — | **decided: carry the 0.84 floor.** It is what the manual documents and what the review argues for, so both sources agree on the outcome even while disagreeing about what the reviewed code does. Applicability to adult cattle was settled first: the review works a 500 kg four-year-old through the age term |
| 12 | Whether Nicol and Brookes' published ewe maintenance includes MEgraze and Ecold, which this model's maintenance figure does not | M3 | Nicol and Brookes (2007), *Pasture and supplements for grazing animals*, NZSAP Occasional Publication No. 14 - **listed on nzsap.org**, so obtainable | **answered for grazing, still open for cold.** Adjabui et al. (2025), *Livestock Science* 299:105766, write the Nicol and Brookes (2017) maintenance as the basal term over km **plus MEgraze, MEmove and MEactivity** (their Eq. 2), which settles the half of this question that was blocking a comparison: the published figure is grazing-inclusive and this model's was not, so the two were never like for like. The corrected gap is 21%, not 15% - see the caveat table above and `tests/validation/LivestockCalibrationTest.cpp`. **Cold is not addressed**: that paper never mentions cold stress, so whether Ecold is in their figure is still unknown. On the year: the paper's own reference list carries **both** a 2007 and a 2017 entry for the same chapter, same title, same pages 151-172, so both printings exist and recording both was right |
| 13 | What the Geenty and Rattray (1987) practical grazing allowance is made of | M3 | Geenty & Rattray, "The energy requirements of grazing sheep and cattle", NZSAP Occasional Publication No. 10, pp. 39-53 | **open, and the volume is confirmed to exist.** NZSAP's site hosts Occasional Publications 11 to 16 and not 10, but *Livestock Feeding on Pasture*, Occasional Publication No. 10 (1987, about 145 pp.) is catalogued with this chapter beginning at p. 39, and NZSAP's own later papers cite it as pp. 39-53. So it is not a phantom citation and not established as unobtainable - it is simply not openly digitised anywhere located so far. The route is a library or NZSAP directly rather than more searching. Until the text is in hand the 2 MJ/day it runs above Nicol and Brookes stays an **observed difference between two frameworks** and must not become a term: no `maintenance += 2.0`, no multiplier |
| 14 | Whether OVERSEER uses lwt^0.75 or lwt^0.73 | M4 | TMC Eq. 13 against the ME review's section 12 | open - the manual says 0.75 in six places; the review reports `W^0.73 (Wheeler 2016b, Appendix 2)`. This implementation follows the manual. Another manual-against-implementation difference, like the age factor floor |
| 15 | Which LINZ parcels are LURDF | M3 | An authoritative statement of the farm's location or boundary, from Lincoln University or a survey plan | **narrowed - a map exists.** LINZ's open cadastre carries no owner, so the parcel snapshot cannot say whose land a parcel is, and forty-seven parcels of over 15 ha are centred in the fetched square. Choosing one on area would produce a boundary that looks surveyed and is invented. What has changed is that a Lincoln University dissertation has been identified carrying **"Map of the LURDF and surrounding Lincoln University farms", Figure 4-1, p. 21** (Lincoln University Research Archive). That is a route: georeference the figure by hand, overlay the LINZ parcels, record the candidate parcel ids, check the merged survey area against 79 ha, and keep the geometry LINZ's rather than drawing it. Correction to an earlier note here: the university's own page is not inaccessible - automated retrieval returns 403, but it is indexable, and it gives 79 ha total, 72 ha under irrigation, 200 stock, Weedons Road |

Item 7 also gates the repository licence and what may be redistributed with a
release, so it is worth settling before M2 rather than at M5.

Item 8 is what turns the example farms from plausible into real, and the format
is already in place to receive it: a farm switches from generated rectangles to
LINZ parcels by changing its `[boundary]` section to `kind = "geopackage"`.

## Engineering caveats

Recorded here because they affect whether a result can be trusted, not because
they are parameters.

| # | Caveat | Milestone |
|---|---|---|
| ~~E1~~ | **Closed in M2.** `std::` distributions are implementation-defined, so the same seed gave different numbers on different standard libraries. Core now implements its own — see [ADR 0007](../adr/0007-own-distributions.md). Uniform deviates are bit-identical across platforms; normal, exponential and gamma agree to within four units in the last place, limited by libm's rounding of `log`, `sqrt` and `pow`. Golden vectors are asserted on all three CI platforms. | M2 |
| E2 | The conservation suite currently exercises the ledger, not agronomy. Every process added in M2 must declare which budget lines it touches and report its flows, or the gate silently proves less than it appears to. | M2 |
| E3 | Coordinate transforms have no round-trip test yet because `gis/` has no PROJ dependency yet. The 1 mm control-point requirement lands with the first transform. | M3 |
| E4 | The synthetic weather generator draws wet days independently, so it has no wet spells and cannot produce a realistic drought. Keeping each day keyed by its date is what makes any subrange of a run reproducible, and a Markov chain would need an arbitrary anchor. Real-year replay is unaffected. See [ADR 0008](../adr/0008-weather-sources.md). | M4, with the drought scenario |
| E6 | FAO-56 says Eq. 52 (Hargreaves) should be checked against Penman-Monteith in each new region before it is trusted. Paddock uses it because CliFlo stations near a farm reliably report temperature and often nothing else; the regional check against a station that does report humidity and wind is outstanding, and belongs with the T3 validation gate. | M2, with T3 |
| E8 | **No unfertilised Canterbury reference series.** T3 validates the seasonal *distribution* against Woodlands, the only site on DairyNZ's sheet with no nitrogen fertiliser - but Woodlands is Southland (46.4 S), not Canterbury (43.5 S). The Canterbury sites are the right climate and the wrong management (154-330 kg N/ha). Neither is both. A proper target is an unfertilised Canterbury trial such as the Winchmore dryland series; until then the magnitude comparison carries this caveat. | M3 |
| E7 | **The pasture growth parameters are fixtures, not a calibration.** Radiation use efficiency, extinction coefficient, specific leaf area, senescence and residual are the right shape and order of magnitude, but the published ranges are wide and often whole-plant (RUE near 2 g DM/MJ usually includes roots; this model grows only what an animal eats). **T3 now measures the gap rather than assuming it.** Over twenty simulated years the model produces 9.8 t DM/ha/yr against 11.0 t measured at the unfertilised Woodlands site and 16.3 t at fertilised Lincoln, and its seasonal curve correlates 0.97 with the unfertilised site and 0.91 with the fertilised one. The shape is right; the magnitude is 11% low against the closest-managed site. No growth figure from this model should be quoted until the parameters themselves are sourced. | M2 shape done, magnitude M3 |
| E9 | **The terrain model had never run.** `Farm::set_slopes` had no callers anywhere in the repository and `FarmletGrid::set_terrain` had callers only in tests, so two sourced pieces of the model - the energy cost of walking a slope (TMC Eq. 23) and the radiation a slope receives (Gillingham et al.) - never fired in a single scenario the project shipped. Every farm ran flat, and no report said so. A `[terrain]` section now reaches both, and the report states which ground a run was over. **All three shipped bundles now run on measured ground**, all on one tile of 2023 LiDAR at 1 m. The two demonstration bundles were moved to get there: their grid origin was a round-number coordinate that converts to central Christchurch, so the ground under a bundle calling itself a farm would have been a city. They now sit on farmland west of Lincoln, chosen from the LINZ parcel snapshot rather than from a map - 93.8% of sampled points fall inside parcels over 10 ha, 14 parcels touch the block, one under half a hectare, and three of the holdings are Rural Sections. Their weather and soils remain placeholders; only the ground is measured. The validation suites run them with the ground taken away, because those suites link no geospatial stack and their pinned numbers were set on flat ground - see `tests/support/ShippedBundle.hpp`. Synthetic surfaces remain the thing the topography code is checked against, because their derivatives are known. A run states in its report which of the two it was over. See `tests/validation/TerrainReachesTheModelTest.cpp`, which asserts direction and reachability and never magnitude | M3 |
| ~~E10~~ | ~~**The activity term is fed by nothing.**~~ **Closed.** `Farm::conditions_on` now supplies Hkm and Vkm from the mean slope of the ground a mob is on, so TMC Eq. 24 is charged in every run. The distances are published rather than chosen: OVERSEER *Characteristics of animals* v6.3, Table 30, taken from Nicol and Brookes (2007, Appendix - activity costs), giving 0.5/1.0/1.5/2.0 km horizontal and 0/0.1/0.15/0.2 km vertical for flat, rolling, easy hill and steep hill. **The note that this was blocked on open item 9 was wrong**, and the error is worth recording: item 9 asks where stock *choose* to graze on ground of varying slope, which is a distribution; Eq. 24 needs only an average distance by topography class, which Nicol and Brookes published and OVERSEER has used for years. Item 9 stays open, for a different question. What this project did have to decide is where one topography class becomes the next, since OVERSEER takes the class as a user input per block and this model has a measured slope per cell: flat below 8 degrees, rolling below 16, easy hill below 26, steep above - Manaaki Whenua's LUC slope classes and Te Ara's description of pastoral land break at the same two degrees. **The effect is measured, not asserted**: sheep maintenance moved from 15% low against CSIRO to 5%, the golden regression moved every one of its 366 days, and a year of the grazed scenario now eats 1.4% more on flat ground. See `WalkingDistanceTest` and `SheepMaintenanceAgainstAdjabuiWithGrazingIncluded` | M3 |
| E11 | **A 25 m cell was one 1 m pixel.** The GeoTIFF reader took the single DEM pixel under each output cell's centre - 0.16% of the ground the cell stands for at 25 m over a 1 m survey - on the stated reasoning that averaging is a kind of invention and the reader's job is to report what the DEM says. That was the wrong quantity: a molehill, a wheel rut or one noisy return became the height of a quarter-hectare. It showed as a visibly crumpled farm on ground that is not, and it fed the slope, which feeds the energy cost of walking (TMC Eq. 23) and the radiation a face receives (Gillingham et al.). The reader now takes the **mean of the source pixels whose centres fall inside the cell**, which is not interpolation - bilinear would invent values between measured points and is still not done - and every number in the average is a measured pixel inside the cell it is reported for. Where a cell is finer than the DEM there is nothing to average and it falls back to the pixel under the centre. Effect on the shipped ground: the Canterbury block's range moved from 10.485-17.295 m to 10.664-17.296 m, the low end being a single-pixel outlier. `tests/gis/GeoTiffElevationTest.cpp` pins it with a one-pixel spike, because a plane cannot tell the two rules apart | Done |
| E12 | **Wind is recorded and read by nothing.** `DailyWeather` carries `wind_speed_m_per_s`, `WeatherConfig` parses a monthly mean for it, both weather sources produce it and `is_valid()` checks it - and no file under `core/src` outside the weather sources ever reads it. Not `Pasture`, not `SoilWater`, not `AnimalEnergy`, not `Farm`. It is the same shape of gap as E9 and E10: an input taken seriously all the way to the point where it would matter. Two places it belongs, both needing a decision rather than a patch: evaporative demand, where this model uses Hargreaves - radiation and temperature range, no wind - rather than FAO-56 Penman-Monteith, which does use wind at 2 m; and cold stress, which is open item 12's territory. Note the shipped Open-Meteo series makes this worse before it makes it better: it supplies a daily **maximum** at **10 m**, and both the statistic and the height are wrong for either equation. Recorded rather than wired, deliberately, because guessing which equation to feed it to is how a model acquires a number nobody can defend | M3 |
| E13 | **The farmer's target gain reached only the chequebook, and now that it reaches the animals there is no ceiling on them.** Demand was computed from `AnimalState::liveweight_change_kg_per_day`, which holds what the mob did YESTERDAY, so a well fed mob ate to maintenance, changed by nothing, and was offered maintenance again: a stable fixed point that left a ewe on exactly her opening weight for 366 days while `target_liveweight_gain_kg_per_day` sat in the panel doing nothing. It only ever entered the decision about how much feed to BUY, which on a farm with grass to spare is no decision at all. Intent is now a separate field, `Mob::target_gain_kg_per_day`, set by the farmer each day; asking for 0.1 kg/day over a year now returns 36.5 kg against a theoretical 36.6. **The remaining gap is that essentially nothing damps it.** TMC Eq. 44-46 make gain dearer as maturity rises - the ewe above went from 0.85 to 1.4 of her standard reference weight - but nothing forbids exceeding mature weight, so asking for 0.2 kg/day for a year yields a 123 kg ewe and the model does not object. A ceiling needs a source for where it sits and what happens at it, which is why one has not been invented here | M3 |
| E5 | The CliFlo column mappings in `scripts/cliflo-snapshot.py` are guesses until checked against a real export of each datatype. The script prints the headings it found and accepts `--column HEADING=field`, so a wrong guess is visible rather than silent. | M2, on first real download |
