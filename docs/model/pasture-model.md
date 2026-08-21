# Pasture model

How grass is grown on one cell, one day at a time.

Code: `core/include/paddock/core/Pasture.hpp`, `core/src/Pasture.cpp`.
Parameters, their sources and their calibration status: [verification
tracker](../validation/verify.md).

## What it answers

How much dry matter this cell grew today, what cover it now carries, and how
much of that is clover.

## Structure

Growth is light interception, converted to dry matter, and then scaled by the
things that hold it back:

```
leaf area index  =  green dry matter × specific leaf area
intercepted PAR  =  incoming PAR × (1 − e^(−k × LAI))          Beer's law
potential growth =  intercepted PAR × radiation use efficiency
actual growth    =  potential × temperature response × water growth factor
```

Senescence then removes a share of the green dry matter that stands above the
residual, and the residual itself never senesces.

**The residual is not a detail.** It is the crown, the stubble and the reserves
a plant regrows from. Without it, a sward grazed or dried to nothing would carry
no leaf area, intercept no light and never grow again — zero would be an
absorbing state, which is exactly what a real pasture is not.

## Species as data

Not one parameter has a default in code. Ryegrass and white clover differ only
in the numbers their TOML definitions give them, which is what makes adding
cocksfoot or lucerne a data change:

| Parameter | Unit | Role |
|---|---|---|
| Specific leaf area | m²/kg | Leaf area per kilogram of dry matter |
| Extinction coefficient | — | Beer's law, how fast the canopy shades itself |
| Radiation use efficiency | g/MJ | Dry matter per MJ of intercepted PAR |
| Base, optimal and maximum temperature | °C | The growth response curve |
| Senescence rate | /day | Share of green dry matter above the residual lost daily |
| Residual | kg DM/ha | Dry matter that does not senesce |
| Nitrogen content | kg N/kg DM | Nitrogen in green tissue |
| Nitrogen fixed | kg N/t DM | Zero for a grass; positive for a legume |

**Radiation use efficiency needs care when reading the literature.** Published
figures around 2 g/MJ for perennial ryegrass are commonly whole-plant, roots
included, while this model grows only what an animal can eat.

## The mixed sward

Ryegrass and white clover are grown as two populations on the same ground,
sharing light through the same canopy. The clover fixes nitrogen in proportion
to the dry matter it grows, and that nitrogen enters the soil mineral pool the
grass draws on.

Legume fraction is therefore an output, not a setting: it moves over seasons as
the two species' responses to temperature and water diverge.

## What holds growth back

- **Temperature**: a response curve between base, optimal and maximum
- **Water**: the FAO-56 stress coefficient from [the soil water
  model](soil-water-model.md), 1.0 when the soil holds everything the plant asks
  for
- **Radiation on this cell**: a slope's own share, so an aspect grows at its own
  rate

## Conservation

Growth, senescence and grazing removal are all declared to the budget ledger.
Dry matter and nitrogen close to 1e-9 over a closed year, checked on every
commit.

## Calibration status

Honest summary — the detail is in the [verification
tracker](../validation/verify.md):

- The **seasonal shape** correlates well with DairyNZ's measured site
- The **magnitude** does not: the modelled year runs low against the measured
  average, and the growth parameters are placeholders until they are sourced
- Comparisons between two scenarios carry more weight than absolute yields,
  because both arms use the same parameters — but a wrong parameter can still
  bias how big the difference comes out, since growth responds non-linearly and
  the two arms sit at different points on the curve

Do not quote an absolute yield from this model.

## Assumptions and limitations

- Two species, one canopy, no botanical composition beyond grass and clover
- No nitrogen fertiliser response curve
- No pests, diseases or treading damage
- Senescence is a fixed daily share, not a function of age or temperature
- Nothing regrows from seed; the residual is the only regrowth pathway

## Tests

- `tests/unit/PastureTest.cpp` — growth response, senescence, the residual floor
- `tests/conservation/PastureBalanceTest.cpp` — dry matter and nitrogen close
- `tests/validation/SeasonalGrowthTest.cpp` — the season's shape against
  measured data, with a comparison plot uploaded by CI
