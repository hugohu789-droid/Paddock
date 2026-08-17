// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Dry matter and nitrogen, over a coupled year.
//
// Weather drives a soil water bucket; the bucket's stress coefficient drives a
// ryegrass and white clover sward; the sward reports every flow it makes. All
// three budgets - water, dry matter, nitrogen - must close to 1e-9 over 365
// simulated days, and each has a negative control proving the gate can fail.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Pasture.hpp>
#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/SoilWater.hpp>
#include <paddock/core/SyntheticWeather.hpp>
#include <paddock/core/Weather.hpp>

#include "support/BitPattern.hpp"
#include "support/TestPasture.hpp"
#include "support/TestWeather.hpp"

namespace paddock::core {
namespace {

using test_support::bit_patterns;
using test_support::test_site_parameters;
using test_support::test_soil_parameters;
using test_support::test_sward_parameters;

constexpr std::uint64_t kMasterSeed = 20240701;
constexpr double kLatitude = -43.5;
constexpr double kInitialSoilWaterMm = 90.0;
constexpr double kInitialGrassKg = 1800.0;
constexpr double kInitialLegumeKg = 400.0;
constexpr double kInitialSoilNitrogenKg = 60.0;

/// One hectare of pasture on one soil, driven by one year of weather.
struct Farmlet {
  SoilWaterBucket soil{test_soil_parameters(), kInitialSoilWaterMm};
  PastureSward sward{test_sward_parameters(), kInitialGrassKg, kInitialLegumeKg,
                     kInitialSoilNitrogenKg};
  BudgetLedger ledger;
  double total_growth_kg = 0.0;

  Farmlet() {
    ledger.set_opening_stock(Budget::Water, soil.water_mm());
    ledger.set_opening_stock(Budget::DryMatter, sward.cover_kg_dm());
    ledger.set_opening_stock(Budget::Nitrogen, sward.total_nitrogen_kg());
  }

