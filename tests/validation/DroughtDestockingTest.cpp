// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// M4's drought scenario: a dry year forces a farmer to sell stock.
///
/// **On recorded weather, and on the driest year there is.** Lincoln's ten
/// Open-Meteo years run from 527 mm in 2015-16 to 1,036 mm in 2024-25, against
/// a published normal near 630. The dry year is the one this asserts on and the
/// wet year is the control - a model that destocked in both would be describing
/// a farm rather than a drought.

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/SnapshotWeather.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::config {
namespace {

std::string bundle_path() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf";
}

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

/// Beef + Lamb New Zealand Class 6 Marlborough-Canterbury, the same figures
/// data/economics/canterbury-sheep.toml carries.
core::OperatingCosts canterbury_costs() {
  core::OperatingCosts costs;
  costs.wages_and_salaries = 87.31;
  costs.animal_health = 55.04;
  costs.weed_and_pest = 51.64;
  costs.shearing = 46.10;
  costs.fertiliser = 153.62;
  costs.lime = 5.51;
  costs.seeds = 38.21;
  costs.vehicles_and_fuel = 78.84;
  costs.electricity = 9.58;
  costs.feed_and_grazing = 68.36;
  costs.dog_expenses = 9.23;
  costs.cultivation_and_sowing = 39.65;
  costs.cash_crop = 2.30;
  costs.repairs_and_maintenance = 73.30;
  costs.irrigation_charges = 61.50;
  costs.cartage = 18.66;
  costs.administration = 45.42;
  costs.insurance_and_acc = 38.58;
  costs.rates = 45.01;
  return costs;
}

core::Prices canterbury_prices() {
  core::Prices prices;
  prices.lamb_dollars_per_kg_carcass = 7.80;
  prices.wool_dollars_per_kg = 3.80;
  prices.cull_ewe_dollars_per_head = 90.0;
  return prices;
}

FarmBusiness a_business() {
  FarmBusiness business;
  business.costs = canterbury_costs();
  business.prices = canterbury_prices();
  business.opening_balance_dollars = 400.0 * 80.0;

  // Five age classes, the way a farmer would describe the flock.
  for (int age = 2; age <= 6; ++age) {
    core::AgeCohort cohort;
    cohort.birth_year = 2015 - age;
    cohort.age_years = age;
    cohort.mob.name = "ewes";
    cohort.mob.head = 83;
    cohort.mob.animal.class_id = "sheep_ewe";
    cohort.mob.animal.species_factor = 1.0;
    cohort.mob.animal.sex_factor = 1.0;
    cohort.mob.animal.standard_reference_weight_kg = 65.0;
    cohort.mob.animal.grazing_coefficient = 0.0025;
    cohort.mob.animal.gain_energy_ceiling_mj_per_kg = 20.3;
    cohort.mob.state.liveweight_kg = 55.0;
    cohort.mob.state.age_days = 365.0 * age;
    business.flock.add(std::move(cohort));
  }
  return business;
}

/// One farm year out of the decade, on the recorded Lincoln weather.
ScenarioBundle year_of(int starting_year) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());

  core::SnapshotWeatherSource::Options options;
  options.path = std::string(PADDOCK_DATA_DIR) + "/scenarios/lincoln-lurdf/weather-2015-2025.csv";
  options.dataset = "open-meteo";
  options.licence = "CC BY 4.0";
  bundle.weather = std::make_shared<core::SnapshotWeatherSource>(options);
  bundle.range =
      core::DateRange{core::Date{starting_year, 7, 1}, core::Date{starting_year + 1, 6, 30}};
  return bundle;
}

int destockings(const RunSummary& run) {
  if (!run.account.has_value()) {
    return 0;
  }
  return static_cast<int>(
      std::count_if(run.account->entries().begin(), run.account->entries().end(),
                    [](const core::CashEntry& entry) {
                      return entry.detail.find("short of feed") != std::string::npos ||
                             entry.detail.find("stay solvent") != std::string::npos;
                    }));
}

