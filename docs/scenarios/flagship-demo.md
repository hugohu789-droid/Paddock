# The flagship demonstration: irrigation off, irrigation on

One farm, one recorded year, run twice. The only difference between the two runs
is whether the farm irrigates.

```bash
paddock dashboard data/scenarios/demo-irrigation-off data/scenarios/demo-irrigation-on
```

That prints what the two were set up to do differently before it prints a single
result:

```
  What changed
  ============
    demo_irrigation_off  ->  demo_irrigation_on

    Irrigation
      irrigation           off  ->  on
      trigger              -  ->  40% available water
      refill target        -  ->  85% available water
      most at once         -  ->  25 mm
      reaches the ground   -  ->  100%

    One category differs, so what the results do is attributable to it.
    Unchanged: Run, Farm, Ground, Weather, Soil, Pasture, Stock, Grazing policy.
```

Then the few outcomes that answer the question, before the twenty-seven that
answer every question:

```
  What changed in the paddock
  ===========================
    demo_irrigation_off  ->  demo_irrigation_on

    Irrigation response   1.00 ->  1.79 x rain-fed    +0.79    +79%  benchmarked
    Pasture grown         6613 -> 11846 kg DM/ha      +5233    +79%  calibrated
    Lowest pasture cover   713 ->   976 kg DM/ha       +263    +37%  exploratory
    Growth-limited days    233 ->    15 days           -218    -94%  conserved
    Dry matter per mm     12.5 ->  14.0 kg DM/ha/mm    +1.5    +12%  calibrated
    Drainage below roots   109 ->   152 mm              +43    +39%  conserved
    Irrigation applied       0 ->   375 mm             +375  from 0  conserved

  What these are worth
    calibrated   reproduces published observations, having been fitted to them
    conserved    no external benchmark; read off budgets the tests close to 1e-9
```

**Nothing on that page is benchmarked, and that is the honest result.** The
strongest label any outcome here earns is *calibrated*: pasture grown is fitted
to Winchmore's dryland water use efficiency and then compared against
Winchmore's own band, which is checking its own arithmetic rather than being
tested against the world. `Confidence` is a rendering of the `Provenance` each
indicator already carried, so a number cannot be promoted by editing the page -
and a fitted value never reaches "benchmarked" whatever band it has.

The page also names what it left off and why: bought feed (the model's
purchases are unbounded, E71/E77), liveweight and stocking rate (the least
validated part of the model), money (the water is free in the model), and
nitrate leached (placeholder coefficients). A summary that quietly dropped the
feed bill would have chosen its metrics to flatter.

**The last two lines of the "What changed" block are the ones that matter.** A reader handed two columns of
indicators will attribute the gap between them to whatever they were told the
comparison was about; this is the model saying what actually differs, read off
the resolved bundles and never off the results. Point it at two unrelated farms
and it says "7 categories differ, not one" and names them.

Either half on its own is `paddock dashboard data/scenarios/demo-irrigation-off`.

**The farm is Lincoln**, the same 80 ha Canterbury sheep block this project
validates against, on real Selwyn-plains weather for 2023-24 (Open-Meteo /
ERA5). The soil, the sward, the stock, the grazing calendar, the dates and the
seed are shared by reference with `lincoln-lurdf`, so there is one weather file,
one soil and one sward on disk and no way for the demo to drift from the farm
the Winchmore comparison is run on.

---

## What differs, and how you can check

The irrigated half adds one section and changes nothing else:

```toml
[irrigation]
enabled = true
trigger_depletion_fraction = 0.6
target_depletion_fraction = 0.15
maximum_application_mm = 25.0
```

The trigger is **FAO-56 Table 22's depletion fraction p = 0.6 for grazed
pasture**: the share of the profile's available water a pasture can lose before
growth is held back. Watering there is watering exactly when the sward would
otherwise start to suffer. It is not a rule chosen to make the difference look
large, and the refill target and the 25 mm cap are `core::IrrigationPolicy`'s own
documented defaults.

**No application efficiency is claimed.** A real spray loses water to drift and
evaporation; this project has no New Zealand figure for that, so the system
models no loss and says so rather than putting an invented penalty into a
comparison nobody could cite.

That the two halves are otherwise identical is asserted, not promised.
`tests/validation/FlagshipDemoTest.cpp` checks it two ways:

- **semantically** - both bundles are loaded and every setting that reaches the
  model is compared, including the SHA-256 of every input file, which compares
  every parameter inside the soil, sward and species definitions at once;
- **as text** - the raw manifests are compared with comments, the two names and
  the irrigation section stripped, which catches a setting added to one file
  that the semantic comparison has never been taught about.

**The view and the test are not the same comparison, deliberately.** The view is
written to be read - it groups settings into ten categories and puts them in
farm language - and the test is written to be exhaustive, down to fields no
reader would want listed. So the test is the proof and the view is the summary,
and `ScenarioInputsTest` checks that the summary reaches the same verdict on
this pair: Irrigation changed, and nothing else did.

