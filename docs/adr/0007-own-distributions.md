# ADR 0007 — Core implements its own distributions

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M2 (closes caveat E1 from M1)

## Context

M1 asserted that the same seed produces bit-identical results, and it does —
on one machine. The claim quietly depended on the standard library, because
`std::mt19937_64`'s output sequence is specified by the standard but
`std::uniform_real_distribution` and `std::normal_distribution` are not.
libstdc++, libc++ and the MSVC STL use different algorithms and return
different numbers from the same engine in the same state.

That makes three things impossible: a golden regression baseline that means
anything on more than one platform, a scenario bundle that reproduces on a
colleague's machine, and a validation result a reviewer can check.

The gap was recorded as E1 in `docs/verify.md` at the end of M1 rather than
papered over. This closes it.

## Decision

`core/Distributions.hpp` provides the deviates the model needs — uniform,
uniform integer, Bernoulli, normal, exponential, gamma — with the algorithm
stated in the source:

- **uniform** takes the top 53 bits of one engine draw and scales by 2^-53.
  Every operation is exact in IEEE-754 double, so the result is bit-identical
  on every platform.
- **normal** is the Marsaglia polar method. It generates deviates in pairs and
  **discards the second**: a cached deviate is hidden state, and hidden state
  is what stops a run reproducing when the order of calls changes. One wasted
  draw per call is a good trade.
- **gamma** is Marsaglia and Tsang, with the standard boost for shape < 1. The
  boosting uniform is drawn before the recursive call so the order of engine
  draws does not depend on the recursion.
- **exponential** is inversion through `log1p`.

Nothing in the simulator may use a `std::` distribution. The tests use these
functions too, so the determinism suite tests what ships.

Golden vectors for each distribution are pinned in
`tests/unit/DistributionsTest.cpp` and run on all three CI platforms. That test
*is* the cross-platform reproducibility claim; the statistical tests beside it
check that the deviates are actually drawn from the intended distribution.

## Consequences

- The same seed gives the same weather on Linux, macOS and Windows, so golden
  baselines and scenario bundles can be pinned once rather than per platform.
- **The guarantee is exact for uniform deviates and to within a few units in
  the last place for the rest.** `std::log`, `std::sqrt` and `std::pow` are not
  required to be correctly rounded, so the last bits of a normal or gamma
  deviate may differ between platforms' libm. The golden tests assert to four
  ULP for those and exactly for uniform. Closing that remaining gap would mean
  implementing our own transcendentals, which is not worth it: a difference of
  a few ULP in a single deviate cannot move a seasonal growth curve, whereas a
  different algorithm moves everything.
- Rejection-based methods (uniform integer, normal, gamma) consume a variable
  number of engine draws. That is fine because streams are keyed by entity ID
  and never shared between subsystems, so one entity's rejection loop cannot
  shift another's sequence.
- Adding a distribution later means adding it here, with a golden vector, not
  reaching into `<random>`.
