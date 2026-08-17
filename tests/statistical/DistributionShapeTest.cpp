// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Statistical shape tests: many draws, checked against the distribution they
// are supposed to come from.
//
// These are labelled "statistical" rather than "fast" and are therefore not in
// the pre-commit subset. They are not less important - CI runs every one of
// them on every pull request - but a hook that takes half a minute is a hook
// developers start bypassing, and T1's job is to be quick enough that nobody
// does.

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
constexpr int kLargeSample = 200000;

std::mt19937_64 seeded() {
  return std::mt19937_64(kSeed);
}

TEST(UniformTest, UnitDeviatesStayInTheHalfOpenUnitInterval) {
  std::mt19937_64 engine = seeded();

  for (int draw = 0; draw < kLargeSample; ++draw) {
    const double value = uniform_unit(engine);
    ASSERT_GE(value, 0.0);
    ASSERT_LT(value, 1.0);
  }
}

TEST(UniformTest, IntegersCoverTheWholeClosedRange) {
  std::mt19937_64 engine = seeded();
  std::array<int, 6> counts{};

  for (int draw = 0; draw < 60000; ++draw) {
    const std::uint64_t value = uniform_int(engine, 1, 6);
    ASSERT_GE(value, 1U);
    ASSERT_LE(value, 6U);
    ++counts.at(static_cast<std::size_t>(value - 1));
  }

  for (const int count : counts) {
    EXPECT_NEAR(static_cast<double>(count), 10000.0, 500.0);
  }
  EXPECT_EQ(uniform_int(engine, 7, 7), 7U);
  EXPECT_EQ(uniform_int(engine, 9, 4), 9U);
}

TEST(BernoulliTest, CertaintiesShortCircuitAndFrequencyMatches) {
  std::mt19937_64 engine = seeded();

  EXPECT_FALSE(bernoulli(engine, 0.0));
  EXPECT_FALSE(bernoulli(engine, -1.0));
  EXPECT_TRUE(bernoulli(engine, 1.0));
  EXPECT_TRUE(bernoulli(engine, 2.0));

  int hits = 0;
  for (int draw = 0; draw < kLargeSample; ++draw) {
    if (bernoulli(engine, 0.3)) {
      ++hits;
    }
  }

  EXPECT_NEAR(static_cast<double>(hits) / kLargeSample, 0.3, 0.01);
}

TEST(NormalTest, MeanAndSpreadMatchTheParameters) {
  std::mt19937_64 engine = seeded();
  double sum = 0.0;
  double sum_of_squares = 0.0;

  for (int draw = 0; draw < kLargeSample; ++draw) {
    const double value = normal(engine, 12.0, 3.0);
    sum += value;
    sum_of_squares += value * value;
  }

  const double mean = sum / kLargeSample;
  const double variance = (sum_of_squares / kLargeSample) - (mean * mean);
  EXPECT_NEAR(mean, 12.0, 0.05);
  EXPECT_NEAR(std::sqrt(variance), 3.0, 0.05);
}

TEST(ExponentialTest, MeanMatchesAndDeviatesAreNonNegative) {
  std::mt19937_64 engine = seeded();
  double sum = 0.0;

  for (int draw = 0; draw < kLargeSample; ++draw) {
    const double value = exponential(engine, 4.0);
    ASSERT_GE(value, 0.0);
    sum += value;
  }

  EXPECT_NEAR(sum / kLargeSample, 4.0, 0.05);
  EXPECT_EQ(exponential(engine, 0.0), 0.0);
  EXPECT_EQ(exponential(engine, -2.0), 0.0);
}

// Daily rainfall depth on a wet day is conventionally gamma-distributed, so
// both the mean and the spread have to be right, not just the mean.
TEST(GammaTest, MeanAndVarianceMatchShapeAndScale) {
  std::mt19937_64 engine = seeded();
  double sum = 0.0;
  double sum_of_squares = 0.0;

  for (int draw = 0; draw < kLargeSample; ++draw) {
    const double value = gamma(engine, 2.5, 3.0);
    ASSERT_GT(value, 0.0);
    sum += value;
    sum_of_squares += value * value;
  }

  const double mean = sum / kLargeSample;
  const double variance = (sum_of_squares / kLargeSample) - (mean * mean);
  EXPECT_NEAR(mean, 2.5 * 3.0, 0.05);           // shape * scale
  EXPECT_NEAR(variance, 2.5 * 3.0 * 3.0, 0.5);  // shape * scale^2
}

TEST(GammaTest, ShapesBelowOneUseTheBoostedBranch) {
  std::mt19937_64 engine = seeded();
  double sum = 0.0;

  for (int draw = 0; draw < kLargeSample; ++draw) {
    const double value = gamma(engine, 0.4, 2.0);
    ASSERT_GE(value, 0.0);
    sum += value;
  }

  EXPECT_NEAR(sum / kLargeSample, 0.4 * 2.0, 0.02);
}

}  // namespace
}  // namespace paddock::core
