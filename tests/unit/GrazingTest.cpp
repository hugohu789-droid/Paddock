// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Animals eating pasture.
//
// Two things have to hold. Every kilogram that leaves the sward is accounted
// for, because a grazing step that lost dry matter would break the
// conservation gate in a way that looks like a pasture bug. And a mob that
// cannot get what it needs has to be visible as such, because running out of
// feed is the outcome a grazing system is judged by, and a model that quietly
// fed the animals anyway would rank every system the same.

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Grazing.hpp>

namespace paddock::core {
namespace {

SwardParameters two_species_sward() {
  SwardParameters sward;
  sward.par_fraction = 0.5;
  sward.decomposition_rate_per_day = 0.02;

  sward.grass.species_id = "ryegrass_perennial";
  sward.grass.specific_leaf_area_m2_per_kg = 20.0;
  sward.grass.extinction_coefficient = 0.5;
  sward.grass.radiation_use_efficiency_g_per_mj = 1.5;
  sward.grass.base_temperature_c = 4.0;
  sward.grass.optimum_temperature_c = 20.0;
  sward.grass.maximum_temperature_c = 35.0;
  sward.grass.senescence_rate_per_day = 0.02;
  sward.grass.residual_kg_dm_per_ha = 1200.0;
  sward.grass.nitrogen_content_fraction = 0.035;
  sward.grass.nitrogen_fixation_kg_per_t_dm = 0.0;

  sward.legume = sward.grass;
  sward.legume.species_id = "clover_white";
  sward.legume.residual_kg_dm_per_ha = 400.0;
  sward.legume.nitrogen_content_fraction = 0.045;
  sward.legume.nitrogen_fixation_kg_per_t_dm = 25.0;

  return sward;
}

DietQuality pasture_diet() {
  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

/// Ewes, using the one species factor CSIRO and Nicol and Brookes agree on.
Mob ewes(int head) {
  Mob mob;
  mob.name = "ewes";
  mob.head = head;
  mob.animal.class_id = "ewe";
  mob.animal.species_factor = 1.0;
  mob.animal.sex_factor = 1.0;
  mob.animal.standard_reference_weight_kg = 65.0;
  mob.animal.grazing_coefficient = 0.0;
  mob.animal.gain_energy_ceiling_mj_per_kg = 20.3;
  mob.state.liveweight_kg = 60.0;
  mob.state.age_days = 1200.0;
  mob.state.liveweight_change_kg_per_day = 0.0;
  return mob;
}

GrazingConditions flat_paddock() {
  GrazingConditions ground;
  ground.pasture_mass_t_dm_per_ha = 2.5;
  ground.area_per_animal_ha = 0.1;
  return ground;
}

// Conservation: what leaves the sward is exactly what the day says was eaten.
// This is the property the whole ledger gate rests on.
TEST(GrazingTest, EveryKilogramLeavingTheSwardIsAccountedFor) {
  PastureSward sward(two_species_sward(), 2500.0, 800.0, 60.0);
  const double before = sward.green_kg_dm();
  const double nitrogen_before = sward.total_nitrogen_kg();

  BudgetLedger ledger;
  const GrazingDay day = graze(sward, 4.0, ewes(120), pasture_diet(), flat_paddock(), &ledger);

  ASSERT_GT(day.eaten_kg_dm, 0.0);

  // The sward is one hectare, the paddock four, so a quarter of the offtake
  // comes off each hectare.
  const double removed_per_hectare = before - sward.green_kg_dm();
  EXPECT_NEAR(removed_per_hectare * 4.0, day.eaten_kg_dm, 1e-9);

  // Species add up to the total.
  EXPECT_NEAR(day.grass_eaten_kg_dm + day.legume_eaten_kg_dm, day.eaten_kg_dm, 1e-9);

  // Nitrogen leaves with the dry matter.
  const double nitrogen_removed_per_hectare = nitrogen_before - sward.total_nitrogen_kg();
  EXPECT_NEAR(nitrogen_removed_per_hectare * 4.0, day.nitrogen_removed_kg, 1e-9);
  EXPECT_GT(day.nitrogen_removed_kg, 0.0);
}

// The residual is what the plant regrows from, so it is not on offer however
// hungry the mob is. Without this a farm could graze itself to bare soil and
// the sward would never recover, because zero leaf area intercepts no light.
TEST(GrazingTest, AHungryMobCannotEatBelowTheResidual) {
  const SwardParameters parameters = two_species_sward();
  PastureSward sward(parameters, 1400.0, 450.0, 60.0);

  // Far more stock than the paddock can feed.
  const GrazingDay day = graze(sward, 1.0, ewes(3000), pasture_diet(), flat_paddock());

  EXPECT_TRUE(day.feed_limited);
  EXPECT_GE(sward.grass_kg_dm(), parameters.grass.residual_kg_dm_per_ha - 1e-9);
  EXPECT_GE(sward.legume_kg_dm(), parameters.legume.residual_kg_dm_per_ha - 1e-9);
  EXPECT_LT(day.satisfaction(), 1.0);
}

// A paddock with feed to spare leaves most of it standing, so what was offered
// and what was eaten are different numbers. Conflating them would make every
// paddock look grazed out.
TEST(GrazingTest, OfferedIsWhatStoodAboveTheResidualNotWhatWasEaten) {
  PastureSward sward(two_species_sward(), 3000.0, 900.0, 60.0);

  const GrazingDay day = graze(sward, 10.0, ewes(50), pasture_diet(), flat_paddock());

  EXPECT_FALSE(day.feed_limited);
  EXPECT_NEAR(day.eaten_kg_dm, day.demand_kg_dm, 1e-9) << "a mob with feed to spare eats its fill";
  EXPECT_GT(day.offered_kg_dm, day.eaten_kg_dm)
      << "offered " << day.offered_kg_dm << ", eaten " << day.eaten_kg_dm;
  // 3000 + 900 standing, 1200 + 400 residual, over ten hectares.
  EXPECT_NEAR(day.offered_kg_dm, ((3000.0 - 1200.0) + (900.0 - 400.0)) * 10.0, 1e-9);
}

// The point of the coupling: a rested paddock carries a mob that a bare one
// cannot. This is the mechanism a grazing system comparison rests on, so it is
// worth asserting directly rather than inferring from a whole-farm run.
TEST(GrazingTest, ARestedPaddockFeedsAMobThatABareOneCannot) {
  // The covers are chosen against the demand rather than by eye. Two hundred
  // dry ewes of 60 kg need about 150 kg DM between them - 0.75 kg each, which
  // is bare maintenance and close to the 0.40 MJ ME per kg lwt^0.75 Simpson
  // (1978b) published for sheep. So the bare paddock has to offer less than
  // that over its two hectares to be short: 50 kg/ha of grass and 5 of clover
  // above their residuals is 110 kg, and the rested one offers 5200.
  //
  // The first draft of this test used 1300 and 420, which offers 240 kg and is
  // not short at all. The code was right and the premise was wrong.
  PastureSward rested(two_species_sward(), 3200.0, 1000.0, 60.0);
  PastureSward bare(two_species_sward(), 1250.0, 405.0, 60.0);

  const GrazingDay on_rested = graze(rested, 2.0, ewes(200), pasture_diet(), flat_paddock());
  const GrazingDay on_bare = graze(bare, 2.0, ewes(200), pasture_diet(), flat_paddock());

  EXPECT_NEAR(on_rested.demand_kg_dm, on_bare.demand_kg_dm, 1e-9) << "same mob, same demand";
  EXPECT_GT(on_rested.eaten_kg_dm, on_bare.eaten_kg_dm);
  EXPECT_FALSE(on_rested.feed_limited);
  EXPECT_TRUE(on_bare.feed_limited);
  EXPECT_GT(on_rested.satisfaction(), on_bare.satisfaction());
}

// The same mob on a bigger paddock takes less from each hectare, and the same
// amount in total. Getting this backwards would make stocking rate meaningless.
TEST(GrazingTest, TheSameMobTakesLessFromEachHectareOfABiggerPaddock) {
  PastureSward small(two_species_sward(), 3000.0, 900.0, 60.0);
  PastureSward large(two_species_sward(), 3000.0, 900.0, 60.0);

  const double small_before = small.green_kg_dm();
  const double large_before = large.green_kg_dm();

  const GrazingDay on_small = graze(small, 2.0, ewes(100), pasture_diet(), flat_paddock());
  const GrazingDay on_large = graze(large, 8.0, ewes(100), pasture_diet(), flat_paddock());

  EXPECT_NEAR(on_small.eaten_kg_dm, on_large.eaten_kg_dm, 1e-9) << "same mob, same total intake";
  EXPECT_GT(small_before - small.green_kg_dm(), large_before - large.green_kg_dm())
      << "but a heavier bite per hectare on the small paddock";
}

// A species already at its residual contributes nothing, and must not be drawn
// below it while the other still has feed.
TEST(GrazingTest, ASpeciesAtItsResidualIsNotDrawnDownFurther) {
  const SwardParameters parameters = two_species_sward();
  // Clover already at its residual; grass with plenty above its own.
  PastureSward sward(parameters, 3000.0, parameters.legume.residual_kg_dm_per_ha, 60.0);

  const GrazingDay day = graze(sward, 1.0, ewes(30), pasture_diet(), flat_paddock());

  EXPECT_GT(day.grass_eaten_kg_dm, 0.0);
  EXPECT_DOUBLE_EQ(day.legume_eaten_kg_dm, 0.0);
  EXPECT_DOUBLE_EQ(sward.legume_kg_dm(), parameters.legume.residual_kg_dm_per_ha);
}

// The loop closing. Run the same mob for a fortnight on a paddock that can feed
// it and one that cannot, and the two animals must end up different weights.
// Before advance_one_day existed they ended up identical, which is why running
// out of feed had no consequence and no grazing system could beat another.
TEST(GrazingTest, AFortnightOnShortFeedCostsWeightThatGoodFeedDoesNot) {
  const DietQuality diet = pasture_diet();
  const GrazingConditions ground = flat_paddock();

  PastureSward plenty(two_species_sward(), 3500.0, 1100.0, 60.0);
  PastureSward short_feed(two_species_sward(), 1260.0, 410.0, 60.0);

  Mob well_fed = ewes(60);
  Mob underfed = ewes(60);
  const double starting_weight = well_fed.state.liveweight_kg;

  for (int day = 0; day < 14; ++day) {
    const GrazingDay on_plenty = graze(plenty, 3.0, well_fed, diet, ground);
    const GrazingDay on_short = graze(short_feed, 3.0, underfed, diet, ground);
    advance_one_day(well_fed, on_plenty, diet, ground);
    advance_one_day(underfed, on_short, diet, ground);
  }

  EXPECT_LT(underfed.state.liveweight_kg, starting_weight) << "the underfed mob has to lose";
  EXPECT_GT(well_fed.state.liveweight_kg, underfed.state.liveweight_kg)
      << "well fed " << well_fed.state.liveweight_kg << " kg, underfed "
      << underfed.state.liveweight_kg << " kg after 14 days";

  // Age advances with the days, which is what makes the maintenance requirement
  // drift over a long run rather than staying pinned to the starting state.
  EXPECT_DOUBLE_EQ(well_fed.state.age_days, ewes(1).state.age_days + 14.0);
}

// A mob eating exactly what a well-stocked paddock offers should hold its
// weight rather than drift, because the grazing step and the energy step are
// the same equations read in opposite directions.
TEST(GrazingTest, AMobWithFeedToSpareHoldsItsWeight) {
  const DietQuality diet = pasture_diet();
  const GrazingConditions ground = flat_paddock();

  PastureSward sward(two_species_sward(), 4000.0, 1200.0, 60.0);
  Mob mob = ewes(20);

  const GrazingDay day = graze(sward, 5.0, mob, diet, ground);
  ASSERT_FALSE(day.feed_limited);

  const LiveweightResponse response = advance_one_day(mob, day, diet, ground);
  EXPECT_NEAR(response.liveweight_change_kg, 0.0, 1e-6)
      << "a mob that ate its requirement should not drift";
}

// **An empty mob is December, not an error.** This used to be refused along
// with a nameless mob and a paddock of no area, and it stopped being an error
// when a mob's head began to be driven by a flock: the lamb crop is empty
// between weaning and the next lambing, and a paddock with no stock on it is a
// real state of a farm rather than a broken input.
TEST(GrazingTest, AnEmptyMobEatsNothingAndIsNotStarving) {
  PastureSward sward(two_species_sward(), 3000.0, 900.0, 60.0);
  const double before = sward.grass_kg_dm() + sward.legume_kg_dm();

  Mob none = ewes(10);
  none.head = 0;
  const GrazingDay day = graze(sward, 1.0, none, pasture_diet(), flat_paddock());

  EXPECT_DOUBLE_EQ(day.eaten_kg_dm, 0.0);
  EXPECT_DOUBLE_EQ(day.demand_kg_dm, 0.0);
  EXPECT_FALSE(day.feed_limited) << "nothing went hungry, because nothing was there";
  EXPECT_DOUBLE_EQ(sward.grass_kg_dm() + sward.legume_kg_dm(), before)
      << "and the grass is untouched";

  // The intake per head is the one that would have been a NaN, and a NaN here
  // spreads into liveweight and never comes back.
  EXPECT_FALSE(std::isnan(day.intake_per_head_kg_dm));
}

TEST(GrazingTest, AnImpossibleMobOrPaddockIsRefused) {
  PastureSward sward(two_species_sward(), 3000.0, 900.0, 60.0);

  EXPECT_THROW(static_cast<void>(graze(sward, 0.0, ewes(10), pasture_diet(), flat_paddock())),
               std::invalid_argument);

  Mob unnamed = ewes(10);
  unnamed.name.clear();
  EXPECT_THROW(static_cast<void>(graze(sward, 1.0, unnamed, pasture_diet(), flat_paddock())),
               std::invalid_argument);
}

}  // namespace
}  // namespace paddock::core