The same suite checks that both runs are bit-identical when repeated, that all
three budgets still close to 1e-9, that no day of either run carries a NaN or an
infinity, that both render a report, that the irrigated half streams a finite
map for every day, and that both bundles load from what is committed with their
hashes intact.

---

## What the year came to

| | Rain-fed | Irrigated |
|---|---|---|
| Pasture grown | 6,613 kg DM/ha | 11,846 kg DM/ha |
| Days growth was held back by dry soil | 233 | 15 |
| Water applied | none | 375 mm in 18 events (300 ML over 80 ha) |
| Evapotranspiration | 529 mm | 844 mm |
| Drainage past the root zone | 109 mm | 152 mm |
| Lowest cover of the year | 713 kg DM/ha | 976 kg DM/ha |
| Bought feed | 4,798 kg DM | 0 kg DM |
| Nitrate leached | 2.1 kg N/ha | 14.7 kg N/ha |

Rainfall was 652 mm in both.

**The headline is water and grass, and it is deliberately not animal
production.** Liveweight, stocking rate and the money that follows from them are
the least validated part of this model - `docs/validation/verify.md` records the
open items - so they are reported by the dashboard with their provenance marked
and they are not what this demonstration is for.

Three things are worth saying about the table:

1. **The response is not flattering to the model.** The extra 375 mm bought
   5,233 kg DM/ha, a marginal 14.0 kg DM/ha per mm applied. Martin et al. (2006)
   put irrigated Canterbury ryegrass and clover near 20 kg DM/ha/mm on total
   water use. The model's response to irrigation is at the modest end of the
   published range, not the generous one.

2. **The irrigated run reads "outside" the dashboard's production band, and
   that is correct.** Those bands are Winchmore's *dryland* treatment. An
   irrigated farm sitting inside a dryland band would be the thing to worry
   about.

3. **The nitrate figure is the interesting one and the weakest one.** Irrigation
   takes this farm from 2.1 to 14.7 kg N/ha, up against Environment
   Canterbury's Selwyn Waihora limit of 15. That is a real trade-off and it is
   the kind of question this model exists to ask - but the leaching coefficients
   are marked `placeholder`, so it is a shape to discuss, not a number to file.

---

## Where to look: 22 January 2024, Paddock 29

**The date.** 22 January 2024 is the widest the two runs ever get. Farm mean
cover is 918 kg DM/ha rain-fed against 2,214 irrigated, and the profile is 12%
full against 73%. The whole of January 2024 is within 2% of that gap, so the
demonstration does not depend on catching one particular day.

**The paddock.** Paddock 29 - index 28, the north-western corner of the block.
The farm carries a west-to-east soil gradient, 55 mm of available water at the
western fence rising to 85 at the eastern one, and Paddock 29 sits at the dry
end with 57 mm. It is the paddock irrigation helps most: over the whole year its
cover runs 490 kg DM/ha above its rain-fed self, the largest margin of the 35,
and the margin falls monotonically across each row as the soil gets deeper.

On 22 January 2024 that paddock reads:

| | Rain-fed | Irrigated |
|---|---|---|
| Cover | 891 kg DM/ha | 2,332 kg DM/ha |
| Profile full | 12% | 68% |

A factor of 2.6 in standing feed, on the same day, on the same soil, under the
same weather, with the same stock. In the map view it is the difference between
a brown corner and a green one.

**Why the west end and not a hillside.** The paddocks on this farm are the grid
cut into 2.28 ha rectangles, not a survey of Lincoln's actual fences - see
`ScenarioBundle::paddock_caveat`. Paddock 29 is a real place in the model and
its number is not a real farmer's name for it.

---

## What this demonstration does not show

- **Whether irrigation pays.** The money panel prices this year, but the water
  itself is free in the model: there is no consent, no pumping cost and no
  capital. A closing balance twice the rain-fed one is not a return on
  investment.
- **When the feed arrives.** This is the one thing on this page that is not
  defensible, and it is worth being exact about. See "What may and may not be
  claimed" below.
- **Anything about the season's shape.** The largest known error in this model
  is that it grows too little in spring and too much in summer against measured
  Winchmore months (E84 in `docs/validation/verify.md`). A summer-time
  irrigation demonstration sits squarely in the season the model overstates, so
  read the January gap as directionally right and quantitatively soft.
- **The real ground.** These runs are flat unless you fetch the LiDAR with
  `paddock ground fetch`. Slope and aspect change both the radiation a paddock
  receives and the energy the stock spend walking it.

---

## What may and may not be claimed

The residual seasonal error recorded in E84 was reviewed against this demo
(E88). The short answer is that it does not block the demonstration, because
what the demonstration claims is an annual ratio, and that ratio is measurable.

