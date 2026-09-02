# M5 — The flock, its books, and a target that was measured

*September 2026*

The farm carries a breeding flock now, in the classes a farmer uses rather than
as a head count. Ewes are mated, carry lambs through winter and milk in spring
on OVERSEER's equation set; lambs graze beside their mothers, are weaned at
about 31 kg, and are sold store or finished. Beside it there are books: prices
and costs from Beef + Lamb New Zealand's Class 6 survey, and a farmer who
proposes an action before taking it so that money can refuse. Nitrogen comes
back out of the animal — urine patches, dung, wool, liveweight — and leaches in
two pools, reported against a regional rule the user supplies. An indicators
page reads the year four ways, for a farmer, a researcher, a regulator, and
somebody who wants the whole table.

**One problem worth writing down.** The model's annual production was measured
against a band worked out of published *reviews* of the Winchmore irrigation
trial: 5.5 to 6.5 t DM/ha for dryland, from a review saying irrigation "roughly
doubles" production. Then I fetched the trial's own data. Its 25 dryland years
have a mean of 6,442 kg DM/ha and a range of 3,904 to 9,845 — the derived band
was one tonne wide where the truth is nearly six, and its midpoint sat at the
very top of what the trial actually averaged. A validation built on it would
have failed this model for having weather. A rain-fed Canterbury year is a
distribution, not a number. `scripts/winchmore-fetch.py` now pulls the series,
checks its hash and rebuilds the calibration file from it.

**One New Zealand thing.** Measured against that, the model grows about 20% more
than the trial — while applying no fertiliser, where Winchmore's dryland
treatment gets 250 kg of superphosphate a hectare a year. An unfertilised farm
should produce *less*, so the real gap is wider than the number says. Re-fitting
radiation use efficiency would close it and hide the reason: there is no
phosphorus limitation in this model at all. It stays open, as E40.

Next: save and load, a scenario editor, and the utilisation gap.
