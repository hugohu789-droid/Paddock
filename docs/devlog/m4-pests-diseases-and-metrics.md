# M4 — Pests, diseases and environmental metrics

*September 2026*

Disease is data, not code. A `.toml` file under `data/diseases/` describes what
weather grows the spores, what dose does the damage and what that costs an
animal, and the engine reads it — so a second disease is a second file, not a
second class. The first one is facial eczema: warm, wet nights raise the spore
count on the litter, exposure accumulates as toxin-days per gram, and the mob's
liver damage follows. `paddock disease <bundle>` reports the season a year would
have asked of a zinc programme.

**One problem worth writing down.** The step from toxin load to serum GGT could
not be derived. Working the sourced numbers straight through gave an answer
about sixteen times out of step with what the field literature observes, and I
could not find the missing factor. Calling the constant Placeholder would have
understated it — it *is* anchored to published spore-count thresholds. Calling
it Derived would have been a lie, because no source states it. So the project
grew a fifth provenance status, `fitted`: calibrated so that published
observations reproduce, and required to name what it was fitted to. It counts as
evidence. It is not a citation. The distinction now travels into every report
and every CSV the model writes, and three other quantities have since needed it.

**One New Zealand thing I did not know.** Sunlight destroys sporidesmin.
Marbrook and Matthews (1962) exposed spore samples and found that on an
overcast day there was no detectable loss from either the exposed sample or the
darkened control — which is to say the toxin survives a dull day and does not
survive a bright one. This model has no light term at all, so it treats a week
of sun and a week of cloud as equally dangerous. It is written down as E16
rather than quietly averaged away.

Next: a flock with an age structure, and books to charge it to.
