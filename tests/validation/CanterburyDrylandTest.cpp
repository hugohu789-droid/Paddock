// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// What a rain-fed Canterbury farm grows, against what Winchmore measured.
///
/// Caveat E7 recorded that the pasture growth parameters were fixtures rather
/// than a calibration, and E8 that no unfertilised Canterbury reference series
/// had been found to calibrate them against. One exists: the Winchmore
/// irrigation trial ran four dryland replicates of sheep-grazed ryegrass and
/// white clover from 1951 to 2018, and `data/calibration/`
/// `canterbury-dryland-pasture.csv` carries its figures and their provenance.
///
/// **One parameter was fitted and one quantity is an independent check.**
/// Radiation use efficiency was scaled to reproduce Winchmore's dryland water
/// use efficiency of 12.3 kg DM/ha/mm - so asserting that efficiency here would
/// be asserting the fit. Annual production was not fitted to anything, and is
/// the test that could fail.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/SnapshotWeather.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::config {
namespace {

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

ScenarioBundle year_of(int starting_year) {
  ScenarioBundle bundle =
      tests::load_on_flat_ground(std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf");

  core::SnapshotWeatherSource::Options options;
  options.path = std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf/weather-2015-2025.csv";
  options.dataset = "open-meteo";
  options.licence = "CC BY 4.0";
  bundle.weather = std::make_shared<core::SnapshotWeatherSource>(options);
  bundle.range =
      core::DateRange{core::Date{starting_year, 7, 1}, core::Date{starting_year + 1, 6, 30}};
  return bundle;
}

struct YearOfPasture {
  double grown_kg_dm_per_ha = 0.0;
  double evapotranspiration_mm = 0.0;
  double rainfall_mm = 0.0;

  [[nodiscard]] double water_use_efficiency() const {
    return evapotranspiration_mm > 0.0 ? grown_kg_dm_per_ha / evapotranspiration_mm : 0.0;
  }
};

YearOfPasture pasture_year(int starting_year) {
  const ScenarioBundle bundle = year_of(starting_year);
  const RunSummary run =
      run_managed_scenario(bundle, bundle.management.value(), pasture_diet(), "dryland");

  YearOfPasture year;
  for (const core::ProcessEntry& entry : run.ledger.entries(core::Budget::DryMatter)) {
    if (entry.process == "pasture_growth") {
      year.grown_kg_dm_per_ha = entry.inflow;
    }
  }
  for (const core::ProcessEntry& entry : run.ledger.entries(core::Budget::Water)) {
    if (entry.process == "evapotranspiration") {
      year.evapotranspiration_mm = entry.outflow;
    }
    if (entry.process == "rainfall") {
      year.rainfall_mm = entry.inflow;
    }
  }
  return year;
}

/// Winchmore's dryland treatment, measured. 25 farm years, 1960-61 to 1984-85,
/// out of `data/calibration/winchmore-annual-production.csv`.
///
/// **These replaced figures derived from reviews of the same trial**, and the
/// derived ones were narrower than the truth in a way that mattered: 5.5 to 6.5
/// t DM/ha, from a review saying irrigation "roughly doubles" production. The
/// measured mean is 6,442 - at the very top of that band - and the measured
/// range is 3,904 to 9,845, which is two and a half times wide. A dryland
/// Canterbury year is a distribution, not a number, and a band built on the
/// mean alone would fail a model for having weather.
constexpr double kWinchmoreDrylandMean = 6'442.0;
constexpr double kWinchmoreDrylandLowest = 3'904.0;
constexpr double kWinchmoreDrylandHighest = 9'845.0;

/// Lincoln's ten recorded years, which is every year the bundle's weather has.
std::vector<YearOfPasture> the_decade() {
  std::vector<YearOfPasture> years;
  for (int start = 2015; start <= 2024; ++start) {
    years.push_back(pasture_year(start));
  }
  return years;
}

// **Every year inside the measured range.** Not the mean - a single year has no
// business matching a 25-year mean - but inside what the trial actually saw.
TEST(CanterburyDrylandTest, EveryYearSitsInsideWhatWinchmoreMeasured) {
  for (const YearOfPasture& year : the_decade()) {
    EXPECT_GT(year.grown_kg_dm_per_ha, kWinchmoreDrylandLowest * 0.9)
        << "grew " << year.grown_kg_dm_per_ha << " kg DM/ha on " << year.rainfall_mm
        << " mm, below anything Winchmore's dryland treatment did in 25 years";
    EXPECT_LT(year.grown_kg_dm_per_ha, kWinchmoreDrylandHighest * 1.1)
        << "grew " << year.grown_kg_dm_per_ha << " kg DM/ha on " << year.rainfall_mm
        << " mm, above anything it did - and Winchmore is 745 mm against this "
           "farm's rainfall";
  }
}

// **The decade's mean against the trial's**, which is the comparison a single
// year cannot make. Both are rain-fed Canterbury ryegrass and clover on a stony
// soil; the differences are stated rather than corrected for.
TEST(CanterburyDrylandTest, TheDecadeAveragesWhatADrylandCanterburyFarmAverages) {
  const std::vector<YearOfPasture> years = the_decade();
  ASSERT_EQ(years.size(), 10U);

  double grown = 0.0;
  double rain = 0.0;
  for (const YearOfPasture& year : years) {
    grown += year.grown_kg_dm_per_ha;
    rain += year.rainfall_mm;
  }
  grown /= static_cast<double>(years.size());
  rain /= static_cast<double>(years.size());

  // **A third either way**, and the band is wide on purpose. Winchmore is
  // fertilised with phosphorus where this farm applies none, its rainfall is
  // its own, and its 25 years are not these 10. What the comparison can settle
  // is whether the model is producing a Canterbury dryland quantity at all -
  // it read 11.4 t DM/ha before it was calibrated, which is an irrigated farm.
  EXPECT_GT(grown, kWinchmoreDrylandMean * 0.67)
      << "the decade averaged " << grown << " kg DM/ha on " << rain << " mm against Winchmore's "
      << kWinchmoreDrylandMean << " on 745";
  EXPECT_LT(grown, kWinchmoreDrylandMean * 1.33)
      << "the decade averaged " << grown << " kg DM/ha on " << rain << " mm against Winchmore's "
      << kWinchmoreDrylandMean << " on 745";

  GTEST_LOG_(INFO) << "decade mean " << grown << " kg DM/ha at " << rain
                   << " mm; Winchmore dryland " << kWinchmoreDrylandMean << " at 745 mm";
}

// **The spread, which is the half the derived band got wrong.** Winchmore's
// dryland years run from 3,904 to 9,845 - a factor of 2.5 - because a rain-fed
// farm's year is mostly its weather. A model whose decade was flat would be
// describing an irrigated farm however well its mean landed.
TEST(CanterburyDrylandTest, TheDecadeSwingsTheWayARainFedFarmSwings) {
  const std::vector<YearOfPasture> years = the_decade();

  double lowest = years.front().grown_kg_dm_per_ha;
  double highest = lowest;
  for (const YearOfPasture& year : years) {
    lowest = std::min(lowest, year.grown_kg_dm_per_ha);
    highest = std::max(highest, year.grown_kg_dm_per_ha);
  }

  const double measured_spread = kWinchmoreDrylandHighest / kWinchmoreDrylandLowest;
  const double modelled_spread = highest / lowest;

  EXPECT_GT(modelled_spread, 1.4) << "ten years spanning " << lowest << " to " << highest
                                  << " kg DM/ha is a farm whose weather does not reach it";
  EXPECT_LT(modelled_spread, measured_spread * 1.5)
      << "and " << modelled_spread << " times is wider than the 2.5 Winchmore measured";
}

// **Water use efficiency is what was fitted, so this checks the fit took**
// rather than checking the model. Martin et al. (2006): 12.3 kg DM/ha/mm for
// Canterbury dryland, 20 for irrigated ryegrass and clover.
TEST(CanterburyDrylandTest, WaterUseEfficiencyIsNearTheDrylandFigureItWasFittedTo) {
  const double dry = pasture_year(2015).water_use_efficiency();
  const double wet = pasture_year(2024).water_use_efficiency();

  EXPECT_GT(dry, 8.0);
  EXPECT_LT(dry, 16.0);
  EXPECT_GT(wet, 10.0);
  EXPECT_LT(wet, 18.0);

  EXPECT_LT((dry + wet) / 2.0, 16.0)
      << "a mean this high is the irrigated figure, which is where this started";

  // **It varies between years now, and it did not before.** With the old
  // unsourced efficiency this was 19.8 in a 527 mm year and 19.8 in a 1,036 mm
  // one - identical to three figures, because growth and evapotranspiration are
  // scaled by the same water stress coefficient and their ratio never moved.
  EXPECT_GT(std::abs(wet - dry), 1.0)
      << "water use efficiency should differ between the driest year in ten and the wettest";
}

}  // namespace
}  // namespace paddock::config
