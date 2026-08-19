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

// Where the sun is over Lincoln at 14:00 solar time.
//
// The figures were worked out independently of this code, from FAO-56 Eq. 24
// and the standard hour-angle relations, before it was written - so they are a
// check against the source rather than a recording of what the function
// happens to return.
TEST(SolarTest, TheSunOverLincolnAtTwoInTheAfternoon) {
  constexpr double kLincoln = -43.64;

  const SunPosition midwinter = sun_position(kLincoln, 172, 14.0);
  EXPECT_NEAR(midwinter.elevation_degrees, 17.5, 0.2);
  EXPECT_NEAR(midwinter.azimuth_degrees, 331.2, 0.5);

  const SunPosition midsummer = sun_position(kLincoln, 355, 14.0);
  EXPECT_NEAR(midsummer.elevation_degrees, 58.2, 0.2);
  EXPECT_NEAR(midsummer.azimuth_degrees, 299.6, 0.5);

  const SunPosition equinox = sun_position(kLincoln, 264, 14.0);
  EXPECT_NEAR(equinox.elevation_degrees, 39.1, 0.2);
  EXPECT_NEAR(equinox.azimuth_degrees, 319.9, 0.5);
}

// At solar noon the sun is due north from New Zealand, by definition of solar
// noon and of being south of the tropics. This is the case that catches an
// azimuth convention written back to front.
TEST(SolarTest, AtSolarNoonTheSunIsDueNorthFromNewZealand) {
  for (const int day : {1, 80, 172, 264, 355}) {
    const SunPosition noon = sun_position(-43.64, day, 12.0);
    EXPECT_NEAR(noon.azimuth_degrees, 0.0, 0.01) << "day " << day;
    EXPECT_TRUE(noon.is_up()) << "day " << day;
  }
}

// Morning is east of north, afternoon is west of it. Reversed, every hill in
// the three-dimensional view would be lit from the wrong side and still look
// entirely plausible.
TEST(SolarTest, TheSunMovesEastToWestThroughTheDay) {
  const SunPosition morning = sun_position(-43.64, 264, 9.0);
  const SunPosition afternoon = sun_position(-43.64, 264, 15.0);

  EXPECT_LT(morning.azimuth_degrees, 90.0) << "morning sun should be east of north";
  EXPECT_GT(afternoon.azimuth_degrees, 270.0) << "afternoon sun should be west of north";
}

// The sun sets. A view that drew a light below the horizon would light the
// underside of the ground.
TEST(SolarTest, TheSunIsDownAtMidnightAndUpAtMidday) {
  EXPECT_FALSE(sun_position(-43.64, 172, 0.0).is_up());
  EXPECT_TRUE(sun_position(-43.64, 172, 12.0).is_up());
}

// The clearness index against what the sky can deliver.
TEST(SolarTest, ClearnessIsMeasuredAgainstWhatTheSkyCouldDeliver) {
  const double ra = extraterrestrial_radiation_mj(-43.64, 355);
  ASSERT_GT(ra, 0.0);

  EXPECT_DOUBLE_EQ(clearness_index(ra, ra), 1.0);
  EXPECT_DOUBLE_EQ(clearness_index(0.0, ra), 0.0);
  EXPECT_DOUBLE_EQ(clearness_index(-5.0, ra), 0.0) << "negative radiation is not less than none";
  EXPECT_DOUBLE_EQ(clearness_index(10.0, 0.0), 0.0) << "a polar night divides by nothing";
}

// And that the reading of it lands where FAO-56 Eq. 35's coefficients put it:
// 0.75 is full sunshine, 0.25 is none of it direct.
TEST(SolarTest, ASkyIsReadFromTheAngstromEndpoints) {
  EXPECT_EQ(sky_from_clearness(0.75), SkyCondition::Clear);
  EXPECT_EQ(sky_from_clearness(0.70), SkyCondition::Clear);
  EXPECT_EQ(sky_from_clearness(0.50), SkyCondition::PartlyCloudy);
  EXPECT_EQ(sky_from_clearness(0.25), SkyCondition::Overcast);
  EXPECT_EQ(sky_from_clearness(0.10), SkyCondition::Overcast);
}

}  // namespace paddock::core
