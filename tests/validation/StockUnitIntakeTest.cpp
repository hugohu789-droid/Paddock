// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// What the flock eats, against what its stock-unit rating says it should.
///
/// **This is the numerator of a ratio that was being read as a whole.** The
/// dashboard reported utilisation - eaten over grown - against a band of 65 to
/// 80% that cited nothing, and the farm sat far below it. A ratio out of band
/// says nothing about which half is wrong, and the honest way to find out is to
/// source the numerator on its own.
///
/// Parker (1998) defines the New Zealand stock unit as a 55 kg ewe weaning one
/// lamb and eating 550 kg DM a year - and, in the same paper, states the
/// standard unit as "550 kg DM of 10.5 MJ ME/kg DM per annum". That second
/// clause is what makes the comparison fair: 10.5 MJ ME/kg DM is exactly the
/// diet this model feeds, so the two figures are like for like without an
/// adjustment.
///
/// **What this test found.** Production is not the problem, at least not here:
/// the year grows 6,847 kg DM/ha against Winchmore's measured dryland mean of
/// 6,442, which is inside the band. The animals eat about 63% of what their
/// stock-unit rating implies. The gap is almost entirely intake, which reverses
/// the guess that it was half production and half intake.
///
/// The test holds the gap where it is rather than asserting the target. Closing
/// it needs intake capacity - a mob on a big offer eating more than a mob on a
/// small one - which this model does not have: demand is a target liveweight
/// gain, so a feast changes nothing.

#include <gtest/gtest.h>

#include <string>

#include <paddock/config/EconomicsConfig.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/SnapshotWeather.hpp>

#include "../support/CalibrationTable.hpp"
#include "../support/ShippedBundle.hpp"
#include "../support/ValueOf.hpp"

namespace paddock::config {
namespace {

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  // The energy density Parker's standard unit is defined at, which is also what
  // every other run in this project feeds.
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

/// Per hectare, the way the dashboard reports it: the run totals what the mobs
/// removed across the whole farm, so the area has to come back out of it.
double eaten_kg_dm_per_ha(const RunSummary& run, const ScenarioBundle& bundle) {
  const double hectares = bundle.grid.has_value()
                              ? static_cast<double>(bundle.grid->cols * bundle.grid->rows) *
                                    bundle.grid->cell_size_m * bundle.grid->cell_size_m / 10'000.0
                              : 0.0;
  return hectares > 0.0 ? run.eaten_kg_dm / hectares : 0.0;
}

/// The shipped price book, so the flock actually advances.
///
/// **Without the books nothing ages, lambs or is culled**, and the mob's head
/// count never changes - so intake reads 1,313 kg DM/ha rather than 2,434, and
/// a test asking whether the animals eat enough would be measuring a flock that
/// was never stepped. That is E49, and it is easy to walk into twice.
FarmBusiness a_business(const ScenarioBundle& bundle) {
  return business_from(
      bundle, load_economics(std::string(PADDOCK_DATA_DIR) + "/economics/canterbury-sheep.toml"));
}

test::CalibrationTable stock_units() {
  return test::CalibrationTable(std::string(PADDOCK_DATA_DIR) +
                                "/calibration/stock-unit-intake.csv");
}

// **The sourced figure is in the file and says what it says.** A test that
// hardcoded 550 would be asserting my memory of Parker rather than the row a
// reader can check.
TEST(StockUnitIntakeTest, TheStockUnitIsParkersFiveHundredAndFifty) {
  const test::CalibrationTable table = stock_units();

  bool found = false;
  for (std::size_t row = 0; row < table.size(); ++row) {
    if (table.text(row, "quantity") != "stock_unit_intake") {
      continue;
    }
    found = true;
    EXPECT_DOUBLE_EQ(table.number(row, "value"), 550.0);
    EXPECT_EQ(table.text(row, "units"), "kg DM/SU/yr");
    EXPECT_EQ(table.text(row, "status"), "direct");
  }
  EXPECT_TRUE(found) << "the stock unit row is what every figure below divides by";
}

// **The energy density the unit is defined at is the one the model feeds.**
// If this ever stops being true the comparison stops being like for like, and
// every intake conclusion in this file needs an adjustment it does not have.
TEST(StockUnitIntakeTest, TheStandardUnitsDietIsTheDietThisModelFeeds) {
  const test::CalibrationTable table = stock_units();
  for (std::size_t row = 0; row < table.size(); ++row) {
    if (table.text(row, "quantity") == "stock_unit_diet_quality") {
      EXPECT_DOUBLE_EQ(table.number(row, "value"),
                       pasture_diet().metabolisable_energy_mj_per_kg_dm);
    }
  }
}

// **Production is not the half that is wrong here.** Worth asserting because
// the first guess was that it was, and because if production drifts upward
// later the intake conclusion below stops being safe.
TEST(StockUnitIntakeTest, TheYearGrowsWhatACanterburyDrylandYearGrows) {
  const ScenarioBundle bundle = year_of(2023);
  const RunSummary run =
      run_managed_scenario(bundle, tests::value_of(bundle.management, "a [management] section"),
                           pasture_diet(), "su", a_business(bundle));

  double grown = 0.0;
  for (const core::ProcessEntry& entry : run.ledger.entries(core::Budget::DryMatter)) {
    if (entry.process == "pasture_growth") {
      grown = entry.inflow;
    }
  }

  // Winchmore's 25 measured dryland years: mean 6,442, range 3,904 to 9,845.
  EXPECT_GT(grown, 3'904.0) << "grew " << grown << ", under anything the trial measured";
  EXPECT_LT(grown, 9'845.0) << "grew " << grown << ", over anything the trial measured";
}

// **The animals eat about two thirds of their rating, and this holds it there.**
//
// Not asserted as a target - the model cannot reach it, because intake demand
// is a liveweight-gain target rather than an appetite, so a mob offered more
// does not take more. Asserted as a gap, so that it cannot quietly widen and so
// that closing it shows up as a failure here rather than as a number nobody
// looked at.
TEST(StockUnitIntakeTest, TheFlockEatsLessThanItsStockUnitRatingImplies) {
  const ScenarioBundle bundle = year_of(2023);
  const RunSummary run =
      run_managed_scenario(bundle, tests::value_of(bundle.management, "a [management] section"),
                           pasture_diet(), "su", a_business(bundle));

  const double eaten = eaten_kg_dm_per_ha(run, bundle);

  double target = 0.0;
  const test::CalibrationTable table = stock_units();
  for (std::size_t row = 0; row < table.size(); ++row) {
    if (table.text(row, "quantity") == "farm_intake_at_modelled_rate") {
      target = table.number(row, "value");
    }
  }
  ASSERT_GT(target, 0.0);

  const double share = eaten / target;
  EXPECT_GT(share, 0.5) << "the flock eats " << eaten << " kg DM/ha against a stock-unit demand of "
                        << target
                        << ". Under half would be a different and larger fault than the "
                           "one this test was written for";
  EXPECT_LT(share, 0.8) << "the flock now eats " << eaten << " of " << target
                        << " kg DM/ha, which is better than the 63% this was written at. If that "
                           "is intake capacity landing, raise this bound and say so; if it is a "
                           "run that changed for another reason, find out which";
}

}  // namespace
}  // namespace paddock::config