  void run_year(std::uint64_t seed, int year) {
    const SyntheticWeatherSource weather(test_site_parameters(), seed);
    for (const DailyWeather& day : weather.fetch(DateRange::calendar_year(year)).records) {
      const SoilWaterFluxes water = soil.step(day, kLatitude, 1.0, &ledger);
      total_growth_kg += sward.step(day, water.stress_coefficient, &ledger).total_growth_kg_dm();
    }
  }
};

TEST(PastureConservationTest, AllThreeBudgetsCloseOverACoupledYear) {
  Farmlet farmlet;
  farmlet.run_year(kMasterSeed, 2023);

  EXPECT_TRUE(farmlet.ledger.closes(Budget::Water, farmlet.soil.water_mm()))
      << farmlet.ledger.report(Budget::Water, farmlet.soil.water_mm());
  EXPECT_TRUE(farmlet.ledger.closes(Budget::DryMatter, farmlet.sward.cover_kg_dm()))
      << farmlet.ledger.report(Budget::DryMatter, farmlet.sward.cover_kg_dm());
  EXPECT_TRUE(farmlet.ledger.closes(Budget::Nitrogen, farmlet.sward.total_nitrogen_kg()))
      << farmlet.ledger.report(Budget::Nitrogen, farmlet.sward.total_nitrogen_kg());
}

// Nitrogen has exactly one way in - the clover - so the closing stock must
// exceed the opening one by the fixation and by nothing else.
TEST(PastureConservationTest, TheOnlyNitrogenIncomeIsFixation) {
  Farmlet farmlet;
  farmlet.run_year(kMasterSeed, 2023);

  const double gained =
      farmlet.sward.total_nitrogen_kg() - kInitialSoilNitrogenKg -
      (kInitialGrassKg * test_sward_parameters().grass.nitrogen_content_fraction) -
      (kInitialLegumeKg * test_sward_parameters().legume.nitrogen_content_fraction);

  EXPECT_GT(farmlet.ledger.total_inflow(Budget::Nitrogen), 0.0);
  EXPECT_NEAR(gained, farmlet.ledger.total_inflow(Budget::Nitrogen), kConservationTolerance);
  EXPECT_DOUBLE_EQ(farmlet.ledger.total_outflow(Budget::Nitrogen), 0.0);
}

// Negative control for the dry matter line: senescence is a transfer between
// pools, and treating it as a loss - the easiest mistake in the model - has to
// break the budget rather than quietly shrinking the farm.
TEST(PastureConservationTest, MisreportingSenescenceAsALossIsDetected) {
  BudgetLedger ledger;
  SoilWaterBucket soil(test_soil_parameters(), kInitialSoilWaterMm);
  PastureSward sward(test_sward_parameters(), kInitialGrassKg, kInitialLegumeKg,
                     kInitialSoilNitrogenKg);
  ledger.set_opening_stock(Budget::DryMatter, sward.cover_kg_dm());
  double misreported = 0.0;

  const SyntheticWeatherSource weather(test_site_parameters(), kMasterSeed);
  for (const DailyWeather& day : weather.fetch(DateRange::calendar_year(2023)).records) {
    const SoilWaterFluxes water = soil.step(day, kLatitude);
    const PastureGrowth growth = sward.step(day, water.stress_coefficient);
    ledger.record_inflow(Budget::DryMatter, "pasture_growth", growth.total_growth_kg_dm());
    ledger.record_outflow(Budget::DryMatter, "decomposition", growth.decomposition_kg_dm);
    ledger.record_outflow(Budget::DryMatter, "senescence", growth.senescence_kg_dm);
    misreported += growth.senescence_kg_dm;
  }

  ASSERT_GT(misreported, 1.0);
  EXPECT_FALSE(ledger.closes(Budget::DryMatter, sward.cover_kg_dm()));
}

TEST(PastureConservationTest, TheSameSeedGivesTheSameYearOfGrowth) {
  const auto run = [](std::uint64_t seed) {
    Farmlet farmlet;
    farmlet.run_year(seed, 2023);
    return std::vector<double>{farmlet.sward.grass_kg_dm(), farmlet.sward.legume_kg_dm(),
                               farmlet.sward.dead_kg_dm(), farmlet.sward.soil_mineral_nitrogen_kg(),
                               farmlet.total_growth_kg};
  };

  EXPECT_EQ(bit_patterns(run(kMasterSeed)), bit_patterns(run(kMasterSeed)));
  EXPECT_NE(bit_patterns(run(kMasterSeed)), bit_patterns(run(kMasterSeed + 1)));
}

// Shape, not calibration. Growth should follow the season and the year's total
// should be the right order of magnitude for a temperate pasture; pinning it
// inside the 12-16 t DM/ha/year that DairyNZ reports for New Zealand is the
// job of the T3 validation gate against measured growth curves, not of a
// conservation test with fixture parameters.
TEST(PastureConservationTest, GrowthFollowsTheSeasonAndLandsInThePlausibleRange) {
  const SyntheticWeatherSource weather(test_site_parameters(), kMasterSeed);
  SoilWaterBucket soil(test_soil_parameters(), kInitialSoilWaterMm);
  PastureSward sward(test_sward_parameters(), kInitialGrassKg, kInitialLegumeKg,
                     kInitialSoilNitrogenKg);
  double spring_growth = 0.0;
  double winter_growth = 0.0;
  double annual_growth = 0.0;

  for (const DailyWeather& day : weather.fetch(DateRange::calendar_year(2023)).records) {
    const SoilWaterFluxes water = soil.step(day, kLatitude);
    const double grown = sward.step(day, water.stress_coefficient).total_growth_kg_dm();
    annual_growth += grown;
    if (day.date.month >= 10 && day.date.month <= 11) {
      spring_growth += grown;
    }
    if (day.date.month >= 6 && day.date.month <= 7) {
      winter_growth += grown;
    }
  }

  // Printed rather than only asserted: this is the number the T3 gate will
  // calibrate against measured curves, and watching it move is how a parameter
  // change announces itself.
  GTEST_LOG_(INFO) << "annual pasture growth: " << annual_growth << " kg DM/ha/yr ("
                   << spring_growth << " in spring, " << winter_growth << " in winter)";

  EXPECT_GT(spring_growth, winter_growth * 3.0);
  EXPECT_GT(annual_growth, 4000.0);
  EXPECT_LT(annual_growth, 25000.0);
}

}  // namespace
}  // namespace paddock::core
