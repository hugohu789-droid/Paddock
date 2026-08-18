// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The lookup table that makes per-cell slope radiation affordable.
//
// The thing worth testing is not that it stores numbers but that the numbers it
// returns are the ones slope_radiation_ratio would have computed, to the
// accuracy the sampling interval was chosen for. So every assertion here
// compares the table against the function it replaces.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

#include <paddock/core/SlopeRadiation.hpp>
#include <paddock/core/Solar.hpp>
#include <paddock/core/SyntheticTerrain.hpp>
#include <paddock/core/Topography.hpp>

namespace paddock::core {
namespace {

constexpr double kCanterburyLatitude = -43.6;
constexpr double kWest = 1570000.0;
constexpr double kSouth = 5179000.0;

/// The interpolation error the sampling interval was chosen against: 0.008 of a
/// ratio on the worst case, a steep shaded slope in midwinter. See the table in
/// SlopeRadiation.hpp.
constexpr double kInterpolationTolerance = 0.01;

Topography sloping_ground(double rise_east, double rise_north) {
  SyntheticSurface surface;
  surface.reference_easting = kWest;
  surface.reference_northing = kSouth;
  surface.gradient_east = rise_east;
  surface.gradient_north = rise_north;
  surface.undulation_amplitude_m = 0.0;

  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{kWest, kSouth});
  area.expand_to_include(Point2D{kWest + 200.0, kSouth + 200.0});
  return topography_of(SyntheticElevationSource(surface).fetch(area, 10.0));
}

// The point of the whole class: what it returns has to be what the function it
// stands in for would have said, on every day of the year and not just on the
// days it sampled.
TEST(SlopeRadiationTest, TheTableAgreesWithTheFunctionItReplaces) {
  // Falling south at 1 in 2 - about 27 degrees, and shaded, which is where
  // interpolation is hardest.
  const Topography ground = sloping_ground(0.0, 0.5);
  const SlopeRadiationTable table(ground, kCanterburyLatitude);

  const std::size_t col = ground.slope_degrees.cols() / 2;
  const std::size_t row = ground.slope_degrees.rows() / 2;
  const double slope = ground.slope_degrees(col, row);
  const double aspect = ground.aspect_degrees(col, row);

  double worst = 0.0;
  for (int day = 1; day <= 366; ++day) {
    const double exact = slope_radiation_ratio(kCanterburyLatitude, day, slope, aspect);
    worst = std::max(worst, std::abs(table.ratio(col, row, day) - exact));
  }
  EXPECT_LT(worst, kInterpolationTolerance) << "worst interpolation error " << worst;
}

// On a sampled day there is nothing to interpolate, so the table should be
// exact. If it is not, the indexing is wrong rather than the interpolation.
TEST(SlopeRadiationTest, SampledDaysAreExact) {
  const Topography ground = sloping_ground(0.0, -0.3);
  const SlopeRadiationTable table(ground, kCanterburyLatitude);

  const std::size_t col = 3;
  const std::size_t row = 4;
  const double slope = ground.slope_degrees(col, row);
  const double aspect = ground.aspect_degrees(col, row);

  for (int day = 1; day <= 366; day += SlopeRadiationTable::sample_interval_days()) {
    const double exact = slope_radiation_ratio(kCanterburyLatitude, day, slope, aspect);
    ASSERT_NEAR(table.ratio(col, row, day), exact, 1e-12) << "on sampled day " << day;
  }
}

// Level ground is what every ratio is measured against, so its own ratio is one
// by definition - and it is also the case whose aspect is NaN, which must not
// reach the arithmetic.
TEST(SlopeRadiationTest, LevelGroundIsExactlyOneAllYear) {
  const Topography ground = sloping_ground(0.0, 0.0);
  const SlopeRadiationTable table(ground, kCanterburyLatitude);

  for (std::size_t row = 0; row < table.rows(); ++row) {
    for (std::size_t col = 0; col < table.cols(); ++col) {
      ASSERT_TRUE(std::isnan(ground.aspect_degrees(col, row)));
      for (const int day : {1, 100, 200, 300, 366}) {
        ASSERT_DOUBLE_EQ(table.ratio(col, row, day), 1.0);
      }
    }
  }
}

// The seasonal reversal, through the table rather than the function: a northerly
// slope gains in winter and loses slightly in high summer.
//
// The direction of each inequality is geometry and holds independently of this
// code. The threshold of 1.5 is not - it is a regression pin taken from this
// implementation, loose enough to leave the interpolation room and tight enough
// that losing the winter gain would fail.
TEST(SlopeRadiationTest, ANortherlySlopeGainsInWinterAndNotInSummer) {
  const Topography ground = sloping_ground(0.0, -0.36);  // about 20 degrees, facing north
  const SlopeRadiationTable table(ground, kCanterburyLatitude);

  const std::size_t col = ground.slope_degrees.cols() / 2;
  const std::size_t row = ground.slope_degrees.rows() / 2;

  const double midwinter = table.ratio(col, row, 172);
  const double midsummer = table.ratio(col, row, 355);

  EXPECT_GT(midwinter, 1.5) << "midwinter ratio " << midwinter;
  EXPECT_LT(midsummer, 1.0) << "midsummer ratio " << midsummer;
}

// Days outside the year are clamped rather than read out of bounds. A caller
// stepping a leap year hands day 366 to a table built on 365 sample points.
TEST(SlopeRadiationTest, DaysOutsideTheYearAreClamped) {
  const Topography ground = sloping_ground(0.1, -0.2);
  const SlopeRadiationTable table(ground, kCanterburyLatitude);

  EXPECT_DOUBLE_EQ(table.ratio(1, 1, 0), table.ratio(1, 1, 1));
  EXPECT_DOUBLE_EQ(table.ratio(1, 1, 400), table.ratio(1, 1, 366));
}

TEST(SlopeRadiationTest, CellsOutsideTheGridAreRefused) {
  const Topography ground = sloping_ground(0.0, -0.2);
  const SlopeRadiationTable table(ground, kCanterburyLatitude);

  EXPECT_THROW(static_cast<void>(table.ratio(table.cols(), 0, 1)), std::out_of_range);
  EXPECT_THROW(static_cast<void>(table.ratio(0, table.rows(), 1)), std::out_of_range);
}

}  // namespace
}  // namespace paddock::core
