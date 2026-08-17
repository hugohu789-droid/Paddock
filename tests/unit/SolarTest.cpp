// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <gtest/gtest.h>

#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/Solar.hpp>

namespace paddock::core {
namespace {

// FAO-56 Example 8: extraterrestrial radiation on 3 September at 20 degrees
// south. This is a published worked example, so it checks the implementation
// against the source rather than against itself. The tolerances are set by
// FAO-56's own rounding, not by ours.
TEST(SolarTest, MatchesFao56Example8) {
  const int day = Date{2023, 9, 3}.day_of_year();
  ASSERT_EQ(day, 246);

  EXPECT_NEAR(inverse_relative_distance(day), 0.985, 0.001);
  EXPECT_NEAR(solar_declination(day), 0.120, 0.001);
  EXPECT_NEAR(sunset_hour_angle(-20.0, day), 1.527, 0.001);
  EXPECT_NEAR(extraterrestrial_radiation_mj(-20.0, day), 32.2, 0.1);
  EXPECT_NEAR(radiation_as_evaporation_mm(extraterrestrial_radiation_mj(-20.0, day)), 13.1, 0.1);
}

// FAO-56 Example 9, same date and latitude.
TEST(SolarTest, MatchesFao56Example9) {
  EXPECT_NEAR(daylight_hours(-20.0, Date{2023, 9, 3}.day_of_year()), 11.7, 0.05);
}

TEST(SolarTest, SouthernSummerPeaksAroundTheDecemberSolstice) {
  constexpr double kCanterburyLatitude = -43.5;
  const double midsummer =
      extraterrestrial_radiation_mj(kCanterburyLatitude, Date{2023, 12, 22}.day_of_year());
  const double midwinter =
      extraterrestrial_radiation_mj(kCanterburyLatitude, Date{2023, 6, 21}.day_of_year());
  const double equinox =
      extraterrestrial_radiation_mj(kCanterburyLatitude, Date{2023, 3, 21}.day_of_year());

  EXPECT_GT(midsummer, equinox);
  EXPECT_GT(equinox, midwinter);
  // A Canterbury winter day is about a third of a midsummer one at the top of
  // the atmosphere; that ratio is what makes winter growth rates collapse.
  EXPECT_LT(midwinter / midsummer, 0.4);
}

TEST(SolarTest, HemispheresAreMirrorImages) {
  const int day = Date{2023, 12, 22}.day_of_year();

  EXPECT_GT(extraterrestrial_radiation_mj(-43.5, day), extraterrestrial_radiation_mj(43.5, day));
  EXPECT_GT(daylight_hours(-43.5, day), 12.0);
  EXPECT_LT(daylight_hours(43.5, day), 12.0);
}

TEST(SolarTest, DaylengthAtTheEquatorIsAlwaysAboutTwelveHours) {
  for (const int month : {1, 4, 7, 10}) {
    const int day = Date{2023, month, 15}.day_of_year();
    EXPECT_NEAR(daylight_hours(0.0, day), 12.0, 0.01);
  }
}

// Inside the polar circles the sunset hour angle has no solution; clamping is
// what keeps a raster tile from outside New Zealand from producing NaN.
TEST(SolarTest, PolarDayAndNightAreClampedRatherThanUndefined) {
  const int southern_midsummer = Date{2023, 12, 22}.day_of_year();
  const int southern_midwinter = Date{2023, 6, 21}.day_of_year();

  EXPECT_NEAR(daylight_hours(-80.0, southern_midsummer), 24.0, 1e-9);
  EXPECT_NEAR(daylight_hours(-80.0, southern_midwinter), 0.0, 1e-9);
  EXPECT_GE(extraterrestrial_radiation_mj(-80.0, southern_midwinter), 0.0);
}

TEST(SolarTest, EquivalentEvaporationUsesThePublishedFactor) {
  EXPECT_DOUBLE_EQ(radiation_as_evaporation_mm(10.0), 4.08);
  EXPECT_DOUBLE_EQ(radiation_as_evaporation_mm(0.0), 0.0);
}

}  // namespace
}  // namespace paddock::core
