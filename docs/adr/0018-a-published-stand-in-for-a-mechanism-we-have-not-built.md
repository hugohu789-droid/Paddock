# 18. A published stand-in for a mechanism we have not built

Date: 2026-09-02

## Status

Accepted. Follows ADR 0016 and 0017.

## Context

After ADR 0017 the seasonal shape was diagnosed rather than merely wrong
(verify.md, E67). Three candidates were measured and eliminated: spring is not
water-limited (Ks 0.930, six of ninety-one days below half), not
nitrogen-limited (95-102 kg mineral N/ha, and the pool accumulates all year),
and heat stress would touch 6.8% of summer days by 1.5 C.

What identified the fault was the trial's own columns. **Spring beats summer in
25 of Winchmore's 25 measured years**, median ratio 3.14, never below 1.09. The
model manages 8 of 10 at a median of 1.16, and loses outright in exactly the
years summer stays wet. Spring dominance in the record is a property of the
plant; in the model it is a property of the weather.

The mechanism is reproductive development. Perennial ryegrass turns
reproductive in spring - stem elongation and seed head are much of the October
flush - and afterwards carries fewer tillers and less potential through summer
even with water in the soil. Paddock has no phenology at all.

Building one is a substantial piece of work, larger than the intake model
queued behind it.

## Decision

Adopt AgPasture's `ReproductiveGrowthFactor`, which exists for this exact
reason and says so:

> Reproductive phase of perennial is not simulated by the model, the
> ReproductiveGrowthFactor attempts to mimic the main effect, which is a higher
> allocation of DM to shoot during this period.

**The reason this is adoption and not curve-fitting is that every number in it
is a function of latitude.** The season starts later and runs shorter towards
the poles, and the allocation increase grows. Nothing in it was chosen by
looking at Winchmore. At this farm's -43.641 the arithmetic gives a season
opening on 1 September, a 35.8-day ramp to a plateau on 7 October, 59.6 days at
full effect, and a 23.9-day taper ending 29 December, peaking at +28.3% shoot
allocation. Winchmore's biggest months, in order, are October, November,
September and December.

Seven parameters, all AgPasture's own class defaults, which its ryegrass and
white clover resource files override none of. `usingReproSeasonFactor` is true
by default there; here a zero increase turns it off and is the default, so a
sward file written before this keeps the seasons it was written with - the same
convention `degree_days_per_leaf` and `temperature_response_exponent` follow.

Applied to potential growth, before nitrogen limitation, because it is an
allocation change rather than a change in what the plant fixes. AgPasture takes
the extra shoot from root; Paddock has no root pool and its RUE is explicitly an
above-ground one, so the modulation lands on above-ground growth directly.

**RUE was not re-pinned.** Its fit target is water use efficiency - Martin et
al. (2006), 12.3 kg DM/ha/mm - and the reproductive season moved it from 10.98
to 12.43, one percent away. The Winchmore annual total is what this model is
*validated* against, and re-pinning RUE to it would be fitting the model to its
own validation.

## Consequences

| | winter | spring | summer | autumn |
|---|---|---|---|---|
| after ADR 0017 | 10.5% | 38.7% | 33.5% | 17.3% |
| now | 9.1% | 43.6% | 32.2% | 15.1% |
| Winchmore | 10.0% | 54.7% | 18.0% | 17.3% |

**Spring gains five points and autumn pays two of them.** That was predicted
before any code was written and is not hidden: autumn now sits 2.2 points from
the trial against a bound of 2.5, so a further slip fails the test rather than
passing unnoticed.

**It does not restore the signature.** Spring still beats summer 8 years in 10
against the record's 25 in 25. A 28% spring boost cannot outweigh a summer that
grows 3,900 kg DM/ha when the rain comes. This buys about a third of what was
left; the rest wants the mechanism, not another multiplier.

**And it separated the two sourced targets** (E70). Water use efficiency is now
better than it has ever been and the annual total is 17% above the trial, where
after ADR 0017 both agreed. The immediate casualty is E40: the production excess
that was its evidence for a missing phosphorus limitation has been 13-20%, then
2%, then 17% in the course of one day's work. It is not evidence, and E40 needs
an argument that does not rest on it.

## Alternatives considered

**Fit a seasonal multiplier to Winchmore's monthly shape.** It would close the
gap completely and destroy the only validation the shape has. This is the
option the whole ADR exists to refuse.

**Build the phenology properly** - reproductive development, tiller dynamics,
post-flowering decline. Correct, and larger than the work queued behind it. E67
records the diagnosis so that this stays available rather than becoming folklore.

**Apply the factor to the grass only.** White clover flowers in summer rather
than running up a spring seed head, so a spring allocation shift describes it
less well. AgPasture applies it to both and overrides nothing, and departing
from the source on judgement alone is what ADR 0017 was written against. Noted
in the sward file and in verify.md instead.