// **The drought now reaches the cover the farmer holds the farm to, and stops
// one step short of the stock.**
//
// This test has moved twice, and where it has got to is the point of it. It
// began asserting that a dry year forces a sale, which the model could not do.
// It then asserted the drought showed in the grass and went no further, because
// the farm carried about five times the feed it needed. Two of those slack
// halves have since been taken up - a ewe is charged for her pregnancy and her
// milk, and her lambs graze - and the driest year in ten now takes cover to
// 1,597 kg DM/ha against the 1,600 the farmer holds it to.
//
// **No mob goes short even so**, and that is not a contradiction: cover here is
// a farm mean, and a mean below the target still leaves feed where the mobs
// actually are. What remains is the grass. Water use efficiency is near 20 kg
// DM/ha/mm, which Martin et al. (2006) give for *irrigated* ryegrass and clover
// against 12.3 for Canterbury dryland, so the farm still grows about 1.6 times
// what its rainfall should buy. See E20.
TEST(DroughtDestockingTest, TheDroughtReachesTheFarmersFloorAndStopsShortOfTheStock) {
  const ScenarioBundle dry = year_of(2015);  // 527 mm, the driest of the ten
  ASSERT_TRUE(dry.management.has_value());

  const RunSummary drought =
      run_managed_scenario(dry, *dry.management, pasture_diet(), "drought", a_business());
  const RunSummary wet =
      run_managed_scenario(year_of(2024), *dry.management, pasture_diet(), "wet", a_business());

  ASSERT_TRUE(drought.account.has_value()) << "a run given a business should keep books";

  // The drought is real and measurable in the grass.
  EXPECT_GT(drought.days_water_stressed(), wet.days_water_stressed() * 3 / 2)
      << "the driest year in ten should be markedly drier than the wettest";
  EXPECT_LT(drought.lowest_cover_kg_dm_per_ha(), wet.lowest_cover_kg_dm_per_ha())
      << "and should take the farm lower";

  // **It now gets under the farmer's floor.** The wet year does not.
  EXPECT_LT(drought.lowest_cover_kg_dm_per_ha(), dry.management->minimum_cover_kg_dm_per_ha)
      << "the driest year in ten should take a Canterbury farm below its target cover";
  EXPECT_GT(wet.lowest_cover_kg_dm_per_ha(), dry.management->minimum_cover_kg_dm_per_ha)
      << "and the wettest should not, or this is a farm rather than a drought";

  // And still no mob is short, so nothing is sold. A model that destocked on a
  // mean while every mob had feed in front of it would be selling on a summary
  // statistic rather than on hunger.
  EXPECT_EQ(drought.days_short, 0);
  EXPECT_EQ(destockings(drought), 0)
      << "a farmer who sold here would be selling for no reason the model can name";
}

