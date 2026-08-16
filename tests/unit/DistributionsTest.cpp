#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include <paddock/core/Distributions.hpp>

namespace paddock::core {
namespace {

constexpr std::uint64_t kSeed = 20240701;
constexpr int kLargeSample = 50000;

std::mt19937_64 seeded() {
  return std::mt19937_64(kSeed);
}

// Golden vectors, generated once and pinned. Their job is not to check the
// arithmetic - the statistical tests below do that - but to fail loudly if the
// numbers ever change. Because this suite runs on Linux, macOS and Windows,
// agreement here is the cross-platform reproducibility claim in ADR 0007: the
// same seed gives the same weather, not merely weather with the same
// distribution.

constexpr std::array<double, 5> kUniformUnitGolden = {0.54731644547972313, 0.17346783596247495,
                                                      0.48240805292919942, 0.99424279903065693,
                                                      0.65751928113654734};

constexpr std::array<double, 5> kStandardNormalGolden = {
    0.18492186876393685, -0.0074438735999331042, 0.65072997086266204, 0.17261543074472349,
    1.3094311471943432};

constexpr std::array<double, 3> kExponentialMean5Golden = {3.9628097637265496, 0.95258223261198083,
                                                           3.2928404706295171};

constexpr std::array<double, 5> kGammaShape25Scale3Golden = {7.3512662977622121, 3.8970517160021125,
                                                             14.166377793045966, 9.2587416430394995,
                                                             13.384247838675584};

constexpr std::array<double, 3> kGammaShape04Scale2Golden = {
    0.83759673011910329, 0.01838428768216557, 2.8338805715110311};

constexpr std::array<std::uint64_t, 10> kUniformIntGolden = {6U, 1U, 5U, 6U, 5U,
                                                             5U, 5U, 5U, 6U, 3U};

// Exact: every operation in uniform_unit is exact in IEEE-754 double, so this
// must match bit for bit, not merely to a few units in the last place.
TEST(DistributionsGoldenTest, UniformUnitMatchesThePinnedSequence) {
  std::mt19937_64 engine = seeded();

  for (const double expected : kUniformUnitGolden) {
    EXPECT_EQ(uniform_unit(engine), expected);
  }
}

TEST(DistributionsGoldenTest, UniformIntMatchesThePinnedSequence) {
  std::mt19937_64 engine = seeded();

  for (const std::uint64_t expected : kUniformIntGolden) {
    EXPECT_EQ(uniform_int(engine, 1, 6), expected);
  }
}

// Within four units in the last place: these go through std::log, std::sqrt and
// std::pow, which are not required to be correctly rounded, so the platforms
// may differ in the last bits. Anything larger than that is an algorithm
// change, which is what this test is here to catch.
TEST(DistributionsGoldenTest, StandardNormalMatchesThePinnedSequence) {
  std::mt19937_64 engine = seeded();

  for (const double expected : kStandardNormalGolden) {
    EXPECT_DOUBLE_EQ(standard_normal(engine), expected);
  }
}

TEST(DistributionsGoldenTest, ExponentialMatchesThePinnedSequence) {
  std::mt19937_64 engine = seeded();

  for (const double expected : kExponentialMean5Golden) {
    EXPECT_DOUBLE_EQ(exponential(engine, 5.0), expected);
  }
}

TEST(DistributionsGoldenTest, GammaMatchesThePinnedSequence) {
  std::mt19937_64 engine = seeded();
  for (const double expected : kGammaShape25Scale3Golden) {
    EXPECT_DOUBLE_EQ(gamma(engine, 2.5, 3.0), expected);
  }

  // The shape < 1 path takes the boosted branch, which draws in a different
  // order; pinning it separately keeps that order fixed too.
  std::mt19937_64 boosted = seeded();
  for (const double expected : kGammaShape04Scale2Golden) {
    EXPECT_DOUBLE_EQ(gamma(boosted, 0.4, 2.0), expected);
  }
}

TEST(UniformTest, RangeIsRespectedAndEmptyRangesCollapse) {
  std::mt19937_64 engine = seeded();

  for (int draw = 0; draw < 1000; ++draw) {
    const double value = uniform(engine, -5.0, 12.5);
    ASSERT_GE(value, -5.0);
    ASSERT_LT(value, 12.5);
  }

  EXPECT_EQ(uniform(engine, 3.0, 3.0), 3.0);
  EXPECT_EQ(uniform(engine, 3.0, 1.0), 3.0);
}

TEST(NormalTest, ADegenerateSpreadReturnsTheMean) {
  std::mt19937_64 engine = seeded();

  EXPECT_EQ(normal(engine, 7.5, 0.0), 7.5);
  EXPECT_EQ(normal(engine, 7.5, -1.0), 7.5);
}

TEST(GammaTest, DegenerateParametersReturnZero) {
  std::mt19937_64 engine = seeded();

  EXPECT_EQ(gamma(engine, 0.0, 3.0), 0.0);
  EXPECT_EQ(gamma(engine, -1.0, 3.0), 0.0);
  EXPECT_EQ(gamma(engine, 2.0, 0.0), 0.0);
}

TEST(DistributionsTest, TheSameSeedGivesTheSameSequence) {
  std::mt19937_64 first = seeded();
  std::mt19937_64 second = seeded();
  std::vector<double> from_first;
  std::vector<double> from_second;
  from_first.reserve(100);
  from_second.reserve(100);

  for (int draw = 0; draw < 100; ++draw) {
    from_first.push_back(gamma(first, 1.7, 2.2));
    from_second.push_back(gamma(second, 1.7, 2.2));
  }

  EXPECT_EQ(from_first, from_second);
}

}  // namespace
}  // namespace paddock::core
