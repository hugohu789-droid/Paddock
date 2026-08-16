#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

#include <paddock/core/Distributions.hpp>

namespace paddock::core {

namespace {

/// 2^-53. Multiplying a 53-bit integer by this is exact in IEEE-754 double.
constexpr double kTwoPowMinus53 = 1.0 / 9007199254740992.0;

/// Marsaglia and Tsang's squeeze constant for the gamma acceptance test.
constexpr double kGammaSqueeze = 0.0331;

}  // namespace

double uniform_unit(std::mt19937_64& engine) noexcept {
  // The top 53 bits are the ones mt19937_64 tempers best, and 53 is exactly the
  // significand width, so the result lands on the representable grid of [0, 1).
  return static_cast<double>(engine() >> 11U) * kTwoPowMinus53;
}

double uniform(std::mt19937_64& engine, double low, double high) noexcept {
  if (!(high > low)) {
    return low;
  }
  return low + ((high - low) * uniform_unit(engine));
}

std::uint64_t uniform_int(std::mt19937_64& engine, std::uint64_t low, std::uint64_t high) noexcept {
  if (high <= low) {
    return low;
  }
  const std::uint64_t range = high - low;
  if (range == std::numeric_limits<std::uint64_t>::max()) {
    return engine();
  }
  const std::uint64_t buckets = range + 1;
  // Reject the tail that would make the low residues more likely: (~n + 1) % n
  // is 2^64 mod n in unsigned arithmetic.
  const std::uint64_t threshold = (~buckets + 1) % buckets;
  std::uint64_t draw = engine();
  while (draw < threshold) {
    draw = engine();
  }
  return low + (draw % buckets);
}

bool bernoulli(std::mt19937_64& engine, double probability) noexcept {
  if (probability <= 0.0) {
    return false;
  }
  if (probability >= 1.0) {
    return true;
  }
  return uniform_unit(engine) < probability;
}

double standard_normal(std::mt19937_64& engine) noexcept {
  double first = 0.0;
  double second = 0.0;
  double radius_squared = 0.0;
  do {
    first = (2.0 * uniform_unit(engine)) - 1.0;
    second = (2.0 * uniform_unit(engine)) - 1.0;
    radius_squared = (first * first) + (second * second);
  } while (radius_squared >= 1.0 || radius_squared == 0.0);

  return first * std::sqrt(-2.0 * std::log(radius_squared) / radius_squared);
}

double normal(std::mt19937_64& engine, double mean, double standard_deviation) noexcept {
  if (standard_deviation <= 0.0) {
    return mean;
  }
  return mean + (standard_deviation * standard_normal(engine));
}

double exponential(std::mt19937_64& engine, double mean) noexcept {
  if (mean <= 0.0) {
    return 0.0;
  }
  // uniform_unit is in [0, 1), so log1p(-u) is finite: no infinite wait times.
  return -mean * std::log1p(-uniform_unit(engine));
}

double gamma(std::mt19937_64& engine, double shape, double scale) noexcept {
  if (shape <= 0.0 || scale <= 0.0) {
    return 0.0;
  }

  if (shape < 1.0) {
    // Boost a sub-unit shape into the main method's range:
    // Gamma(a) == Gamma(a + 1) * U^(1/a). The uniform is drawn first so that
    // the order of engine draws does not depend on the recursion.
    const double boost = uniform_unit(engine);
    return gamma(engine, shape + 1.0, scale) * std::pow(boost, 1.0 / shape);
  }

  const double shift = shape - (1.0 / 3.0);
  const double scaled = 1.0 / std::sqrt(9.0 * shift);

  while (true) {
    double deviate = 0.0;
    double cubed = 0.0;
    do {
      deviate = standard_normal(engine);
      cubed = 1.0 + (scaled * deviate);
    } while (cubed <= 0.0);
    cubed = cubed * cubed * cubed;

    const double acceptance = uniform_unit(engine);
    const double squared = deviate * deviate;
    if (acceptance < 1.0 - (kGammaSqueeze * squared * squared)) {
      return shift * cubed * scale;
    }
    if (std::log(acceptance) < (0.5 * squared) + (shift * (1.0 - cubed + std::log(cubed)))) {
      return shift * cubed * scale;
    }
  }
}

}  // namespace paddock::core
