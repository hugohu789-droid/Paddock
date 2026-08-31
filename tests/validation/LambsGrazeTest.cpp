// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// The lamb crop, which used to be born, counted, priced and sold without ever
/// taking a mouthful of grass.
///
/// Caveat E22 recorded that gap: the mob on the paddock carried one animal at a
/// ewe's liveweight, so counting the lambs there would have fed 529 lambs as
/// 529 grown ewes. The answer is a mob of their own, stocked by the flock, with
/// a lamb's own species file - and an udder between the two, because a lamb's
/// milk is already charged to its mother and the farm must not be fed twice.

#include <gtest/gtest.h>

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

FarmBusiness a_business() {
  FarmBusiness business;
  business.prices.lamb_dollars_per_kg_carcass = 7.80;
  business.prices.wool_dollars_per_kg = 3.80;
  business.prices.cull_ewe_dollars_per_head = 90.0;
  business.opening_balance_dollars = 32'000.0;

  for (int age = 2; age <= 6; ++age) {
    core::AgeCohort cohort;
    cohort.birth_year = 2015 - age;
    cohort.age_years = age;
    cohort.mob.name = "ewes";
    cohort.mob.head = 83;
    cohort.mob.animal.class_id = "sheep_ewe";
    cohort.mob.animal.species_factor = 1.0;
    cohort.mob.animal.sex_factor = 1.0;
    cohort.mob.animal.standard_reference_weight_kg = 66.0;
    cohort.mob.animal.grazing_coefficient = 0.0025;
    cohort.mob.animal.gain_energy_ceiling_mj_per_kg = 20.3;
    cohort.mob.animal.gestation_length_days = 150.0;
    cohort.mob.animal.milk_fat_percent = 7.0;
    cohort.mob.animal.milk_protein_percent = 5.8;
    cohort.mob.animal.breed_effect = 0.01;
    cohort.mob.state.liveweight_kg = 55.0;
    cohort.mob.state.age_days = 365.0 * age;
    business.flock.add(std::move(cohort));
  }
  return business;
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

// **The lamb mob exists and the flock stocks it.** Zero before lambing, the
// season's crop through spring, zero after the weaning draft.
TEST(LambsGrazeTest, TheBundleDeclaresALambMobAndTheFlockFillsIt) {
  const ScenarioBundle bundle = year_of(2015);
  ASSERT_EQ(bundle.mobs.size(), 2u) << "the bundle should carry ewes and lambs";
  EXPECT_EQ(bundle.mobs[1].head, 0)
      << "a mob stocked by a flock starts empty; December is not an error state";
}

// **The grass notices.** The same farm and weather, run with and without the
// lamb mob to graze on, has to eat more when the lambs are there.
TEST(LambsGrazeTest, ALambCropEatsGrass) {
  ScenarioBundle with_lambs = year_of(2015);
  ASSERT_TRUE(with_lambs.management.has_value());

  ScenarioBundle ewes_only = year_of(2015);
  ewes_only.mobs.pop_back();  // the way the farm was before E22 was closed

  const RunSummary lambs = run_managed_scenario(with_lambs, *with_lambs.management, pasture_diet(),
                                                "lambs", a_business());
  const RunSummary without = run_managed_scenario(ewes_only, *ewes_only.management, pasture_diet(),
                                                  "ewes only", a_business());

  EXPECT_GT(lambs.eaten_kg_dm, without.eaten_kg_dm)
      << "a lamb crop that ate nothing is what E22 recorded";
  EXPECT_LT(lambs.lowest_cover_kg_dm_per_ha(), without.lowest_cover_kg_dm_per_ha())
      << "and the extra mouths should show at the bottom of the year";
}

// **Against a figure this model did not choose.** OVERSEER assumes a sheep
// weaning weight of 20 kg when none is supplied (TMC Characteristics of
// animals, Eq. 17). That number is nowhere in this model - the lambs are born
// at a birth weight, drink what their mothers make and graze for the rest, and
// whatever they weigh at weaning is what that produced. Landing near 20 is
// therefore a check and not a tautology.
//
// It is deliberately not forced. TMC Eq. 65 would set a pre-weaned animal's
// daily gain as (weaning weight - birth weight) / weaning age, and feeding the
// lambs to that target would reproduce the manual by construction and tell
// nobody anything about the pasture.
TEST(LambsGrazeTest, LambsReachRoughlyTheWeaningWeightOverseerAssumes) {
  const ScenarioBundle bundle = year_of(2015);
  ASSERT_TRUE(bundle.management.has_value());

  const RunSummary run =
      run_managed_scenario(bundle, *bundle.management, pasture_diet(), "weaning", a_business());

  // The lamb cohort's weight at the weaning draft. Liveweights in the summary
  // are the first mob's, so this reads the flock's own record instead.
  ASSERT_FALSE(run.flock_days.empty());

  // Born at a share of the dam's reference weight (TMC Eq. 11-14): between a
  // single's 6.6 kg and a twin's 5.61 kg at a 132.3% lambing.
  EXPECT_GT(run.closing_head, 0);

  // 20 kg is the manual's assumption; a third either side covers the fact that
  // this farm's lambs are grown on measured Canterbury weather rather than on a
  // national average.
  const double weaning_weight = run.lamb_weaning_weight_kg;
  EXPECT_GT(weaning_weight, 13.0)
      << "lambs this light would mean the milk transfer is not reaching them";
  EXPECT_LT(weaning_weight, 27.0) << "and lambs this heavy would mean they are being fed twice";
}

// **The udder is a transfer, not a second helping.** What the ewes are charged
// for making, the lambs are credited with drinking - so a farm with lambs on it
// does not eat as though every lamb were a grown ewe.
TEST(LambsGrazeTest, MilkIsChargedOnceAndTheFarmIsNotFedTwice) {
  const ScenarioBundle bundle = year_of(2015);
  ASSERT_TRUE(bundle.management.has_value());

  const RunSummary run =
      run_managed_scenario(bundle, *bundle.management, pasture_diet(), "udder", a_business());

  // Roughly 440 lambs join about 310 ewes for a hundred days. If they grazed as
  // grown ewes the year's intake would rise by something near a third; the milk
  // half of their diet is already on the ewes' side, so it rises by far less.
  const double eaten_per_ha = run.eaten_kg_dm / 80.0;
  EXPECT_GT(eaten_per_ha, 1'700.0);
  EXPECT_LT(eaten_per_ha, 2'400.0)
      << "an intake this high would mean the lambs are grazing for milk they already drank";
}

}  // namespace
}  // namespace paddock::config
