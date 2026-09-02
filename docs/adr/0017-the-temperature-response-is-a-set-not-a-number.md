# 17. The temperature response is a set, not a number

Date: 2026-09-02

## Status

Accepted. Extends ADR 0016.

## Context

After ADR 0016 the model's seasonal shape was still wrong: 35.6% of the year
grown in spring against a measured 54.7%, and 39.9% in summer against 18.0%.
The arithmetic pointed at one parameter. With a base of 4.4 C and an optimum of
20.0 C in a triangular response, Canterbury's 11.9 C spring gets 48% of the
response and its 16.9 C summer gets 80% - a 1.67x advantage to summer before
radiation adds another 1.24x. `optimum_temperature_c` was marked PLACEHOLDER
and had never been read out of anything.

The obvious move was to lower it until the seasons agreed. A reader checking
the literature first found that this would have been wrong twice over.

**The value was defensible all along.** Oregon State extension gives perennial
ryegrass a growth optimum of 65-70 F (18.3-21.1 C); classic summaries give
18-20 C; a 2022 study of multiple accessions gives 25.2-29.4 C with large
genotype variability. The placeholder happened to sit in the middle of the
supported range.

**And an optimum means nothing on its own.** It belongs to a process - leaf
elongation, shoot growth, germination and biomass accumulation do not share one
- and it belongs to a response function. Copying a number out of a paper into a
different equation than the one it was fitted to is not sourcing it.

## Decision

Take the cardinal temperatures, the functional form and the exponent from one
place, as one set, and change nothing else.

The source is APSIM AgPasture's `TemperatureLimitingFactor`, the C3 response
after Thornley & Johnson:

    Tmax = Topt + (Topt - Tmin) / q
    f(T) = ((T - Tmin)^q * (Tmax - T)) / ((Topt - Tmin)^q * (Tmax - Topt))

It is a photosynthesis response inside a pasture biomass model calibrated on
New Zealand seasonal production, which is the process and the place this model
needs. Ryegrass takes `Tmin 2.0, Topt 20.0, q 1.5`, so `Tmax` is 32.0; white
clover takes `3.5, 23.0, 1.65`, so `Tmax` is 34.82. **The optimum did not
move.** Li FY, Snow VO & Holzworth DR (2011), NZJAR 54: 331-352.

`temperature_response_exponent` is a TOML parameter, and zero keeps the
triangular response, so a sward file written before this still runs as written.

**And feed the curve the temperature it was fitted to.** AgPasture evaluates it
twice - at the daily mean and at a daylight temperature of `0.75*max +
0.25*min` - and weights them one to three. Paddock was passing the plain
24-hour mean, half of which is spent in the dark at the coldest part of the day.
The resulting understatement is proportional to the diurnal range and to the
local steepness of the curve, which makes it worth 30% in a Canterbury winter
and 2% in its summer.

**And refit the radiation use efficiency, which was always a fit.** Its target
is unchanged and sourced: Martin et al. (2006), 12.3 kg DM/ha/mm for Canterbury
dryland. Fitted against the old response it was stale by construction.

## Consequences

| | winter | spring | summer | autumn |
|---|---|---|---|---|
| at E61 | 6.2% | 32.2% | 41.3% | 20.3% |
| now | 10.5% | 38.7% | 33.5% | 17.3% |
| Winchmore | 10.0% | 54.7% | 18.0% | 17.3% |

**Winter and autumn are closed** - within half a point, from four and three
points out - and their test bounds have tightened from twelve points to two and
a half. Water use efficiency lands at 12.24 against the 12.3 it was fitted to,
and the decade mean at 6,572 kg DM/ha against a measured 6,442.

**Spring and summer are not closed.** They moved six and eight points and
remain sixteen apart. The temperature response has now been taken as far as a
published parameter set and a published curve can take it, and the remainder
did not come with it. That is a more tractable problem than the one this
started as, and E66 lists what has not been looked at: spring phenology and
reproductive growth, tillering, seasonal nitrogen supply, and whether the water
balance really empties a Canterbury profile in February.

**It also costs E40 its evidence.** The model used to grow 13-20% more pasture
than the trial while applying no phosphorus, and that excess was the headline
argument for a missing nutrient limitation. Most of it was the temperature
response. E40 is not closed, but it needs a new argument.

**And the flock got hungrier.** `StockUnitIntakeTest` had the mob eating 73% of
its stock-unit demand and now has it eating 68%, because there is less grass -
and there is less grass because there was always supposed to be less. The bound
moved to the measurement rather than the measurement being argued away.

## Alternatives considered

**Lower `Topt` until the seasons agreed.** It would have worked, it would have
contradicted the literature, and it would have been E40's mistake for a third
time.

**Keep the triangular response and adopt only the cardinals.** This is the
error the whole ADR is about: AgPasture's exponent is fitted jointly with its
cardinals, and its derived maximum is a function of both. Splitting the set
would have used published numbers in an equation nobody fitted them to.
