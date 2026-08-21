# M2 — Weather, soil water and pasture

*August 2026*

A year of weather now drives a soil water bucket and a ryegrass–white clover
sward, and you can watch it. `paddock scenario run` prints the year; `paddock-gui`
draws it as a map you can scrub through a day at a time. All three budgets — dry
matter, water, nitrogen — close to 1e-9 over the year and on every individual
day, each with a negative control proving the gate can fail. Every parameter in
the model cites FAO-56 or a published New Zealand source, or is marked
PLACEHOLDER and listed in `docs/validation/verify.md`.

**One problem worth writing down.** The validation gate compares modelled
seasonal growth with DairyNZ's measured averages, and it failed. Twice, for two
different reasons that were both mine. The first: the scenario started on
1 January with a full soil profile, so January grew on water it had been handed
rather than water it had earned, and the model's January was its peak — above
every measured January. The second: I was comparing a single simulated year
against multi-year measured means, so one dry February counted as model error
rather than as weather. Starting the run on 1 July and averaging twenty years
after a spin-up year took the correlation from 0.77 to 0.97. Neither fix was a
loosened tolerance.

**One New Zealand thing.** The gate still failed against Lincoln, and passed
against Woodlands, and the reason turned out to be agronomy rather than
arithmetic. Nitrogen fertiliser lifts the shoulders of the season — late winter
and early autumn, when temperature and slow mineralisation would otherwise hold
growth back — so a fertilised sward spreads its growth more evenly across the
year. This model has no fertiliser; its only nitrogen income is clover fixation.
Holding it to a fertilised site's seasonal distribution was asking it to
reproduce fertiliser it never received. It is measured against the one
unfertilised site on the sheet now, and the test says why.

Next: real farm boundaries and terrain from LINZ, and livestock.
