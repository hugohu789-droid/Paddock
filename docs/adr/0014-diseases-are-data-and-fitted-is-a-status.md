# ADR 0014 — Diseases are data, `fitted` is a provenance status, and the dose is measured in toxin

- **Status:** accepted
- **Date:** 2026-08-31
- **Milestone:** M4

## Context

M4 asks for a "data-driven disease/pest framework" and six scenarios on top of
it. Facial eczema is the first, and building it forced four decisions that the
rest of M4 will inherit.

The chain a facial eczema model has to represent runs weather → sporulation →
spore count → toxin on the pasture → liver damage → lost production. Its two
ends are published in New Zealand sources and its middle is not, and the
arithmetic says so rather than the absence of a search saying so: at the spore
count field guidance calls dangerous, a 60 kg ewe accumulates the single
experimental dose that produces severe disease in 283 days, against the 10 to 18
days guidance gives for clinical signs. Sixteen-fold. Every explanation for that
gap — grass weight measured fresh rather than dry, stock grazing the litter at
the base of the sward rather than the paddock average, counts well above the
quoted threshold — is itself unsourced.

## Decision

### Diseases are data, and a loader is what makes that true

`CLAUDE.md` already says diseases are data rather than classes. The lesson from
building the first one is that the rule needs a loader to mean anything: for a
few hours `data/diseases/facial-eczema.toml` carried the numbers and their
citations while the model was built from a hand-written struct in the tests, and
**the two had already drifted before anyone looked** — the file said a
sporulation rate of 3.0 and the suite asserted 1.9, with every test green.

`config/DiseaseConfig` loads the file, the suites that check the shipped disease
read it rather than copying it, and a parameter that cites a source `[sources]`
does not define is rejected at load. A TOML file nothing loads is a comment.

### `fitted` is a fifth provenance status

`Provenance` had `direct`, `derived`, `verify` and `placeholder`. The step from
toxin load to serum GGT is none of them. It is not `placeholder` — it is
calibrated so the published field thresholds reproduce, which is more than no
evidence. It is not `derived` — that claims arithmetic on stated figures, and
the arithmetic above is exactly what rules that out.

`fitted` counts as evidence, so a fitted value must name what it was fitted to.
A number calibrated against observations that does not say which observations
cannot be falsified by anyone.

### The dose is measured in toxin, not in spores

Exposure first accumulated spore-days. That made the picograms-per-spore figure
decorative: Fitzgerald, Collin and Towers (1998) measured the same spore count
carrying 2.7 times the toxin depending which strains were present, and while the
axis was spores that measurement could not change a result. Nor could anything
that destroys toxin without destroying spores, which is both of the open gaps —
rain eluting it and sunlight altering it.

The axis is now toxin-days per gram. The fitted reactor exposure is the exact
translation of the spore-day threshold it replaced at 1.41 pg per spore, **so no
published number moved on the day the axis did**. A migration that changed the
answers would have hidden whatever else it changed.

### A fitted step is labelled everywhere it appears

The empirical index is marked `fitted` in the data file, described as fitted in
the header that implements it, and the report that quotes it says the model gave
the animals no zinc. A reader who takes one number out of context should still
not be able to mistake a calibration for a citation.

## Consequences

Adding a second spore-borne toxin is a data file. Adding a disease with a
different mechanism — a degree-day insect, a boundary-crossing pest — is not,
and `core/Mycotoxin` is deliberately named for the process rather than for
facial eczema so that the next one does not inherit the wrong shape.

Three quantities in this feature were implemented, sourced carefully, and read
by nothing: the activity term of caveat E10, the disease file itself, and the
toxin conversion. Naming that as a pattern seems more useful than filing three
accidents, and the pattern has a cheap test — ask of every new parameter which
caller consumes it, before the parameter is committed rather than after.

The fitted middle bounds what may be published. Spore counts, the weather
trigger and the liver-injury regression are quotable against their sources; the
GGT a given season produces is quotable only as the output of an index
calibrated to field thresholds, and `docs/validation/verify.md` says so in the
row that matters.
