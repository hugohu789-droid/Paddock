# Grazing model

What the stock eat, what it costs them, and how the farmer decides where they
stand.

Code: `core/include/paddock/core/Grazing.hpp`, `core/include/paddock/core/AnimalEnergy.hpp`,
`core/include/paddock/core/Farmer.hpp` and their sources.
The decision as a diagram is in the [system
architecture](../architecture/system-architecture.md#flow-4-the-grazing-decision).

## What it answers

How much a mob needs today, how much of that the ground can offer, what the
shortfall costs in liveweight or in bought feed, and which paddock the mob
stands on tomorrow.

## Demand

A mob's daily energy requirement is built from maintenance, the cost of grazing
and walking, and the energy value of the liveweight change the farmer is aiming
at. Intake follows from the requirement and the energy density of the diet.

Where a paddock sits matters: walking a slope costs energy, so the same mob on
the same feed needs more of it on steeper ground. That is one of the two places
terrain enters the model.

## Offer

A cell offers what stands above the sward's residual. A mob grazes every cell of
every paddock it occupies, in proportion to what each cell offers, so the ground
it eats down is the ground that had the most on it.

## The farmer's rule

**There is no per-paddock "grazeable" test in this model.** The rule is about
the mob:

1. A mob moves when it **went short yesterday**, or when it has been on its
   paddock for the **maximum graze days**
2. It goes to the **free paddock that has rested longest** — occupied paddocks
   are out, because a paddock carries at most one mob here
3. If every other paddock is occupied, the mob stays and the grazing lengthens,
   recorded as such
4. The **minimum spell is what the farmer aims at, not a gate**: a mob moves
   onto ground that has had less rest, and the run records the short spell

Smith and Dawson's rule — do not graze a pasture for more than three days with
the major grazing mob — is a maximum rather than a period to serve out, which is
why a mob that has emptied its paddock moves early.

The two ways a farm with too few paddocks shows the strain are both counted:
**short spells** (moving onto ground that has not rested) and **extended
grazings** (nowhere to move to). Together they are what the literature calls the
shuffle, and they are measurable here rather than hidden.

## The cover floor

The farmer will not graze the sward below a cover floor. At the floor there is a
choice, and it is a setting:

- **Buy the whole demand** — asks the pasture for nothing, so cover climbs away
  from the floor
- **Graze to the floor and buy the rest** — buys less feed and leaves less grass

If the farmer may not buy feed, the stock go short and the run records that
instead. That is the counterfactual the bought-feed figure is measured against.

## The system choice

A managed run chooses between rotational grazing and set stocking each day, from
the farm's mean cover against the policy's thresholds — or follows the scenario's
own calendar if that is what the policy asks for. Set stocking means a mob holds
every paddock rather than being confined to one.

## Conservation

Every kilogram eaten leaves the sward and is declared to the ledger. Bought feed
enters as an inflow with its date and its reason. Dry matter closes to 1e-9 over
a grazed year.

## Calibration status

From the [verification tracker](../validation/verify.md), and it matters:

- **Cattle maintenance** reproduces DairyNZ to about 2% across 300–600 kg
- **Sheep maintenance is low by about 5%** against CSIRO (2007). The basal term
  agrees to 0.3% — it is the same equation — and the rest is the cost of
  grazing, walking and activity, which this model now charges about seven tenths
  of. It charged a tenth until TMC Eq. 24 was given a walking distance; the
  distances are OVERSEER's Table 30, from Nicol and Brookes (2007), keyed to
  slope by the LUC classes
- **Sheep carrying capacity is therefore overstated by about 5%**
- **Absolute liveweight gain is not quotable**: standard reference weight is
  unverified and it drives the energy value of gain
- Comparisons between grazing systems are sound: both arms carry the same
  parameters

## Assumptions and limitations

- **Utilisation runs about 26 to 35% where a New Zealand sheep farm removes 65
  to 80%.** The stock eat roughly a third of what grows and the rest dies
  standing. The stocking rate is not the cause — 7.0 SU/ha against Beef +
  Lamb's Class 6 average of 7.74 — so the gap is in intake, in what a mob will
  take off a cell, or in how the rotation offers it. This is the largest open
  question in the model and no result that depends on how much was eaten should
  be quoted without it
- One mob per paddock
- No selective grazing within a cell — a mob eats the offer in proportion
- No treading damage, no pugging, no camping behaviour
- Dairy lactation is not modelled; **ewe** pregnancy and lactation are, on
  OVERSEER's equation set, so a "dairy cow" here is still a dry cow while a ewe
  in milk eats like one

## Tests

- `tests/unit/GrazingTest.cpp`, `tests/unit/FarmerTest.cpp` — intake, the
  farmer's choice, short spells and extended grazings
- `tests/unit/AnimalEnergyTest.cpp`, `tests/validation/LivestockCalibrationTest.cpp`
  — requirements against published tables, with the sheep gap held inside 16%
- `tests/validation/GrazingSystemComparisonTest.cpp` — rotation against set
  stocking on one farm
- `tests/conservation/FarmBalanceTest.cpp` — a grazed year closes
