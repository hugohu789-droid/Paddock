# Nitrogen model

What Paddock does with nitrogen, what it rests on, and what it still cannot say.

Code: the nitrogen pool, fixation, excreta return and leaching live in
`core/src/Pasture.cpp`; the bookkeeping in `core/src/BudgetLedger.cpp`; the
compliance report in `config/src/NitrogenReport.cpp`.
Evidence: [verification tracker](../validation/verify.md).

## Read this first

**Every number here is a model output, and the two that matter most rest on a
placeholder each.** Nitrogen balances to 1e-9, but a balance only says nothing
was lost by accident — it does not say the pathways are right. The patch uptake
that decides how much of a urine patch leaches is a placeholder, and the share
of loss that happens between patches is fitted rather than measured. A leaching
figure from this model is the right order of magnitude and is not a measurement.

This section used to say that dung and urine were not returned at all and that
leaching was not modelled. Both are now modelled; the paragraph above is what
replaced them, and it is a weaker claim than "not modelled" only in the sense
that there is now something to be wrong about.

## What is modelled

**A soil mineral nitrogen pool** per cell, drawn on by growth and topped up by
fixation.

**Clover fixation**, as a function of the legume fraction and of growth.

**What comes back out of the animal.** Nitrogen eaten does not leave the system.
It is partitioned each day between:

| Path | Rate | Standing |
|---|---|---|
| Dung | 0.00835 kg N per kg DM eaten | TMC Eq. 137, Barrow and Lambourne (1962) |
| Wool | 0.0022 kg N/head/day | **placeholder** — rests on an unsourced 5 kg fleece |
| Liveweight gain | 0.029 kg N per kg gain | **placeholder** — 18% protein, 16% N |
| Urine | the remainder | derived, as what the other three do not take |

**Urine patches, because the paddock average is not what leaches.** A patch
lands at up to 1,000 kg N/ha — OVERSEER's stated ceiling — and the pasture on it
takes up only a few hundred before the rest is surplus. Spreading the same
nitrogen evenly over the paddock would leach almost none of it, which is exactly
why patches are modelled separately.

**Leaching in two pools**, both booked to the ledger by name:

- `nitrate_leaching` — surplus under the patch, carried by the day's drainage
- `nitrate_leaching_inter_patch` — the rest of the paddock, at a fitted
  fraction of 0.012, calibrated so the inter-patch share sits inside OVERSEER's
  stated "under 15%" of total loss

**A compliance report** against a rule the caller supplies. `paddock nitrogen
<bundle> <regulation.toml>` quotes the rule, its exclusions and the year's loss.

## What is not modelled

- **Fertiliser response.** There is no applied-nitrogen response curve, and this
  farm applies none. That absence is also why annual production sits about 20%
  above a *fertilised* trial — see verify.md, E40
- **Volatilisation, denitrification, immobilisation.** None of the gaseous or
  microbial pathways
- **Mineralisation of organic matter.** The pool is not fed by the soil itself
- **Preferential flow.** `drainage_mixing_fraction` is 1.0, meaning perfect
  mixing; a stony Canterbury soil bypasses the matrix and would leach
  differently

## What it may be used for

| Question | Standing |
|---|---|
| Does the bookkeeping balance? | Sound — that is what the ledger asserts |
| How much nitrogen did the clover fix this year? | Reasonable within the model's own terms, unvalidated against measured fixation |
| Is this farm's nitrogen sustainable over years? | Better than it was — the return path exists. Still unvalidated against a measured farm |
| How much nitrate leaves this farm? | **An order of magnitude, not a measurement.** Two placeholders and one fitted parameter stand between this figure and anything published |
| Would this farm comply with a regional rule? | The comparison is honest and the input is not. Read it as "is this farm near the line", never as a consent number |