### Defensible

**The annual response, and the ratio behind it.** `data/calibration/winchmore-annual-production.csv`
carries three measured irrigated treatments beside the dryland column, 25 years
each. The demo waters at FAO-56's p = 0.6 - as soon as the sward would be held
back - which is a wet regime, so the 15% and 20% soil-moisture treatments are
its company: 50 treatment-years, median response ratio 1.90, mean 1.83, p10 to
p90 of 1.23 to 2.46.

**The model's ratio is 11,846 / 6,613 = 1.79 - the 48th percentile of those
fifty.** The demo year's 652 mm of rain is 13% below the trial's 745 mm mean, so
a ratio at about the median is what a drier-than-average year should give.

**This is gated, not just observed.** `tests/validation/IrrigationResponseTest.cpp`
runs both halves through the same priced path the demonstration uses, takes the
production figures off the dashboards the customer reads, and requires the ratio
to sit inside 1.25 to 2.40 - the p10 to p90 of the measurement, rounded inward.
The distribution is read out of the CSV rather than transcribed, and a second
test re-reads it and fails if the band drifts from the percentiles it came from.
The response per mm applied is gated too, as a ceiling: 14.2 against Martin et
al. (2006)'s ~20, because a model converting water faster than the published
figure is over-responding whatever its ratio says (E90).

Also defensible, on the same footing as the rest of the model: water applied
(375 mm is an ordinary Canterbury season), drainage, days growth was held back,
and dry matter per mm - the last at 14.2 kg DM/ha per mm applied, below Martin
et al. (2006)'s ~20 for irrigated Canterbury pasture, which is the conservative
direction.

### Exploratory - shown, not quotable

**Lowest pasture cover.** Downgraded in E89. It is fitted to nothing: no
measured minimum-cover series has been found, its band is the farm's own
management floor rather than an observation, and a third to a half of what it
counts is dead material whose turnover rate has no New Zealand measurement
behind it (E26).

**Anything about when the feed arrives**, including the 22 January illustration
and Paddock 29. Those are shown because a person needs somewhere to look on a
map, and they are illustrations of a mechanism rather than claims about a date.

### Why timing is the weak half

The rain-fed arm grows `10.4 / 53.2 / 26.7 / 9.6` across winter, spring, summer
and autumn against Winchmore's measured `10.0 / 54.7 / 18.0 / 17.3`. In this
particular year spring is nearly right and the error is **summer over by 1.49x,
autumn under by 7.7 points** - a different shape from E84's decade mean, and one
year rather than a trend.

**68% of the irrigation response falls in summer** - 3,679 of 5,383 kg DM/ha -
which is exactly the season the model overstates. Summer's water-stress factor
goes from 0.381 rain-fed to 0.999 irrigated, and E67's finding is that this
model answers summer water in a way a real post-reproductive sward cannot,
because it has no tillers and no phenology. So the seasonal bias does not cancel
in the difference between the two runs; it is concentrated in it, and the annual
total is right partly through compensating errors.

That is why the annual ratio may be quoted and the monthly shape may not.

### Phenology is deferred, not skipped

Building phenology would change the seasonal path. There is no evidence it would
move the annual ratio off the measured median it currently sits on, and the
demonstration's claim is the annual ratio. It stays on the roadmap as the
largest known modelling gap (E84) and it does not gate this demo.

### What should gate the pasture half of the demo

Proposed bounds, derived from the measured series rather than chosen:

| Gate | Bound | From |
|---|---|---|
| Rain-fed annual production | 3,900 to 9,850 kg DM/ha | Winchmore's measured dryland range over 25 years |
| Irrigation response ratio | 1.25 to 2.40 | p10 to p90 of the 15% and 20% treatments, 50 treatment-years |
| Response per mm applied | at most 20 kg DM/ha/mm | Martin et al. (2006) for irrigated Canterbury pasture - a ceiling, since exceeding the published figure means over-responding |
| Budgets and determinism | already gated | `FlagshipDemoTest` |

All four are implemented in `IrrigationResponseTest` (E90).

The seasonal split is deliberately **not** gated on accuracy - it is a known
open error and a gate would either fail on day one or be set so wide it asserted
nothing. It carries a **regression guard** instead, which is a different thing
and is labelled as one: the rain-fed summer share sits at 26.4% today against a
32% ceiling and a 15% floor. Passing it is not evidence of anything. Failing it
means something upstream moved the season and whoever moved it should say so -
most obviously if somebody "fixed" the season by tuning rather than by
phenology.

---

## Files

```
data/scenarios/demo-irrigation-off/scenario.toml   the rain-fed half
data/scenarios/demo-irrigation-on/scenario.toml    the irrigated half
tests/validation/FlagshipDemoTest.cpp              the checks described above
```

Both manifests are generated from one body so that the settings they share
cannot drift apart in a hand edit, and the tests fail if they ever do.
