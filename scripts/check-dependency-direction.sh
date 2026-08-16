#!/usr/bin/env bash
# T2 gate: enforces the two rules that hold the architecture together.
#
#   1. No arrow out of core. core/ includes nothing but C++17 and stdlib, and
#      never a header from gis/, viz/, app/ or ai/.
#   2. No hidden non-determinism in core: no rand(), no wall clock.
#
# A grep is a blunt instrument, but it is a blunt instrument that runs on every
# pull request, and the failure mode it guards against is a single stray
# #include that a reviewer would wave through.
set -euo pipefail

cd "$(dirname "$0")/.."

status=0

report() {
  status=1
  echo "check-dependency-direction: $1" >&2
  echo "$2" >&2
  echo >&2
}

core_sources=$(git ls-files 'core/*.cpp' 'core/*.hpp')

forbidden_includes='#[[:space:]]*include[[:space:]]*[<"](gdal|ogr|cpl_|proj|geos|vtk|Qt|QtCore|QtGui|QtWidgets|curl|nlohmann|toml|spdlog|paddock/(gis|viz|app|ai)/)'
if hits=$(grep -nEI "${forbidden_includes}" ${core_sources} 2>/dev/null); then
  report "core/ must not depend on anything outside C++17 and the stdlib." "${hits}"
fi

# Determinism: global generators and the wall clock have no place in the core.
forbidden_calls='(\bstd::rand\b|\brand\(\)|\bsrand\(|std::chrono::system_clock|std::chrono::steady_clock|std::random_device|\btime\(NULL\)|\btime\(nullptr\))'
if hits=$(grep -nEI "${forbidden_calls}" ${core_sources} 2>/dev/null); then
  report "core/ must not use global RNG or wall-clock time (determinism)." "${hits}"
fi

# The reverse direction is allowed and expected, so only core is checked here.
# gis/, viz/, app/ and ai/ may include paddock/core/ freely.

# ADR 0007: std distributions are implementation-defined, so the same seed gives
# different numbers on different standard libraries. Nothing in the simulator -
# tests included - may use one; core/Distributions.hpp states its algorithms.
# Distributions.hpp itself is exempt: it is the file that names them in order to
# explain why nothing else may.
all_sources=$(git ls-files 'core/*.cpp' 'core/*.hpp' 'gis/*.cpp' 'gis/*.hpp' 'viz/*.cpp' \
  'viz/*.hpp' 'app/*.cpp' 'app/*.hpp' 'ai/*.cpp' 'ai/*.hpp' 'tests/*.cpp' 'tests/*.hpp' \
  | grep -v 'core/include/paddock/core/Distributions.hpp')

forbidden_distributions='std::(uniform_real|uniform_int|normal|lognormal|exponential|gamma|poisson|bernoulli|binomial|geometric|weibull|discrete|piecewise_constant|piecewise_linear|chi_squared|cauchy|fisher_f|student_t|negative_binomial|extreme_value)_distribution'
if hits=$(grep -nEI "${forbidden_distributions}" ${all_sources} 2>/dev/null); then
  report "std distributions are not reproducible across standard libraries; use core/Distributions.hpp (ADR 0007)." "${hits}"
fi

if [ "${status}" -eq 0 ]; then
  echo "check-dependency-direction: core is clean (zero external dependencies, no global RNG)"
fi

exit "${status}"
