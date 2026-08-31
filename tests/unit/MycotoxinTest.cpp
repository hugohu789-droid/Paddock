// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// The mycotoxin mechanics, on a parameter set of this suite's own.
///
/// Assertions that come from a publication name it, so a failure says which
/// figure stopped being reproduced rather than only which line moved. Nothing
/// here reads the shipped disease file: this suite links core alone, and the
/// tests that hold the file and the model together live where config is
/// available - see `FacialEczemaBehavesTheWayItsFileDescribes`.

#include <gtest/gtest.h>

#include <cmath>

#include <paddock/core/Mycotoxin.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {
namespace {

/// **Not the shipped disease.** A parameter set for exercising the mechanics,
/// which is all this suite can do: it links core only, and reading
/// data/diseases/facial-eczema.toml needs config. The tests that check the
/// shipped file are in the config and validation suites, where the file can
/// actually be loaded - `FacialEczemaBehavesTheWayItsFileDescribes` and
/// `FacialEczemaGeographyTest`.
///
/// The numbers below that come from publications are marked where they are
/// used. The two sporulation rates are deliberately left at the values this
/// suite was written against rather than tracking the file, because nothing
/// here asserts anything about their size - only that a rise rises and a decay
/// decays.
MycotoxinParameters a_mycotoxin() {
  MycotoxinParameters parameters;
  parameters.grass_minimum_temperature_c = 12.0;
  parameters.consecutive_nights = 4;
  parameters.rainfall_mm_per_48h = 4.0;
  parameters.rise_per_favourable_day = 1.9;
  parameters.decay_per_unfavourable_day = 0.75;
  parameters.background_spores_per_g = 2000.0;
  parameters.picograms_per_spore = 1.41;
  parameters.reactor_spore_days = 1'500'000.0;
  parameters.reactor_ggt_iu_per_l = 55.0;
  parameters.liver_injury_intercept = -2.96;
  parameters.liver_injury_ln_ggt_slope = 0.89;
  parameters.clinical_fraction_of_affected = 0.10;
  return parameters;
}

DailyWeather a_night(double minimum_c) {
  DailyWeather day;
  day.date = Date{2024, 3, 1};
  day.min_air_temperature_c = minimum_c;
  day.max_air_temperature_c = minimum_c + 8.0;
  return day;
}

TEST(MycotoxinTest, TheParameterSetIsValid) {
  EXPECT_EQ(a_mycotoxin().invalid_reason(), "");
}

// DairyNZ Technical Series (February 2012): favourable when overnight minimum
// grass temperature is at or above 12 C and humidity is high, given here as
// drizzly rain of 4-6 mm per 48 hours. Both conditions, not either.
TEST(MycotoxinTest, WarmAndDampTogetherFavourSporulation) {
  const MycotoxinParameters fe = a_mycotoxin();

  EXPECT_TRUE(night_favours_sporulation(a_night(12.0), 4.0, fe)) << "at the threshold, both met";
  EXPECT_TRUE(night_favours_sporulation(a_night(15.0), 9.0, fe)) << "well inside both";

  EXPECT_FALSE(night_favours_sporulation(a_night(11.9), 9.0, fe)) << "wet but a fraction too cold";
  EXPECT_FALSE(night_favours_sporulation(a_night(18.0), 3.9, fe)) << "warm but too dry";
  EXPECT_FALSE(night_favours_sporulation(a_night(8.0), 0.0, fe)) << "neither";
}

// "One or two small increases over several weeks, followed by a major rapid
// rise": the count must not move until the run of nights is long enough.
TEST(MycotoxinTest, TheCountRisesOnlyAfterAFullRunOfNights) {
  const MycotoxinParameters fe = a_mycotoxin();
  const double start = 10'000.0;

  for (int nights = 0; nights < fe.consecutive_nights; ++nights) {
    EXPECT_LT(next_spore_count(start, nights, fe), start)
        << nights << " favourable nights is not yet a run, so the count should be decaying";
  }

  EXPECT_GT(next_spore_count(start, fe.consecutive_nights, fe), start)
      << "the fourth night completes the run and the count rises";
}

// The fungus lives on dead litter year round, so a count of exactly zero would
// be a claim nobody has made.
TEST(MycotoxinTest, TheCountDecaysToTheBackgroundAndNoFurther) {
  const MycotoxinParameters fe = a_mycotoxin();

  double count = 200'000.0;
  for (int day = 0; day < 200; ++day) {
    count = next_spore_count(count, 0, fe);
  }
  EXPECT_DOUBLE_EQ(count, fe.background_spores_per_g);
}

// Fitzgerald, Collin and Towers (1998): 113 ng/g grass at 80,000 spores/g in
// untreated pasture. That measurement is what the per-spore figure is derived
// from, so the model must reproduce it.
TEST(MycotoxinTest, TheToxinLoadReproducesTheMeasuredPasture) {
  const MycotoxinParameters fe = a_mycotoxin();
  EXPECT_NEAR(toxin_ng_per_g(80'000.0, fe), 113.0, 1.0)
      << "Fitzgerald et al. (1998) measured 113 ng/g at this count";

  // And the atoxigenic plots in the same trial: 26 ng/g at 50,000 spores/g,
  // which is 0.52 pg per spore. Same count, very different toxin - the reason
  // the data file carries both figures.
  MycotoxinParameters atoxigenic = fe;
  atoxigenic.picograms_per_spore = 0.52;
  EXPECT_NEAR(toxin_ng_per_g(50'000.0, atoxigenic), 26.0, 1.0);
}

// Morris, Smith and Hickey (2002): LIS = -2.96 + 0.89 ln(GGT).
TEST(MycotoxinTest, LiverInjuryFollowsThePublishedRegression) {
  const MycotoxinParameters fe = a_mycotoxin();

  // Worked by hand from the published coefficients rather than from the code.
  const double at_250 = -2.96 + (0.89 * std::log(250.0));
  EXPECT_NEAR(liver_injury_score(250.0, fe), at_250, 1e-9);

  const double at_6001 = -2.96 + (0.89 * std::log(6001.0));
  EXPECT_NEAR(liver_injury_score(6001.0, fe), at_6001, 1e-9)
      << "the highest individual GGT Cuttance et al. (2021) recorded";

  // The regression turns negative below about 27 IU/L, which is a healthy
  // animal and not a liver in credit.
  EXPECT_DOUBLE_EQ(liver_injury_score(20.0, fe), 0.0);
  EXPECT_DOUBLE_EQ(liver_injury_score(0.0, fe), 0.0);
}

// DairyNZ: "only about 10% of affected animals show clinical signs, for every
// clinical case there will be 10 cows with sub-clinical FE". A mob can be well
// over the reactor threshold and still show a farmer nothing.
TEST(MycotoxinTest, MostOfAnAffectedMobShowsNothing) {
  const MycotoxinParameters fe = a_mycotoxin();

  EXPECT_EQ(clinically_affected(400, 300.0, fe), 40) << "one in ten of a big mob";
  EXPECT_EQ(clinically_affected(400, 10.0, fe), 0) << "below the reactor GGT, nobody is affected";

  // The case that matters most: a small mob over the threshold, where the
  // arithmetic says nobody visible even though every animal is taking damage.
  EXPECT_EQ(clinically_affected(9, 300.0, fe), 0)
      << "nine ewes over the reactor threshold and not one visible case";
  EXPECT_GT(liver_injury_score(300.0, fe), 0.0) << "while the liver damage is real";
}

// **The fitted step, checked against what it was fitted to.** This is not a
// validation - it cannot be, because the calibration and the check share a
// source. It is here so that a change to the index has to be deliberate.
TEST(MycotoxinTest, TheFittedIndexReproducesTheFieldThresholds) {
  const MycotoxinParameters fe = a_mycotoxin();

  // Field guidance: 100,000 spores/g is dangerous, and clinical signs follow
  // intake by 10 to 18 days. A fortnight there should reach the reactor GGT.
  const double dangerous_fortnight = 100'000.0 * 15.0;
  EXPECT_GE(ggt_from_exposure(dangerous_fortnight, fe), fe.reactor_ggt_iu_per_l);

  // And 20,000 - "damaging over an extended period" - should not get there in
  // the same fortnight.
  EXPECT_LT(ggt_from_exposure(20'000.0 * 15.0, fe), fe.reactor_ggt_iu_per_l);

  // But should over a season, which is what "extended period" means.
  EXPECT_GE(ggt_from_exposure(20'000.0 * 90.0, fe), fe.reactor_ggt_iu_per_l);

  EXPECT_DOUBLE_EQ(ggt_from_exposure(0.0, fe), 0.0);
}

}  // namespace
}  // namespace paddock::core
