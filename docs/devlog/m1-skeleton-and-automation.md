# M1 — Skeleton and automation

*August 2026*

An empty project, but a real pipeline. Paddock now has its simulation kernel's
type foundation — a georeferenced `Raster<T>` in NZTM2000, `Polygon`, entities
as components, a daily clock, seeded RNG streams and the budget ledger — plus
the gates every later milestone has to pass through: format-on-save and clangd
diagnostics in both editors, a pre-commit hook, and a three-platform CI matrix
running the full test suite, clang-tidy, and a Debug build under ASan and UBSan.
Sixty-two tests, no business logic. That was the point.

**One problem worth writing down.** The pre-commit hook runs the tests labelled
`fast`, and it was passing in well under a second. It should have been running
58 tests; it was running one. CMake's `gtest_discover_tests(... PROPERTIES
LABELS "unit;fast")` treats the semicolon as a list separator and quietly keeps
only the first label, so every discovered test was labelled `unit` and nothing
matched `fast`. Escaping it — `"unit\;fast"` — fixed it. A gate that looks green
while checking almost nothing is worse than no gate, which is also why the
conservation suite ships with a negative control: one deliberately unreported
outflow, asserted to break the budget.

**One New Zealand thing.** The farm year here runs 1 July to 30 June, and it is
tempting to step a simulation "one year" by advancing 365 days. From 1 July 2023
that lands on 30 June 2024 — February 2024 has 29 days. Seasonal comparisons
have to step to a date, not to a count. My clock test asserted the wrong answer
and the code was right, which is the good way round to find out.

Next: weather, soil water and pasture growth, with the first parameters that
have to cite a published source.
