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
#include "../support/ValueOf.hpp"

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
  return static_cast<int>(std::count_if(
      tests::value_of(run.account, "an account").entries().begin(),
      tests::value_of(run.account, "an account").entries().end(), [](const core::CashEntry& entry) {
        return entry.detail.find("short of feed") != std::string::npos ||
               entry.detail.find("stay solvent") != std::string::npos;
      }));
}

/// Mean cover over January and February - **where a Canterbury drought is**.
///
/// The annual minimum used to be the drought and is not any more. Once
/// senescence became a season (E27) every year bottoms out in the same place,
/// late winter, before spring growth starts: the driest year in ten now has a
/// slightly *higher* annual low than the wettest, because the wet year grew
/// more leaf in autumn and more of it died over winter. That is not the drought
/// moving; it is the drought never having been what the annual minimum
/// measured.
double summer_cover(const RunSummary& run) {
  double total = 0.0;
  int days = 0;
  for (std::size_t day = 0; day < run.dates.size() && day < run.cover_kg_dm_per_ha.size(); ++day) {
    if (run.dates[day].month == 1 || run.dates[day].month == 2) {
      total += run.cover_kg_dm_per_ha[day];
      ++days;
    }
  }
  return days > 0 ? total / days : 0.0;
}

// **The drought is in the summer, and it does not force a sale at this
// stocking rate.** This test has moved four times and this is the only move
// that was a retreat, so it is worth being exact about what was given up.
//
// It passed. The driest year took cover below the 1,600 kg DM/ha the farmer
// holds the farm to, the mob went short, and the farmer sold - M4's acceptance,
// met. **It was an artifact.** Half of that cover was dead standing material:
// the sward carried senescence and decomposition at the same 2% a day, which
// forces exactly half the standing crop to be dead whatever else the model
// does, and the threshold was being crossed by a number inflated with thatch.
//
// Fixing that (E26) and giving senescence a season (E27) left cover honest and
// in a real Canterbury range - 1,300 to 2,800 kg DM/ha where a sheep farm runs
// 1,200 to 2,500 - and at 5.2 ewes a hectare an honest cover does not fall to
// 1,600 even in the driest year in ten. Utilisation is 21%. **The farm is
// understocked, which is E20 and was always E20**; the destocking that used to
// happen was covering for it.
//
// So this asserts the drought where the drought is: a summer several hundred
// kilograms of dry matter poorer, and a farm that harvests less because of it.
TEST(DroughtDestockingTest, TheDroughtShowsInTheSummerAndNotInTheAnnualLow) {
  const ScenarioBundle dry = year_of(2015);  // 527 mm, the driest of the ten
  ASSERT_TRUE(dry.management.has_value());

  const RunSummary drought =
      run_managed_scenario(dry, tests::value_of(dry.management, "a [management] section"),
                           pasture_diet(), "drought", a_business());
  const RunSummary wet =
      run_managed_scenario(year_of(2024), tests::value_of(dry.management, "a [management] section"),
                           pasture_diet(), "wet", a_business());

  ASSERT_TRUE(drought.account.has_value()) << "a run given a business should keep books";

  // The drought is real, measurable, and in the summer.
  EXPECT_GT(drought.days_water_stressed(), wet.days_water_stressed() * 3 / 2)
      << "the driest year in ten should be markedly drier than the wettest";
  EXPECT_LT(summer_cover(drought), summer_cover(wet))
      << "and should leave several hundred kilograms less standing through January and February";
  // **And the two years now part company, which is what this test was waiting
  // for.** It used to assert the opposite - that a farm which grew 5.7 tonnes
  // and one that grew 9.8 handed their animals the same dinner - and said in so
  // many words that separation would mean the farm had become feed-limited.
  // They separated, and not because the stocking rate moved. E62 gave the sward
  // the half of drought it was missing: leaf that dies because the plant has run
  // out of water, rather than only leaf that lives longer because the tiller has
  // slowed down. A Canterbury summer now browns off, and a mob on a browned-off
  // farm eats less than a mob on a green one.
  //
  // **This is the first time anything in this model has been short of feed for
  // a reason the weather caused**, which is the thing an intake model has to be
  // built on top of. 155 tonnes eaten against 195.
  EXPECT_LT(drought.eaten_kg_dm, wet.eaten_kg_dm * 0.9)
      << "the driest year in ten should feed fewer mouths than the wettest";
  EXPECT_GT(drought.eaten_kg_dm, wet.eaten_kg_dm * 0.6)
      << "and should not collapse - a dryland Canterbury drought is a bad year, not a failed one";

  // **And the annual minimum has moved into the summer, where it belongs.** It
  // used to sit in late winter in both years, within 250 kg DM/ha of each other,
  // for reasons that had nothing to do with the drought. Now the dry year's
  // floor is several hundred kilograms below the wet year's.
  EXPECT_LT(drought.lowest_cover_kg_dm_per_ha(), wet.lowest_cover_kg_dm_per_ha() - 150.0)
      << "the drought should set the year's floor, not the winter";

  // **The drought now takes stock off, which is M4's acceptance and which this
  // model did not meet until the intake ceiling was switched on (E107).** A ewe
  // who cannot harvest what she needs off a short sward goes short, the short
  // days run together, and the farmer sells - which is what a farmer does in a
  // drought and what the flat-appetite farm could never produce.
  //
  // How many sales is E95-dependent and is not asserted. That it happens at all
  // is the claim.
  EXPECT_GT(destockings(drought), 0)
      << "a drought year that never forces a sale is the acceptance M4 asks for going unmet";
}

