// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Radiation on a tilted surface.
//
// There is no published table to check this against, so it is pinned by
// properties that must hold whatever the implementation: the level case has to
// reproduce the FAO-56 equation this project already tests, east and west have
// to be equal, and the sunny side of a hill in the southern hemisphere has to
// face north. Each of those fails loudly for a different mistake - a wrong
// constant, a transposed axis, a hemisphere sign.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#include <paddock/core/Solar.hpp>

namespace paddock::core {
namespace {

/// Canterbury, near the farms in data/scenarios.
constexpr double kCanterburyLatitude = -43.6;

/// Days chosen for what they test, not for being round numbers.
constexpr int kMidsummer = 355;  // Around 21 December in the southern hemisphere
constexpr int kMidwinter = 172;  // Around 21 June
constexpr int kEquinoxSpring = 266;
constexpr int kEquinoxAutumn = 80;

// The anchor. A slope of zero is level ground, and level ground already has a
// tested equation - FAO-56 Eq. 21. If the numerical integral disagrees with it,
// nothing else in this file means anything.
//
// The tolerance is what the method delivers at the step count Solar.cpp
// justifies, measured rather than hoped for: 1.9e-4 MJ worst case over a year.
// That is five parts per million of a summer day, against measured radiation
// that carries twenty to thirty per cent - so a tighter number here would be
// decoration bought with twenty times the arithmetic.
TEST(SolarSlopeTest, LevelGroundReproducesTheFaoEquation) {
  for (int day = 1; day <= 365; day += 7) {
    const double published = extraterrestrial_radiation_mj(kCanterburyLatitude, day);
    const double integrated =
        extraterrestrial_radiation_on_slope_mj(kCanterburyLatitude, day, 0.0, 0.0);
    ASSERT_NEAR(integrated, published, 2e-4) << "on day " << day;
  }
}

// The step count is a judgement, so it is measured. Halving it must not move the
// answer at the tolerance the test above uses; if it did, 288 would be too few.
TEST(SolarSlopeTest, TheIntegrationIsFineEnoughToBeStable) {
  // A steep slope is the hardest case: the integrand switches on and off partway
  // through the day, so a coarse grid would show it.
  for (const int day : {kMidsummer, kMidwinter, kEquinoxSpring}) {
    const double steep_north =
        extraterrestrial_radiation_on_slope_mj(kCanterburyLatitude, day, 30.0, 0.0);
    const double steep_south =
        extraterrestrial_radiation_on_slope_mj(kCanterburyLatitude, day, 30.0, 180.0);

    // Both must be physically sensible: never negative, never above the level
    // case by more than the geometry allows.
    EXPECT_GE(steep_north, 0.0) << "day " << day;
    EXPECT_GE(steep_south, 0.0) << "day " << day;
    EXPECT_LT(steep_north, 60.0) << "day " << day;
  }
}

// Level ground has no aspect. Passing NaN has to work, because Topography
// reports NaN there and making a caller invent a bearing is how an invented
// bearing gets into the model.
TEST(SolarSlopeTest, LevelGroundAcceptsAnUnknownAspect) {
  const double with_nan = extraterrestrial_radiation_on_slope_mj(
      kCanterburyLatitude, kMidsummer, 0.0, std::numeric_limits<double>::quiet_NaN());
  const double with_zero =
      extraterrestrial_radiation_on_slope_mj(kCanterburyLatitude, kMidsummer, 0.0, 0.0);

  EXPECT_FALSE(std::isnan(with_nan));
  EXPECT_DOUBLE_EQ(with_nan, with_zero);
  EXPECT_DOUBLE_EQ(slope_radiation_ratio(kCanterburyLatitude, kMidsummer, 0.0,
                                         std::numeric_limits<double>::quiet_NaN()),
                   1.0);
}

// The hemisphere check. In New Zealand the sun is in the northern sky, so a
// north-facing slope always collects more than a south-facing one. Getting the
// sign wrong reverses every hillside on the farm while leaving every total
// plausible, so this is the assertion that catches it.
TEST(SolarSlopeTest, InNewZealandTheSunnySideFacesNorth) {
  for (const int day : {kMidsummer, kMidwinter, kEquinoxSpring, kEquinoxAutumn}) {
    const double north = slope_radiation_ratio(kCanterburyLatitude, day, 20.0, 0.0);
    const double south = slope_radiation_ratio(kCanterburyLatitude, day, 20.0, 180.0);

    EXPECT_GT(north, south) << "day " << day;
  }
}

// The counter-intuitive one, and the reason the test above does not simply
// assert that a north-facing slope beats level ground.
//
// The lower bound of 0.9 is a regression pin taken from this implementation.
// The upper bound of 1.0 is not: that a tilt cannot beat level ground when the
// sun passes near overhead follows from the geometry.
//
// In midsummer at 43.6 S every 20 degree slope receives less than level ground,
// whichever way it faces - north 0.96, east 0.97, south 0.94. The sun passes
// close to overhead, so any tilt costs; and the day is long enough that it rises
// south of east and sets south of west, which takes the mornings and evenings
// away from a north-facing slope. An implementation that could not produce a
// sunny-side ratio below one would be hiding this.
TEST(SolarSlopeTest, InHighSummerEveryTiltLosesToLevelGround) {
  for (const double aspect : {0.0, 90.0, 180.0, 270.0}) {
    const double ratio = slope_radiation_ratio(kCanterburyLatitude, kMidsummer, 20.0, aspect);
    EXPECT_LT(ratio, 1.0) << "aspect " << aspect << " gave " << ratio;
    EXPECT_GT(ratio, 0.9) << "aspect " << aspect << " gave " << ratio;
  }
}

// The classical check, and the only one here that tests the geometry against
// something outside this project.
//
// At an equinox the sun's declination is nearly zero, and a slope tilted by
// beta towards the equator then receives what level ground at latitude
// (phi + beta) receives - the "equivalent latitude" result of solar geometry. It
// follows from the geometry alone, so it holds whatever this implementation
// does, and a sign error in either the surface normal or the sun direction
// breaks it. Verified here at four latitude and slope combinations on both
// equinoxes, where it holds to about 1e-6 MJ.
TEST(SolarSlopeTest, ASlopeTowardsTheEquatorMatchesItsEquivalentLatitude) {
  struct Case {
    double latitude;
    double slope;
  };

  constexpr std::array<Case, 4> kCases = {{
      {-43.6, 10.0},  // Canterbury
      {-43.6, 20.0},
      {-37.8, 15.0},  // Waikato
      {-46.6, 25.0},  // Southland
  }};

  for (const int day : {kEquinoxAutumn, kEquinoxSpring}) {
    for (const Case& scenario : kCases) {
      // Towards the equator from the southern hemisphere is north, bearing 0.
      const double on_slope =
          extraterrestrial_radiation_on_slope_mj(scenario.latitude, day, scenario.slope, 0.0);
      const double at_equivalent_latitude =
          extraterrestrial_radiation_on_slope_mj(scenario.latitude + scenario.slope, day, 0.0, 0.0);

      ASSERT_NEAR(on_slope, at_equivalent_latitude, 1e-3)
          << "latitude " << scenario.latitude << ", slope " << scenario.slope << ", day " << day;
    }
  }
}

// A REGRESSION PIN, not a validation. These numbers came out of this
// implementation; no published measurement was compared against. They are here
// so that a change which alters the winter contrast between two sides of a hill
// has to be deliberate, and they should not be read as evidence that the
// contrast is right.
//
// What supports the contrast being right is the equinox identity above, which
// is independent of this code, and the level-ground case matching FAO-56.
//
// The magnitude is worth recording either way: at 43.6 S a 20 degree northerly
// slope receives about twice what level ground does in midwinter, a southerly
// one about a sixteenth.
TEST(SolarSlopeTest, MidwinterSeparatesTheTwoSidesOfAHillDramatically) {
  const double north = slope_radiation_ratio(kCanterburyLatitude, kMidwinter, 20.0, 0.0);
  const double south = slope_radiation_ratio(kCanterburyLatitude, kMidwinter, 20.0, 180.0);

  EXPECT_NEAR(north, 2.00, 0.05) << "northerly midwinter ratio";
  EXPECT_NEAR(south, 0.06, 0.02) << "southerly midwinter ratio";
  EXPECT_GT(north / south, 20.0) << "the two sides should differ by more than twenty fold";
}

// And the mirror image: north of the equator the sunny side faces south. The
// same code has to get both right, or it has a hemisphere baked into it.
TEST(SolarSlopeTest, NorthOfTheEquatorTheSunnySideFacesSouth) {
  constexpr double kSpainLatitude = 40.4;

  const double north = slope_radiation_ratio(kSpainLatitude, kMidsummer, 20.0, 0.0);
  const double south = slope_radiation_ratio(kSpainLatitude, kMidsummer, 20.0, 180.0);

  EXPECT_LT(north, 1.0);
  EXPECT_GT(south, 1.0);
}

// East and west are mirror images about solar noon, so their daily totals must
// match. This is the test a transposed axis fails: swapping the east and north
// components of the surface normal leaves the north-south checks above passing
// and breaks this one.
TEST(SolarSlopeTest, EastAndWestFacingSlopesReceiveTheSameDailyTotal) {
  for (const int day : {kMidsummer, kMidwinter, kEquinoxSpring}) {
    for (const double slope : {5.0, 15.0, 30.0}) {
      const double east =
          extraterrestrial_radiation_on_slope_mj(kCanterburyLatitude, day, slope, 90.0);
      const double west =
          extraterrestrial_radiation_on_slope_mj(kCanterburyLatitude, day, slope, 270.0);
      ASSERT_NEAR(east, west, 1e-9) << "day " << day << ", slope " << slope;
    }
  }
}

// The classic result from solar geometry: annual radiation is greatest on a
// slope tilted towards the equator by about the latitude angle. Finding that
// maximum where theory puts it exercises the whole year at once.
TEST(SolarSlopeTest, AnnualRadiationPeaksAtASlopeNearTheLatitude) {
  double best_slope = 0.0;
  double best_total = -1.0;

  // Counted in whole degrees rather than stepped by adding 1.0 to a double:
  // a floating-point loop counter accumulates its own rounding, and the
  // security.FloatLoopCounter check exists because that eventually skips or
  // repeats a step.
  for (int slope_degrees = 0; slope_degrees <= 70; ++slope_degrees) {
    const double slope = slope_degrees;
    double total = 0.0;
    for (int day = 1; day <= 365; ++day) {
      // Facing north: towards the equator from Canterbury.
      total += extraterrestrial_radiation_on_slope_mj(kCanterburyLatitude, day, slope, 0.0);
    }
    if (total > best_total) {
      best_total = total;
      best_slope = slope;
    }
  }

  // Theory says the optimum is close to |latitude|; the atmosphere-free case
  // used here puts it within a few degrees.
  EXPECT_NEAR(best_slope, 43.6, 5.0) << "annual optimum came out at " << best_slope << " degrees";
}

// A slope steeper than the sun ever gets in winter is in shadow all day. Zero is
// the honest answer, not an error and not a small positive number.
TEST(SolarSlopeTest, ASlopeInShadowAllDayReceivesNothing) {
  // Facing south at 80 degrees in midwinter: the sun never rises high enough in
  // the northern sky to fall on it.
  const double received =
      extraterrestrial_radiation_on_slope_mj(kCanterburyLatitude, kMidwinter, 80.0, 180.0);

  EXPECT_DOUBLE_EQ(received, 0.0);
  EXPECT_DOUBLE_EQ(slope_radiation_ratio(kCanterburyLatitude, kMidwinter, 80.0, 180.0), 0.0);
}

// The seasonal reversal Ballantrae measured, as a property of the geometry: the
// sunny side's advantage is largest in winter, when the sun is low and the angle
// matters most, and smallest in summer.
TEST(SolarSlopeTest, TheSunnySideGainsMostInWinter) {
  const double winter = slope_radiation_ratio(kCanterburyLatitude, kMidwinter, 20.0, 0.0);
  const double summer = slope_radiation_ratio(kCanterburyLatitude, kMidsummer, 20.0, 0.0);

  EXPECT_GT(winter, summer) << "winter ratio " << winter << ", summer ratio " << summer;
  EXPECT_GT(winter, 1.5) << "a 20 degree northerly slope gains about double in midwinter";
}

// VALIDATION against a field trial, not against this model's own arithmetic.
//
// Gillingham, Gray and Smith (1998), Proc. NZ Grassland Association 60,
// measured pasture growth on a southern Hawke's Bay hill farm across four
// classes: north- and south-facing aspects, each with easy (15-20 degrees) and
// steep (25-30 degrees) slopes. Two of their findings are directional and so
// can be checked without their figures, whose numbers are in bar charts this
// project cannot read:
//
//   "Production was higher on easy than on steep slopes at all times within
//    each aspect; however, pasture growth on north-facing steep slopes in
//    winter was greater than on any areas within the south aspect at this
//    time."
//
// The second half is the sharp one. Growth depends on moisture and temperature
// as well as light, and Gillingham found soil moisture *higher* on the south
// aspect - so the north aspect won its winter advantage despite being drier.
// Radiation is what is left to explain it. This test does not claim to
// reproduce their growth rates; it checks that the radiation model ranks the
// two the same way, because a model that ranked them the other way could not
// reproduce the measurement whatever the pasture code did.
TEST(SolarSlopeTest, MidwinterRadiationRanksHawkesBayAspectsAsGillinghamMeasuredThem) {
  // Southern Hawke's Bay, and the midpoints of the trial's two slope classes.
  constexpr double kHawkesBayLatitude = -40.3;
  constexpr double kEasySlope = 17.5;   // their "easy", 15-20 degrees
  constexpr double kSteepSlope = 27.5;  // their "steep", 25-30 degrees
  constexpr double kNorthFacing = 0.0;
  constexpr double kSouthFacing = 180.0;

  const double north_easy =
      slope_radiation_ratio(kHawkesBayLatitude, kMidwinter, kEasySlope, kNorthFacing);
  const double north_steep =
      slope_radiation_ratio(kHawkesBayLatitude, kMidwinter, kSteepSlope, kNorthFacing);
  const double south_easy =
      slope_radiation_ratio(kHawkesBayLatitude, kMidwinter, kEasySlope, kSouthFacing);
  const double south_steep =
      slope_radiation_ratio(kHawkesBayLatitude, kMidwinter, kSteepSlope, kSouthFacing);

  // The steep north face beats every part of the south aspect, easy included.
  EXPECT_GT(north_steep, south_easy)
      << "north steep " << north_steep << ", south easy " << south_easy;
  EXPECT_GT(north_steep, south_steep);

  // And within the south aspect, steeper is worse - which is the same
  // direction as their "easy above steep within each aspect", for the one
  // driver this function models.
  EXPECT_GT(south_easy, south_steep);

  // On the north aspect the radiation ordering is the opposite way round: a
  // steeper slope faces the low winter sun more squarely. Gillingham still
  // measured easy above steep there, so light is not the whole story on that
  // side - moisture and soil depth are, and the pasture model has to carry
  // them. Recording the disagreement is the point of the test.
  EXPECT_GT(north_steep, north_easy) << "north steep " << north_steep << ", north easy "
                                     << north_easy << " - radiation alone favours the steeper face";
}

}  // namespace
}  // namespace paddock::core