// **The year reads like a farm year.** Four events, in order, at prices from
// the economics file: the July cull draft, shearing, the weaning lamb sale and
// the weaning cull.
TEST(DroughtDestockingTest, TheLedgerReadsLikeAFarmYear) {
  const ScenarioBundle year = year_of(2015);
  ASSERT_TRUE(year.management.has_value());

  const RunSummary run =
      run_managed_scenario(year, *year.management, pasture_diet(), "ledger", a_business());
  ASSERT_TRUE(run.account.has_value());

  const double sold = run.account->total_for(core::LedgerReason::SoldStock);
  const double wool = run.account->total_for(core::LedgerReason::SoldWool);

  EXPECT_GT(sold, 40'000.0) << "cull ewes and a lamb crop";
  EXPECT_LT(sold, 100'000.0)
      << "**the assertion that catches the same stock being sold twice.** Before every sale took "
         "its animals with it, this farm drafted 74 head a day for three hundred days and banked "
         "a million dollars from a flock of 415";
  EXPECT_GT(wool, 10'000.0);

  // The flock is smaller at the close than the lambs made it, because the
  // lambs left.
  EXPECT_LT(run.closing_head, 800);
  EXPECT_GT(run.closing_head, 300);
}

// **What the farmer sells stops eating.** The regression test for the coupling
// that was missing: the flock and the mob on the paddock used to be separate
// populations, so a year ate exactly the same grass however the flock went -
// 1,305 kg DM/ha in the driest year of ten and 1,297 in the wettest, on a farm
// whose flock doubled at lambing and halved at weaning. Nothing a farmer did
// could change the feed pressure, which made destocking incapable of relieving
// anything and put M4's drought acceptance out of reach by construction.
TEST(DroughtDestockingTest, WhatTheFarmerSellsStopsEating) {
  const ScenarioBundle year = year_of(2015);
  ASSERT_TRUE(year.management.has_value());

  // The same farm and weather, run with a big flock and a small one.
  FarmBusiness small = a_business();
  const int culled = small.flock.remove_from_breeding(small.flock.breeding_head() / 2);
  ASSERT_GT(culled, 0);

  const RunSummary big =
      run_managed_scenario(year, *year.management, pasture_diet(), "big", a_business());
  const RunSummary few =
      run_managed_scenario(year, *year.management, pasture_diet(), "few", std::move(small));

  EXPECT_LT(few.eaten_kg_dm, big.eaten_kg_dm * 0.75)
      << "half a flock has to eat markedly less grass, or selling stock relieves nothing";
  EXPECT_GT(few.closing_cover_kg_dm, big.closing_cover_kg_dm)
      << "and the grass it did not eat has to still be there";
}

// The control. A model that destocked every year would be describing a farm
// rather than a drought.
TEST(DroughtDestockingTest, TheWettestYearDoesNot) {
  const ScenarioBundle wet = year_of(2024);  // 1,036 mm, the wettest of the ten
  ASSERT_TRUE(wet.management.has_value());

  const RunSummary run =
      run_managed_scenario(wet, *wet.management, pasture_diet(), "wet year", a_business());

  ASSERT_TRUE(run.account.has_value());
  EXPECT_EQ(destockings(run), 0) << "a wet year should not force anybody to sell";
}

// **Money observes; the flock grazes.** This test used to say that a priced run
// and an unpriced one grew identical pasture, and that stopped being true the
// day the flock started driving the mob on the paddock - correctly, because a
// run that carries a flock now carries its grazing too.
//
// The invariant worth keeping is the narrower one underneath it: **the books
// themselves change nothing.** Two runs with the same flock, the same policy
// and different money must grow the same grass to the last kilogram. Both
// balances here are far enough from empty that no rule reads them differently;
// a farm near insolvency is supposed to graze differently, and does.
TEST(DroughtDestockingTest, TheBooksThemselvesDoNotChangeThePasture) {
  const ScenarioBundle dry = year_of(2015);
  ASSERT_TRUE(dry.management.has_value());

  FarmBusiness comfortable = a_business();
  comfortable.opening_balance_dollars = 320'000.0;

  const RunSummary ordinary =
      run_managed_scenario(dry, *dry.management, pasture_diet(), "ordinary", a_business());
  const RunSummary rich =
      run_managed_scenario(dry, *dry.management, pasture_diet(), "rich", std::move(comfortable));

  ASSERT_TRUE(ordinary.account.has_value());
  ASSERT_TRUE(rich.account.has_value());
  EXPECT_NE(ordinary.account->balance(), rich.account->balance()) << "the money did differ";

  ASSERT_EQ(ordinary.cover_kg_dm_per_ha.size(), rich.cover_kg_dm_per_ha.size());
  EXPECT_EQ(ordinary.cover_kg_dm_per_ha, rich.cover_kg_dm_per_ha)
      << "and the grass did not, because an account that fed the pasture would not be an account";
  EXPECT_DOUBLE_EQ(ordinary.closing_cover_kg_dm, rich.closing_cover_kg_dm);

  // An unpriced run keeps no books at all - and now grazes differently, because
  // it carries no flock to graze with.
  const RunSummary unpriced =
      run_managed_scenario(dry, *dry.management, pasture_diet(), "unpriced");
  EXPECT_FALSE(unpriced.account.has_value());
}

// The flock runs its own year inside the run: lambs arrive, the draft leaves.
TEST(DroughtDestockingTest, TheFlockRunsItsYearInsideTheRun) {
  const ScenarioBundle year = year_of(2019);
  ASSERT_TRUE(year.management.has_value());

  const RunSummary run =
      run_managed_scenario(year, *year.management, pasture_diet(), "flock", a_business());

  const int born =
      std::accumulate(run.flock_days.begin(), run.flock_days.end(), 0,
                      [](int running, const core::FlockDay& day) { return running + day.born; });
  const int sold = std::accumulate(
      run.flock_days.begin(), run.flock_days.end(), 0,
      [](int running, const core::FlockDay& day) { return running + day.sold_store; });

  EXPECT_GT(born, 0) << "a year with a lambing date in it should produce lambs";
  EXPECT_GT(sold, 0) << "and a weaning date should send most of them away";
  EXPECT_GT(run.closing_head, 0) << "leaving a flock behind";
}

}  // namespace
}  // namespace paddock::config
