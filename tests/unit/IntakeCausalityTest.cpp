// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// What an animal eats, and in what order the model decides it.
//
// **The chain under test**: what the animal can physiologically hold, then the
// trough within that, then the grass within what is left of it, then the weight
// that comes out of what was actually eaten. Every one of those steps existed
// before this file; the third bound on the trough did not, and without it a
// farmer could buy his way to any liveweight gain he asked for - E77 measured
// 182.49 kg over a year against a target of 182.5, to the decimal.
//
// **The appetite parameters are set here rather than loaded.** The shipped
// sheep files hold `appetite_scalar_per_day` at zero, which switches the
// ceiling off for every sheep in the project - a data state E93 records and
// which is blocked on evidence rather than on code. Setting it explicitly is
// what `AnimalEnergyTest` already does, and it is the only way to test a
// mechanism whose data is currently disabled.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include <paddock/core/Farm.hpp>
#include <paddock/core/SyntheticTerrain.hpp>
#include <paddock/core/SyntheticWeather.hpp>

namespace paddock::core {
namespace {

constexpr double kWest = 1570000.0;
constexpr double kSouth = 5179000.0;
constexpr double kCellSize = 25.0;

BoundingBox farm_area() {
  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{kWest, kSouth});
  area.expand_to_include(Point2D{kWest + 400.0, kSouth + 300.0});
  return area;
}

SoilWaterParameters soil() {
  SoilWaterParameters parameters;
  parameters.total_available_water_mm = 120.0;
  parameters.depletion_fraction = 0.6;
  parameters.crop_coefficient = 0.95;
  parameters.runoff_fraction = 0.05;
  return parameters;
}

SwardParameters sward() {
  SwardParameters parameters;
  parameters.par_fraction = 0.5;
  parameters.decomposition_rate_per_day = 0.02;

  parameters.grass.species_id = "ryegrass_perennial";
  parameters.grass.specific_leaf_area_m2_per_kg = 20.0;
  parameters.grass.extinction_coefficient = 0.5;
  parameters.grass.radiation_use_efficiency_g_per_mj = 1.5;
  parameters.grass.base_temperature_c = 4.0;
  parameters.grass.optimum_temperature_c = 20.0;
  parameters.grass.maximum_temperature_c = 35.0;
  parameters.grass.senescence_rate_per_day = 0.02;
  parameters.grass.residual_kg_dm_per_ha = 1200.0;
  parameters.grass.nitrogen_content_fraction = 0.035;
  parameters.grass.nitrogen_fixation_kg_per_t_dm = 0.0;

  parameters.legume = parameters.grass;
  parameters.legume.species_id = "clover_white";
  parameters.legume.residual_kg_dm_per_ha = 400.0;
  parameters.legume.nitrogen_content_fraction = 0.045;
  parameters.legume.nitrogen_fixation_kg_per_t_dm = 25.0;

  return parameters;
}

FarmletInitialState opening(double cover_kg_dm_per_ha) {
  FarmletInitialState state;
  state.soil_water_mm = 90.0;
  state.grass_kg_dm_per_ha = cover_kg_dm_per_ha * 0.8;
  state.legume_kg_dm_per_ha = cover_kg_dm_per_ha * 0.2;
  state.soil_mineral_nitrogen_kg_per_ha = 60.0;
  return state;
}

DietQuality pasture_diet() {
  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

/// A ewe with GrazPlan's appetite parameters set, which the shipped files do
/// not do. C_I1 = 0.04 and C_I2 = 1.7 are the values `AnimalEnergyTest` uses
/// and the ones the species file's own commentary cites.
Mob ewes(int head, double liveweight_kg = 60.0) {
  Mob mob;
  mob.name = "ewes";
  mob.head = head;
  mob.animal.class_id = "sheep_ewe";
  mob.animal.species_factor = 1.0;
  mob.animal.sex_factor = 1.0;
  mob.animal.standard_reference_weight_kg = 65.0;
  mob.animal.grazing_coefficient = 0.0025;
  mob.animal.gain_energy_ceiling_mj_per_kg = 20.3;

  // Without these `normal_weight_kg` returns the animal's own weight, relative
  // condition is one by construction, and the condition factor can never bite -
  // which is a fair description of a model that does not know what an animal of
  // this frame ought to weigh.
  mob.animal.normal_weight_rate = 0.0157;
  mob.animal.normal_weight_exponent = 0.27;

  mob.animal.appetite_scalar_per_day = 0.04;
  mob.animal.appetite_size_coefficient = 1.7;
  mob.animal.condition_intake_limit = 1.5;
  mob.animal.lactation_peak_days = 28.0;
  mob.animal.lactation_curve_exponent = 1.4;
  mob.animal.lactation_peak_no_young = 0.524;
  mob.animal.lactation_peak_one_young = 0.524;
  mob.animal.lactation_peak_two_young = 0.707;
  mob.animal.lactation_peak_three_young = 0.891;
  mob.animal.intake_availability_rate_per_kg_dm = 0.00112;
  mob.animal.intake_grazing_time_rate_per_kg_dm = 0.00112;
  mob.animal.intake_grazing_time_increase = 0.6;

  mob.state.liveweight_kg = liveweight_kg;
  mob.state.age_days = 1200.0;
  return mob;
}

Farm build_farm(double cover_kg_dm_per_ha, int head, double liveweight_kg = 60.0) {
  const Raster<double> elevation = SyntheticElevationSource().fetch(farm_area(), kCellSize);
  const Raster<SoilWaterParameters> soils(elevation.cols(), elevation.rows(), elevation.transform(),
                                          soil());
  FarmletGrid grid(soils, sward(), opening(cover_kg_dm_per_ha), -43.6);
  std::vector<Paddock> paddocks = SyntheticParcelSource(2.0).fetch(farm_area());
  PaddockMask mask(elevation, paddocks);

  Farm farm(std::move(grid), std::move(mask), std::move(paddocks));
  farm.add_mob(ewes(head, liveweight_kg), {0});
  return farm;
}

DailyWeather a_growing_day() {
  DailyWeather day;
  day.date = Date{2024, 1, 15};
  day.rainfall_mm = 2.0;
  day.min_air_temperature_c = 10.0;
  day.max_air_temperature_c = 20.0;
  day.solar_radiation_mj_per_m2 = 20.0;
  return day;
}

/// One day, with `offered` kg of dry matter put in front of the mob.
MobDay one_day(Farm& farm, double offered) {
  const std::vector<double> supplement{offered};
  const FarmDay day = farm.step(a_growing_day(), pasture_diet(), supplement);
  return day.mobs.front();
}

// ---------------------------------------------------------------------------
// The ceiling itself.

// **The invariant the whole chain exists for.** Whatever is on the ground and
// whatever is in the trough, an animal eats what an animal can eat.
TEST(IntakeCausalityTest, TotalIntakeNeverExceedsPhysiologicalCapacity) {
  for (const double cover : {600.0, 1'500.0, 3'000.0}) {
    for (const double offered : {0.0, 50.0, 500.0, 5'000.0}) {
      Farm farm = build_farm(cover, 100);
      const MobDay day = one_day(farm, offered);

      EXPECT_GT(day.intake_capacity_kg_dm, 0.0)
          << "cover " << cover << ", offered " << offered << ": no ceiling was computed";
      EXPECT_LE(day.total_intake_kg_dm(), day.intake_capacity_kg_dm + 1e-9)
          << "cover " << cover << " kg DM/ha, offered " << offered << " kg: ate "
          << day.total_intake_kg_dm() << " against a capacity of " << day.intake_capacity_kg_dm;
    }
  }
}

// **The task's own acceptance condition: feed was offered and could not all be
// eaten.** Five tonnes in front of a hundred ewes is about twenty-five kg each
// against an appetite near two, and the refusal has to be visible in the record
// rather than absorbed.
TEST(IntakeCausalityTest, FeedOfferedBeyondAppetiteIsRefusedAndSaidSo) {
  Farm farm = build_farm(2'000.0, 100);
  const MobDay day = one_day(farm, 5'000.0);

  EXPECT_DOUBLE_EQ(day.supplement_offered_kg_dm, 5'000.0);
  EXPECT_LT(day.supplement_kg_dm, day.supplement_offered_kg_dm)
      << "every kilogram offered was eaten, which is not a thing an animal does";
  EXPECT_LE(day.supplement_kg_dm, day.intake_capacity_kg_dm + 1e-9);

  // And the difference is retrievable, which is what makes it representable
  // rather than merely bounded.
  const double refused = day.supplement_offered_kg_dm - day.supplement_kg_dm;
  EXPECT_GT(refused, 0.0);
}

// ---------------------------------------------------------------------------
// The four feed situations.

// Abundant pasture and nothing in the trough: the mob eats off the paddock and
// the supplement columns stay empty.
TEST(IntakeCausalityTest, AbundantPastureAndNoSupplement) {
  Farm farm = build_farm(3'000.0, 100);
  const MobDay day = one_day(farm, 0.0);

  EXPECT_GT(day.grazing.eaten_kg_dm, 0.0);
  EXPECT_DOUBLE_EQ(day.supplement_kg_dm, 0.0);
  EXPECT_DOUBLE_EQ(day.supplement_offered_kg_dm, 0.0);
  EXPECT_TRUE(day.grazing.constraint.requirement_met()) << "short on three tonnes of cover";
  EXPECT_LE(day.total_intake_kg_dm(), day.intake_capacity_kg_dm + 1e-9);
}

// Pasture short, feed available: the trough makes up the difference, and the
// mob is no longer short.
TEST(IntakeCausalityTest, PastureLimitedWithSupplementAvailable) {
  Farm bare = build_farm(1'250.0, 200);
  const MobDay without = one_day(bare, 0.0);
  ASSERT_TRUE(without.grazing.constraint.feed_supply_limited)
      << "the setup is not actually pasture-limited";

  Farm fed = build_farm(1'250.0, 200);
  const MobDay with = one_day(fed, 100.0);

  EXPECT_GT(with.supplement_kg_dm, 0.0) << "feed was there and none of it was eaten";
  EXPECT_GT(with.total_intake_kg_dm(), without.total_intake_kg_dm())
      << "feeding out changed nothing";
  EXPECT_LE(with.total_intake_kg_dm(), with.intake_capacity_kg_dm + 1e-9);
}

// **Feed that is not there cannot be eaten.** The same short farm with an empty
// trough: the mob goes short, and nothing appears from nowhere to stop it.
TEST(IntakeCausalityTest, PastureLimitedWithNoSupplementAvailable) {
  Farm farm = build_farm(1'250.0, 200);
  const MobDay day = one_day(farm, 0.0);

  EXPECT_DOUBLE_EQ(day.supplement_kg_dm, 0.0);
  EXPECT_TRUE(day.grazing.constraint.feed_supply_limited)
      << "an empty trough on a bare paddock is the farm being short, not the ewe being full";
  EXPECT_LT(day.total_intake_kg_dm(), day.grazing.demand_kg_dm + day.supplement_kg_dm);
}

// ---------------------------------------------------------------------------
// The target gain is an intent, not an outcome.

// **A farmer cannot instruct an animal to put on two kilograms a day.** He can
// buy feed, and the animal can decline it. Asked for an impossible gain the mob
// eats to its ceiling and no further, and the weight it puts on is whatever
// that intake paid for.
TEST(IntakeCausalityTest, AnImpossibleTargetGainCannotForceIntakeOrWeight) {
  Farm modest = build_farm(2'000.0, 100);
  modest.set_target_gain(0, 0.05);
  const MobDay easy = one_day(modest, 1'000.0);

  Farm impossible = build_farm(2'000.0, 100);
  impossible.set_target_gain(0, 2.0);
  const MobDay asked = one_day(impossible, 1'000.0);

  // The demand rises with the instruction - that is what an intent is for.
  EXPECT_GT(asked.grazing.demand_kg_dm + asked.supplement_kg_dm,
            easy.grazing.demand_kg_dm + easy.supplement_kg_dm);

  // The intake does not follow it past the ceiling.
  EXPECT_LE(asked.total_intake_kg_dm(), asked.intake_capacity_kg_dm + 1e-9);
  EXPECT_LT(asked.response.liveweight_change_kg, 2.0)
      << "the mob gained what it was told to rather than what it ate";

  // And the ceiling is the same in both, because it is a property of the
  // animal and not of what anybody wants from it.
  EXPECT_NEAR(asked.intake_capacity_kg_dm, easy.intake_capacity_kg_dm, 1e-9);
}

// ---------------------------------------------------------------------------
// GrazPlan's own behaviour, preserved.

// **A ewe in milk wants more than a dry one**, which is the relationship that
// makes the ceiling usable at all: against a bare requirement the availability
// term would put every lactating mob permanently short.
TEST(IntakeCausalityTest, ALactatingEweHasMoreAppetiteThanADryOne) {
  const Mob mob = ewes(1);

  const AnimalState dry = mob.state;
  AnimalState milking = mob.state;
  milking.days_lactating = 28;
  milking.young = 1.0;

  const double dry_capacity = potential_intake_kg_dm(mob.animal, dry);
  const double milking_capacity = potential_intake_kg_dm(mob.animal, milking);

  EXPECT_GT(milking_capacity, dry_capacity)
      << "dry " << dry_capacity << " kg DM, milking " << milking_capacity;

  // GrazPlan's own shape: twins ask for more than a single.
  AnimalState twins = milking;
  twins.young = 2.0;
  EXPECT_GT(potential_intake_kg_dm(mob.animal, twins), milking_capacity);
}

// **And a fat ewe wants less**, which is what stops a well-fed mature animal
// growing without limit. The condition factor is GrazPlan's and is not touched
// here; this asserts that it is still doing its job through the farm.
TEST(IntakeCausalityTest, AMatureEweDoesNotGrowWithoutLimitOnAbundantFeed) {
  const Mob mob = ewes(1);

  // **Appetite falls with condition, not with weight**, which is GrazPlan's
  // own shape and is the one that stops a mature animal growing away: a ewe
  // heavy for her frame eats less, and the frame is the normal weight her age
  // and reference weight give her. Asserting it against bare liveweight would
  // be asserting a model this is not.
  AnimalState state = mob.state;
  state.liveweight_kg = 60.0;
  const double frame = normal_weight_kg(mob.animal, state);
  ASSERT_GT(frame, 0.0);

  double previous = 0.0;
  for (const double condition : {1.0, 1.1, 1.2, 1.3}) {
    AnimalState fatter = mob.state;
    fatter.liveweight_kg = frame * condition;
    const double capacity = potential_intake_kg_dm(mob.animal, fatter);
    if (previous > 0.0) {
      EXPECT_LT(capacity, previous) << "appetite did not fall as condition rose: at " << condition
                                    << " of normal weight she wants " << capacity;
    }
    previous = capacity;
  }

  // Over a fortnight of abundant pasture and an open trough the mob gains, and
  // gains less each day as it does. A year would be better and this is a unit
  // suite; the direction is the claim.
  Farm farm = build_farm(3'000.0, 50, 60.0);
  farm.set_target_gain(0, 1.0);
  double first = 0.0;
  double last = 0.0;
  for (int day = 0; day < 14; ++day) {
    const MobDay today = one_day(farm, 1'000.0);
    EXPECT_LE(today.total_intake_kg_dm(), today.intake_capacity_kg_dm + 1e-9);
    if (day == 0) {
      first = today.response.liveweight_change_kg;
    }
    last = today.response.liveweight_change_kg;
  }
  EXPECT_LE(last, first + 1e-9) << "daily gain grew as the ewe did";
}

}  // namespace

// ---------------------------------------------------------------------------
// Which constraint bound the day.
//
// **A farm out of grass and a ewe out of room are different facts**, and the
// field that used to carry both sold four fifths of a flock off an irrigated
// farm growing twelve tonnes (E103). These check that each situation produces
// the name that describes it.

// Feed everywhere, appetite the only limit: the animal's state, not the farm's.
TEST(IntakeCausalityTest, AbundantFeedWithAppetiteTheLimitIsIntakeCapacityLimited) {
  // A target gain nothing could eat its way to, on three tonnes of cover with
  // an open trough - so the only thing that can stop her is her own ceiling.
  Farm farm = build_farm(3'000.0, 100);
  farm.set_target_gain(0, 2.0);
  const MobDay day = one_day(farm, 5'000.0);

  EXPECT_TRUE(day.grazing.constraint.intake_capacity_limited)
      << "she was full and still short, and the model did not say so";
  EXPECT_FALSE(day.grazing.constraint.feed_supply_limited)
      << "three tonnes of cover and an open trough is not a farm short of feed";

  // And she really was at her ceiling.
  EXPECT_NEAR(day.total_intake_kg_dm(), day.intake_capacity_kg_dm, 1e-6);
}

// Not enough grass, room left in the animal: the farm's problem.
TEST(IntakeCausalityTest, ShortPastureWithRoomLeftIsFeedSupplyLimited) {
  Farm farm = build_farm(1'250.0, 200);
  const MobDay day = one_day(farm, 0.0);

  EXPECT_TRUE(day.grazing.constraint.feed_supply_limited);
  EXPECT_FALSE(day.grazing.constraint.intake_capacity_limited)
      << "there was room left in her, so the ceiling was not what stopped her";
  EXPECT_LT(day.total_intake_kg_dm(), day.intake_capacity_kg_dm);
}

// Short pasture, and the trough makes it up: no shortage at all.
TEST(IntakeCausalityTest, SupplementThatFillsTheGapClearsTheFeedSupplySignal) {
  Farm bare = build_farm(1'250.0, 200);
  ASSERT_TRUE(one_day(bare, 0.0).grazing.constraint.feed_supply_limited)
      << "the setup is not actually short";

  Farm fed = build_farm(1'250.0, 200);
  const MobDay day = one_day(fed, 400.0);

  EXPECT_TRUE(day.grazing.constraint.requirement_met())
      << "the trough covered the deficit and the farm still reported a shortage";
  EXPECT_GT(day.supplement_kg_dm, 0.0);
}

// Short pasture and not enough in the trough either: still the farm's problem.
TEST(IntakeCausalityTest, SupplementThatDoesNotFillTheGapLeavesItFeedSupplyLimited) {
  Farm farm = build_farm(1'250.0, 200);
  const MobDay day = one_day(farm, 5.0);

  EXPECT_TRUE(day.grazing.constraint.feed_supply_limited);
  EXPECT_FALSE(day.grazing.constraint.intake_capacity_limited);
}

// **Both on the same farm-day, which the farm record has to keep.** Within one
// mob the two are exclusive - intake either reached the ceiling or it did not -
// so the case that matters is two mobs in different situations.
TEST(IntakeCausalityTest, AFarmCanBeShortOfFeedAndFullOfAppetiteOnTheSameDay) {
  // A short farm, so grass alone cannot feed either mob. The first is fed out
  // to past its appetite and asked for a gain nothing could eat its way to, so
  // its ceiling is what stops it. The second is left on the bare paddock.
  Farm farm = build_farm(1'250.0, 100);
  farm.add_mob(ewes(200), {1});
  farm.set_target_gain(0, 2.0);
  farm.set_target_gain(1, 2.0);

  const std::vector<double> supplement{5'000.0, 0.0};
  const FarmDay day = farm.step(a_growing_day(), pasture_diet(), supplement);

  ASSERT_EQ(day.mobs.size(), 2U);
  EXPECT_TRUE(day.mobs[0].grazing.constraint.intake_capacity_limited);
  EXPECT_TRUE(day.mobs[1].grazing.constraint.feed_supply_limited);

  EXPECT_TRUE(day.any_mob_feed_supply_limited);
  EXPECT_TRUE(day.any_mob_intake_capacity_limited) << "the farm record lost one of the two facts";
}

// **Both at once is the ordinary case on a short sward through lambing**, and
// the two bools have to be able to say so: a ewe whose milk has outrun her
// appetite, on a paddock too short to harvest fast enough, is limited by her
// own ceiling and by the farm at the same time. An enum could not express it.
TEST(IntakeCausalityTest, AShortSwardAndAnUnreachableRequirementAreBothTrue) {
  Farm farm = build_farm(1'250.0, 100);
  farm.set_target_gain(0, 2.0);
  const MobDay day = one_day(farm, 0.0);

  EXPECT_TRUE(day.grazing.constraint.intake_capacity_limited)
      << "the requirement was above her ceiling and the model did not say so";
  EXPECT_TRUE(day.grazing.constraint.feed_supply_limited)
      << "she did not even get what the sward could have given her";
  EXPECT_FALSE(day.grazing.constraint.requirement_met());
}

}  // namespace paddock::core
