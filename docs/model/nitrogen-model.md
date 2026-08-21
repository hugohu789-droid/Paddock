# Nitrogen model

What Paddock does with nitrogen today, and — more importantly — what it does
not.

Code: the nitrogen pool and fixation live in `core/src/Pasture.cpp`; the
bookkeeping in `core/src/BudgetLedger.cpp`.
Evidence: [verification tracker](../validation/verify.md).

## Read this first

**Nitrogen over more than a season is wrong in a known direction.** Dung and
urine are not returned to the soil, so a grazed run strips the farm: every
kilogram of nitrogen the stock eat leaves the system and never comes back, where
on a real farm most of it lands on the paddock within a day or two.

The bookkeeping is sound — nitrogen balances to 1e-9 — but a balance only says
nothing was lost by accident. It does not say the pathways are complete, and
here they are not.

## What is modelled

**A soil mineral nitrogen pool** per cell, drawn on by growth and topped up by
fixation.

**Fixation by white clover**, in proportion to the dry matter the legume grows,
at a rate its species definition gives in kg N per tonne of dry matter. Grasses
fix nothing.

**Nitrogen in green tissue**, in kg N per kg of dry matter, so what grows and
what is eaten each carry their nitrogen with them.

**Every flux declared**: growth takes nitrogen from the pool, fixation adds to
it, senescence and grazing move it out of the sward. The ledger closes over a
closed year, on every commit.

## What is not modelled

- **Return of dung and urine.** The largest pathway on a grazed farm, and it is
  absent
- **Nitrate leaching.** Nothing follows nitrogen down the profile, so the model
  cannot answer a regulatory question about loss to water
- **Fertiliser response.** There is no applied-nitrogen response curve
- **Volatilisation, denitrification, immobilisation.** None of the gaseous or
  microbial pathways
- **Mineralisation of organic matter.** The pool is not fed by the soil itself

## What it may be used for

| Question | Standing |
|---|---|
| Does the bookkeeping balance? | Sound — that is what the ledger asserts |
| How much nitrogen did the clover fix this year? | Reasonable within the model's own terms, unvalidated against measured fixation |
| Is this farm's nitrogen sustainable over years? | **No** — the missing return makes a long run strip the farm |
| How much nitrate leaves this farm? | **Not modelled at all** |

## Where it is going

Returning dung and urine is the first of the nitrogen work, because until it is
there every longer-horizon nitrogen question has a known wrong answer. Leaching
follows it, and only then does a compliance-style report make sense.

## Tests

- `tests/conservation/ClosedSystemTest.cpp` — nitrogen closes to 1e-9 over 365
  days
- `tests/conservation/PastureBalanceTest.cpp` — fixation and uptake balance
  against the pool