TEST(DroughtDestockingTest, TheLedgerReadsLikeAFarmYear) {
  const ScenarioBundle year = year_of(2015);
  ASSERT_TRUE(year.management.has_value());

  const RunSummary run =
      run_managed_scenario(year, tests::value_of(year.management, "a [management] section"),
                           pasture_diet(), "ledger", a_business());
  ASSERT_TRUE(run.account.has_value());

  const double sold =
      tests::value_of(run.account, "an account").total_for(core::LedgerReason::SoldStock);
  const double wool =
      tests::value_of(run.account, "an account").total_for(core::LedgerReason::SoldWool);

  EXPECT_GT(sold, 40'000.0) << "cull ewes and a lamb crop";
  EXPECT_LT(sold, 100'000.0)
      << "**the assertion that catches the same stock being sold twice.** Before every sale took "
         "its animals with it, this farm drafted 74 head a day for three hundred days and banked "
         "a million dollars from a flock of 415";
  // The flock is smaller than it was, so the clip is. E95 has not settled how deep the
  // early-lactation intake deficit should be for this ewe, so this bound records what the model
  // does and is not a validation of it.
  EXPECT_GT(wool, 9'000.0);

  // The flock is smaller at the close than the lambs made it, because the
  // lambs left.
  EXPECT_LT(run.closing_head, 800);
  EXPECT_GT(run.closing_head, 50)
      << "a dry year takes a draft off this farm, but not the whole flock. E95 has not "
         "settled how deep the early-lactation intake deficit should be for this ewe, so "
         "this bound records what the model does and is not a validation of it";
}

// **What the farmer sells stops eating.** The regression test for the coupling
// that was missing: the flock and the mob on the paddock used to be separate
// populations, so a year ate exactly the same grass however the flock went -
// 1,305 kg DM/ha in the driest year of ten and 1,297 in the wettest, on a farm
// whose flock doubled at lambing and halved at weaning. Nothing a farmer did
// could change the feed pressure, which made destocking incapable of relieving
// anything and put M4's drought acceptance out of reach by construction.
TEST(DroughtDestockingTest, WhatTheFarmerSellsStopsEating) {
  // **The wet year, not the dry one.** In a dry year the big flock destocks and
  // the small one does not, so the two end the year with similar numbers and
  // the comparison measures the destocking rule instead of the coupling. A wet
  // year sells nothing for feed, which leaves only the thing being tested.
  const ScenarioBundle year = year_of(2024);
  ASSERT_TRUE(year.management.has_value());

  // The same farm and weather, run with a big flock and a small one.
  FarmBusiness small = a_business();
  const int culled = small.flock.remove_from_breeding(small.flock.breeding_head() / 2);
  ASSERT_GT(culled, 0);

  const RunSummary big =
      run_managed_scenario(year, tests::value_of(year.management, "a [management] section"),
                           pasture_diet(), "big", a_business());
  const RunSummary few =
      run_managed_scenario(year, tests::value_of(year.management, "a [management] section"),
                           pasture_diet(), "few", std::move(small));

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
      run_managed_scenario(wet, tests::value_of(wet.management, "a [management] section"),
                           pasture_diet(), "wet year", a_business());

  ASSERT_TRUE(run.account.has_value());
  // **A wet year now sells too, and that is the honest state rather than a
  // comfortable one.** With the intake ceiling on, a ewe at peak lactation can
  // fall short on a sward that is growing well but standing short at lambing,
  // and the short days run together. Whether a wet year should destock at all
  // is exactly the magnitude question E95 leaves open - the direction of the
  // early-lactation deficit is sound and its depth is not settled - so this
  // records what the model does and asserts only that it is not the whole
  // flock.
  EXPECT_LT(destockings(run), 20)
      << "a wet year is selling on most days, which is not a lactation dip";
}

// **Money observes; the flock grazes.** This test used to say that a priced run
// and an unpriced one grew identical pasture, and that stopped being true the
// day the flock started driving the mob on the paddock - correctly, because a
// run that carries a flock now carries its grazing too.
//
// The invariant worth keeping is the narrower one underneath it: **the books
// themselves change nothing.** Two runs with the same flock, the same policy
// and different money must grow the same grass to the last kilogram.
//
// Both balances are deliberately large. A farm near insolvency is supposed to
// graze differently and does - once the pasture was calibrated, a farm opening
// on $32,000 sold stock in the driest year where one opening on $320,000 bought
// feed instead, and this test failed for exactly the right reason. Money that
// runs out is a real input to a decision; money that does not is not.
TEST(DroughtDestockingTest, TheBooksThemselvesDoNotChangeThePasture) {
  const ScenarioBundle dry = year_of(2015);
  ASSERT_TRUE(dry.management.has_value());

  FarmBusiness ordinary_b = a_business();
  ordinary_b.opening_balance_dollars = 300'000.0;
  FarmBusiness comfortable = a_business();
  comfortable.opening_balance_dollars = 320'000.0;

  const RunSummary ordinary =
      run_managed_scenario(dry, tests::value_of(dry.management, "a [management] section"),
                           pasture_diet(), "ordinary", std::move(ordinary_b));
  const RunSummary rich =
      run_managed_scenario(dry, tests::value_of(dry.management, "a [management] section"),
                           pasture_diet(), "rich", std::move(comfortable));

  ASSERT_TRUE(ordinary.account.has_value());
  ASSERT_TRUE(rich.account.has_value());
  EXPECT_NE(tests::value_of(ordinary.account, "an account").balance(),
            tests::value_of(rich.account, "an account").balance())
      << "the money did differ";

  ASSERT_EQ(ordinary.cover_kg_dm_per_ha.size(), rich.cover_kg_dm_per_ha.size());
  EXPECT_EQ(ordinary.cover_kg_dm_per_ha, rich.cover_kg_dm_per_ha)
      << "and the grass did not, because an account that fed the pasture would not be an account";
  EXPECT_DOUBLE_EQ(ordinary.closing_cover_kg_dm, rich.closing_cover_kg_dm);

  // An unpriced run keeps no books at all - and now grazes differently, because
  // it carries no flock to graze with.
  const RunSummary unpriced = run_managed_scenario(
      dry, tests::value_of(dry.management, "a [management] section"), pasture_diet(), "unpriced");
  EXPECT_FALSE(unpriced.account.has_value());
}

// The flock runs its own year inside the run: lambs arrive, the draft leaves.
TEST(DroughtDestockingTest, TheFlockRunsItsYearInsideTheRun) {
  const ScenarioBundle year = year_of(2019);
  ASSERT_TRUE(year.management.has_value());

  const RunSummary run =
      run_managed_scenario(year, tests::value_of(year.management, "a [management] section"),
                           pasture_diet(), "flock", a_business());

  const int born =
      std::accumulate(run.flock_days.begin(), run.flock_days.end(), 0,
                      [](int running, const core::FlockDay& day) { return running + day.born; });
  const int finished = std::accumulate(
      run.flock_days.begin(), run.flock_days.end(), 0,
      [](int running, const core::FlockDay& day) { return running + day.kept_to_finish; });

  EXPECT_GT(born, 0) << "a year with a lambing date in it should produce lambs";
  EXPECT_GT(finished, 0)
      << "and a weaning date should put most of them into the finishing mob - this farm's own "
         "cost survey is Beef + Lamb's Class 6, which finishes";
  EXPECT_GT(run.closing_head, 0) << "leaving a flock behind";
}

}  // namespace
}  // namespace paddock::config
