# 16. Drought kills leaf, and AgPasture says how much

Date: 2026-09-02

## Status

Accepted.

## Context

The model's annual pasture production sat comfortably inside the band measured
at Winchmore, and its seasonal shape was badly wrong: 41% of the year grown in
summer against a measured 18%, and 32% in spring against a measured 55%. On a
Canterbury dryland farm summer is when growth stops, and it was the model's
biggest season (E61).

The water stress was not the problem. Measured over a ten-year run, the FAO-56
coefficient averages 0.618 in spring and 0.366 in summer, and 86 to 100% of
November-to-February days sit below 0.5. Summer is correctly the more limited
season.

The problem was that the same coefficient was multiplied into leaf death, in
the direction that protects the sward. `senescence_share` drove leaf appearance
by thermal time *and moisture*, on the argument that a thirsty tiller takes
longer to push out its next leaf, so the leaf it is carrying lives longer. That
argument is sound and DairyNZ state it.

It is also half of what a drought does. Senescence share is linear in
`degree_days x Ks`, and on this farm's weather the halves cancelled almost
exactly - spring 7.5 x 0.618 = 4.64, summer 12.5 x 0.366 = 4.59. Leaf lived 78
days in a February drought and 78 days in an October spring. Cover never fell,
light interception held, and growth carried on (E62).

## Decision

Add the half that was missing - leaf that dies because the plant has run out of
water - and take its form and its numbers from APSIM AgPasture rather than
inventing them.

Below a threshold, the tissue turnover rate is multiplied by

    1 + max * ((threshold - Ks) / threshold) ^ exponent

with `threshold = 0.6`, `max = 1.0`, `exponent = 2.0`: no effect in a wet
spring, doubling on a profile at wilting point. AgPasture gives its ryegrass
and its white clover identical values, so both species here carry the same
three. They are TOML parameters, not constants, and a threshold of zero turns
the effect off.

Source: `MoistureEffectOnTissueTurnover` in AgPasture's `PastureSpecies`, with
defaults from `Models/Resources/AGPRyegrass.json` and `AGPWhiteClover.json`.
The model is Li FY, Snow VO & Holzworth DR (2011), "Modelling the seasonal and
geographical pattern of pasture production in New Zealand", *New Zealand
Journal of Agricultural Research* 54: 331-352 - calibrated on this exact
question.

**And remove the water factor from leaf appearance.** Adding the drought term
alone moved the seasonal shape by two tenths of a point, because the appearance
term was still dividing by the same Ks and the two halves went on cancelling.
AgPasture drives tissue turnover by temperature and moisture with moisture
*increasing* it; there is no term anywhere in AgPasture where water slows leaf
death. Its `ttfLeafNumber` is a structural constant, not a phyllochron.

## Consequences

Every season moved toward the measurement, and so did the annual total:

| | winter | spring | summer | autumn | decade mean |
|---|---|---|---|---|---|
| before | 6.2% | 32.2% | 41.3% | 20.1% | 8,106 kg DM/ha |
| after | 6.7% | 35.6% | 39.9% | 17.9% | 7,287 kg DM/ha |
| Winchmore | 10.0% | 54.7% | 18.0% | 17.3% | 6,442 kg DM/ha |

**Three points of a twenty-two point gap.** The mechanism was real and is now
sourced; it is not what most of the seasonal error was made of. The remainder
is held from both sides by `WinchmoreSeasonalTest`, and E64 names the next
suspect on evidence: `optimum_temperature_c = 20.0` is a PLACEHOLDER, and with
a base of 4.4 C it hands summer a 1.67x temperature advantage over spring
before radiation adds another 1.24x. It is not to be moved until it is sourced.

**The consequence that matters more than the shape.** `DroughtDestockingTest`
asserted that a drought year and a wet year harvest within 5% of each other,
and recorded in its own comment that separation would mean the farm had become
feed-limited. They separated: 155 tonnes eaten against 195, and the year's
lowest cover moved out of late winter and into the summer, 652 against 954 kg
DM/ha. This is the first time anything in this model has gone short of feed for
a reason the weather caused. An intake model has to stand on that.

Nobody sells yet, so M4's destocking acceptance remains unmet.

## Alternatives considered

**Tune `optimum_temperature_c` down until the shape matched.** It would have
worked, and it would have been E40's mistake repeated - pushing a missing
mechanism into a parameter that nobody had sourced. The parameter is now flagged
rather than moved.

**Keep both water effects and raise the drought multiplier.** This would have
made the numbers agree while leaving the model saying that leaf lives longer in
a drought, which is the claim that was wrong.
