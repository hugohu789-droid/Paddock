# Soil water model

The water balance of one cell of ground, stepped once a day.

Code: `core/include/paddock/core/SoilWater.hpp`, `core/src/SoilWater.cpp`.
Evidence and calibration status: [verification
tracker](../validation/verify.md).

## What it answers

How much water is in the root zone this evening, how much left as drainage or
evapotranspiration, and — the number the rest of the model actually consumes —
how much today's growth was held back by dryness.

## State

| Variable | Unit | Meaning |
|---|---|---|
| Soil water | mm | Water held in the root zone |
| Total available water | mm | What the root zone can hold between field capacity and wilting point |
| Depletion | mm | Total available water − soil water |
| Available fraction | 0–1 | Soil water ÷ total available water |

Available water is the fraction the panels and the irrigation policy speak in;
depletion is what the equations use. They are the same quantity from opposite
ends, and the conversion happens in one place so the two cannot drift.

## Inputs each day

- Rainfall, mm
- Maximum and minimum temperature, °C
- The radiation ratio for this cell's slope and aspect, 1.0 on level ground
- Irrigation applied to this cell, mm — decided before the balance runs

## The balance

```
soil water(t) = soil water(t−1) + rain + irrigation − evapotranspiration − drainage
```

Drainage is what will not fit: anything above field capacity leaves the same
day. Nothing is carried as surface water, and there is no run-on or run-off
between cells — a limitation, and a deliberate one for now.

## Evapotranspiration

FAO-56 Eq. 52, the Hargreaves temperature method:

```
ETo = 0.0023 × (Tmean + 17.8) × (Tmax − Tmin)^0.5 × Ra
```

with extraterrestrial radiation `Ra` computed from the date and latitude
(FAO-56 Eq. 21) and scaled by this cell's radiation ratio, so a north-facing
slope evaporates at its own rate rather than at level ground's.

**Why Hargreaves and not Penman-Monteith.** FAO-56 recommends Penman-Monteith
where humidity and wind are measured. Hargreaves is its documented fallback when
only temperature is reliable, which is the common case for the CliFlo stations a
New Zealand farm is near. FAO-56 also says Eq. 52 should be checked against
Penman-Monteith in each new region — that check is an open item in the
verification tracker.

## Water stress

FAO-56 Eq. 84. The coefficient is **one while the root zone still holds readily
available water**, and falls linearly to zero at wilting point.

The interface calls it the *water growth factor* for exactly that reason: a
number that is 1.00 on a perfect day reads as maximum stress if it is labelled
"stress".

The readily available fraction is adjusted for evaporative demand following the
note under FAO-56 Table 22: a crop stands more depletion on a cool day than on a
hot one.

## What consumes it

The stress coefficient multiplies pasture growth for the same cell on the same
day — see [the pasture model](pasture-model.md). Nothing else reads it, and
nothing writes it.

## Conservation

Every flux is declared to the budget ledger: rain and irrigation in,
evapotranspiration and drainage out. A closed run must balance to 1e-9 over 365
days, checked on every commit by the conservation suite.

## Assumptions and limitations

- A single-layer bucket. No layered profile, no water table, no capillary rise
- No lateral flow: cells do not shed water onto one another, so a hollow does
  not stay wetter than the shoulder above it
- Drainage is instantaneous above field capacity
- Soil parameters in the shipped example bundles are marked PLACEHOLDER where
  they have not been sourced from S-map
- Hargreaves has not been checked against Penman-Monteith for New Zealand
  conditions in this project

## Tests

- `tests/unit/SoilWaterTest.cpp` — the balance step, stress at known depletions
- `tests/conservation/WaterBalanceTest.cpp` — water closes to 1e-9 over a year
- `tests/unit/SolarSlopeTest.cpp`, `tests/unit/SlopeRadiationTest.cpp` — the
  radiation ratio a slope receives
