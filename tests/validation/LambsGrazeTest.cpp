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
#include <paddock/core/AnimalEnergy.hpp>
#include <paddock/core/Flock.hpp>
#include <paddock/core/SnapshotWeather.hpp>

#include "../support/ShippedBundle.hpp"
#include "../support/ValueOf.hpp"

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
  ASSERT_EQ(bundle.mobs.size(), 2U) << "the bundle should carry ewes and lambs";
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

  const RunSummary lambs = run_managed_scenario(
      with_lambs, tests::value_of(with_lambs.management, "a [management] section"), pasture_diet(),
      "lambs", a_business());
  const RunSummary without = run_managed_scenario(
      ewes_only, tests::value_of(ewes_only.management, "a [management] section"), pasture_diet(),
      "ewes only", a_business());

  EXPECT_GT(lambs.eaten_kg_dm, without.eaten_kg_dm)
      << "a lamb crop that ate nothing is what E22 recorded";
  EXPECT_LT(lambs.lowest_cover_kg_dm_per_ha(), without.lowest_cover_kg_dm_per_ha())
      << "and the extra mouths should show at the bottom of the year";
}

// **Against a figure this model did not choose, and against the better of two.**
//
// This checked OVERSEER's default sheep weaning weight of 20 kg, which is what
// that model assumes when nobody supplies one. Beef + Lamb New Zealand state a
// stronger thing about a real flock: "top flocks should be achieving at least
// 40kg lamb weaned per ewe mated". That is a whole-flock figure - it multiplies
// the weaning weight by the lambing percentage - so it checks two of this
// model's answers at once and cannot be met by a heavy lamb from a poor
// lambing.
//
// Neither number is in the model. The lambs are born at a share of their dam's
// reference weight, drink what their mothers make, graze for the rest, and
// whatever they weigh at weaning is what that produced.
TEST(LambsGrazeTest, TheFlockWeansAboutWhatBeefAndLambAskOfATopFlock) {
  const ScenarioBundle bundle = year_of(2015);
  ASSERT_TRUE(bundle.management.has_value());

  const RunSummary run =
      run_managed_scenario(bundle, tests::value_of(bundle.management, "a [management] section"),
                           pasture_diet(), "weaning", a_business());

  const double weaning_weight = run.lamb_weaning_weight_kg;
  ASSERT_GT(weaning_weight, 0.0) << "the weaning weight should be recorded whichever way the "
                                    "crop went - it used to be recorded only when stores were "
                                    "sold, so a farm that finished its lambs reported nothing";

  // Beef + Lamb's 40 kg is per ewe MATED, so it carries the lambing percentage
  // with it. This flock lambs at 132.3%.
  const double lambing = core::FlockRates{}.lambing_percentage / 100.0;
  const double weaned_per_ewe = weaning_weight * lambing;

  EXPECT_GT(weaned_per_ewe, 32.0)
      << "weaning " << weaning_weight << " kg lambs at " << lambing << " a ewe comes to "
      << weaned_per_ewe << " kg a ewe mated, well under what Beef + Lamb ask of a top flock";
  EXPECT_LT(weaned_per_ewe, 50.0)
      << "and this much would be a flock nobody in Canterbury is running";

  // OVERSEER's own default, kept as the second opinion it is: it assumes 20 kg
  // where Beef + Lamb describe 30, and a model landing between a national
  // assumption and a top flock is in the right country.
  EXPECT_GT(weaning_weight, 20.0) << "above the figure OVERSEER assumes when none is supplied";
  EXPECT_LT(weaning_weight, 36.0) << "and below anything a Canterbury dryland farm weans";
}

TEST(LambsGrazeTest, MilkIsChargedOnceAndTheFarmIsNotFedTwice) {
  const ScenarioBundle bundle = year_of(2015);
  ASSERT_TRUE(bundle.management.has_value());

  const RunSummary run =
      run_managed_scenario(bundle, tests::value_of(bundle.management, "a [management] section"),
                           pasture_diet(), "udder", a_business());

  // Roughly 440 lambs join about 310 ewes for a hundred days. If they grazed as
  // grown ewes the year's intake would rise by something near a third; the milk
  // half of their diet is already on the ewes' side, so it rises by far less.
  //
  // The band moved down when the pasture was calibrated against Winchmore
  // (E21): on a farm growing 4.9 t DM/ha in its driest year, what the mobs can
  // take off the paddock is bounded by the grass rather than by their appetite,
  // and the rest of the year's feed is bought.
  const double eaten_per_ha = run.eaten_kg_dm / 80.0;
  // The band moved up when the farm started finishing its lambs rather than
  // selling them at weaning: a crop carried to autumn eats five more months of
  // grass, which is most of why utilisation went from 21% to 46%.
  EXPECT_GT(eaten_per_ha, 1'500.0);
  EXPECT_LT(eaten_per_ha, 3'200.0)
      << "an intake this high would mean the lambs are grazing for milk they already drank";
}

// **The udder balances, and the loss across it is real.** The ewes are charged
// for the milk they make and the lambs are credited with drinking it, so this is
// a transfer - but making milk is only 62% efficient (kl, TMC Eq. 3), so the
// ewes eat more dry matter than the lambs are spared. Energy is conserved with a
// loss, which is what a udder is; a transfer that broke even would be creating
// energy somewhere.
//
// Checked on the equations rather than through a run, because a run's totals
// mix this with everything else the farm did that day.
TEST(LambsGrazeTest, MakingMilkCostsMoreThanDrinkingItSaves) {
  core::AnimalClassParameters ewe;
  ewe.class_id = "sheep_ewe";
  ewe.kind = core::AnimalKind::Sheep;
  ewe.species_factor = 1.0;
  ewe.sex_factor = 1.0;
  ewe.standard_reference_weight_kg = 66.0;
  ewe.grazing_coefficient = 0.0025;
  ewe.gain_energy_ceiling_mj_per_kg = 20.3;
  ewe.gestation_length_days = 150.0;
  ewe.milk_fat_percent = 7.0;
  ewe.milk_protein_percent = 5.8;
  ewe.breed_effect = 0.01;

  core::AnimalState milking;
  milking.liveweight_kg = 66.0;
  milking.age_days = 1500.0;
  milking.young = 1.323;
  milking.days_lactating = 20;

  core::GrazingConditions ground;
  ground.pasture_mass_t_dm_per_ha = 2.0;
  ground.area_per_animal_ha = 0.2;

  const core::DietQuality diet = pasture_diet();

  // What she puts in the milk, and what it costs her to put it there.
  const double net_in_milk = core::lactation_net_energy_mj(ewe, milking, ground);
  const double cost_to_her = net_in_milk / diet.lactation_efficiency();

  ASSERT_GT(net_in_milk, 0.0);
  EXPECT_GT(cost_to_her, net_in_milk) << "milk cannot cost less to make than it contains";
  EXPECT_NEAR(cost_to_her / net_in_milk, 1.0 / diet.lactation_efficiency(), 1e-9);

  // A lamb is credited with the net energy, not with what she spent - so the
  // farm's feed demand rises by the difference and not by the whole of it.
  EXPECT_NEAR(cost_to_her - net_in_milk, net_in_milk * (1.0 / 0.6197 - 1.0), 0.05)
      << "the loss across the udder is one over kl, about 61% on this diet";
}

}  // namespace
}  // namespace paddock::config
