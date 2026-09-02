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
/// **This file was written with the wrong target and corrected within the
/// hour, by reading one more table in the same paper.** It first took a ewe as
/// 1.35 SU, a figure asserted without a source in the economics file to make
/// 417 ewes on 80 ha look like Beef + Lamb's Class 6. Parker's Table 1 gives a
/// ewe as 1.0 SU in the Meat and Wool Board's Economic Service figures - the
/// survey that became Beef + Lamb's - and in MAF's. The modelled flock is 417
/// ewes at 55 kg rearing 105%, which is Parker's base ewe almost exactly.
///
/// So the demand is 2,867 kg DM/ha, not 3,850, and the animals eat 85% of it
/// rather than 63%.
///
/// **Which moves the diagnosis again.** Production is inside Winchmore's
/// measured band, intake per stock unit is not far off, and what is actually
/// low is the stocking rate: 5.2 SU/ha against the class average of 7.74. The
/// farm is understocked by about a third, and that is most of why utilisation
/// reads 36%. Intake capacity would not fix it - a cap and an availability
/// response both move intake down, not up.

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

  // **A band, not a target.** 85% of a stock-unit rating is close enough that
  // the remaining distance could be the model, could be 550 against the 520
  // also in common use for the same unit, or could be that a real ewe's year is
  // not this ewe's year. It is not where a third of a farm's feed went.
  const double share = eaten / target;
  EXPECT_GT(share, 0.7) << "the flock eats " << eaten << " kg DM/ha against a stock-unit demand of "
                        << target
                        << ". Under 70% would be a real intake fault rather than the ordinary "
                           "distance between a model and a rule of thumb";
  EXPECT_LT(share, 1.0) << "the flock now eats " << eaten << " of " << target
                        << " kg DM/ha, at or over its stock-unit rating. Not a failure, but this "
                           "bound has stopped describing the model - work out what changed";
}

}  // namespace
}  // namespace paddock::config
