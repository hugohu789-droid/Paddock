// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// What eating a given amount does to an animal.
//
// This is the inverse of the requirement calculation, and it closes one of the
// two gaps in the simulation loop: without it a mob that could not get what it
// needed was exactly the same animal the next day, so running out of feed had
// no consequence and every grazing system scored the same.
//
// The strongest test here is the round trip. The two directions are separate
// code paths through the same published equations, and if feeding an animal
// exactly what the requirement says it needs does not hold its weight steady,
// one of them is wrong.

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

#include <paddock/core/AnimalEnergy.hpp>

namespace paddock::core {
namespace {

DietQuality pasture_diet() {
  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

AnimalClassParameters ewe() {
  AnimalClassParameters animal;
  animal.class_id = "ewe";
  animal.species_factor = 1.0;
  animal.sex_factor = 1.0;
  animal.standard_reference_weight_kg = 65.0;
  animal.grazing_coefficient = 0.0025;
  animal.gain_energy_ceiling_mj_per_kg = 20.3;
  return animal;
}

AnimalState steady_ewe() {
  AnimalState state;
  state.liveweight_kg = 60.0;
  state.age_days = 1200.0;
  state.liveweight_change_kg_per_day = 0.0;
  return state;
}

GrazingConditions flat_paddock() {
  GrazingConditions ground;
  ground.pasture_mass_t_dm_per_ha = 2.5;
  ground.area_per_animal_ha = 0.1;
  return ground;
}

// The round trip, and the reason to trust either direction. Ask what an animal
// holding its weight needs, feed it exactly that, and its weight must hold.
TEST(LiveweightResponseTest, FeedingExactlyTheRequirementHoldsTheWeightSteady) {
  const AnimalClassParameters animal = ewe();
  const AnimalState state = steady_ewe();
  const DietQuality diet = pasture_diet();
  const GrazingConditions ground = flat_paddock();

  const EnergyRequirement need = daily_energy_requirement(animal, state, diet, ground);
  const LiveweightResponse got =
      liveweight_response(animal, state, diet, ground, need.intake_kg_dm);

  EXPECT_NEAR(got.liveweight_change_kg, 0.0, 1e-6)
      << "fed " << need.intake_kg_dm << " kg DM, changed " << got.liveweight_change_kg << " kg";

  // The energy tolerance is looser than the weight one, and deliberately: the
  // forward direction stops iterating when maintenance moves by less than
  // 0.1 MJ, which is the manual's own criterion (TMC section 5.2.2), so its
  // answer is not carried to machine precision and the inverse cannot agree
  // more closely than the forward calculation agrees with itself. In practice
  // the residual is a few microjoules; a millijoule is a bound that means
  // something without demanding precision the source does not have.
  EXPECT_NEAR(got.surplus_me_mj, 0.0, 1e-3);
}

// And the same round trip for an animal that is meant to be growing: ask what a
// 100 g/day gain needs, feed that, and get 100 g/day back.
TEST(LiveweightResponseTest, TheRoundTripHoldsForAGrowingAnimalToo) {
  const AnimalClassParameters animal = ewe();
  AnimalState growing = steady_ewe();
  growing.liveweight_change_kg_per_day = 0.1;

  const DietQuality diet = pasture_diet();
  const GrazingConditions ground = flat_paddock();

  const EnergyRequirement need = daily_energy_requirement(animal, growing, diet, ground);
  const LiveweightResponse got =
      liveweight_response(animal, growing, diet, ground, need.intake_kg_dm);

  EXPECT_NEAR(got.liveweight_change_kg, 0.1, 1e-4)
      << "asked for 0.1 kg/day, got " << got.liveweight_change_kg;
  EXPECT_FALSE(got.losing);
}

// The gap this closes: eating less than maintenance costs weight.
TEST(LiveweightResponseTest, AnUnderfedAnimalLosesWeight) {
  const AnimalClassParameters animal = ewe();
  const AnimalState state = steady_ewe();
  const DietQuality diet = pasture_diet();
  const GrazingConditions ground = flat_paddock();

  const EnergyRequirement need = daily_energy_requirement(animal, state, diet, ground);
  const LiveweightResponse half_fed =
      liveweight_response(animal, state, diet, ground, need.intake_kg_dm * 0.5);

  EXPECT_TRUE(half_fed.losing);
  EXPECT_LT(half_fed.liveweight_change_kg, 0.0);
  EXPECT_LT(half_fed.surplus_me_mj, 0.0);
}

// More feed means more gain, monotonically. A model where it did not would rank
// grazing systems arbitrarily.
TEST(LiveweightResponseTest, MoreFeedMeansMoreGain) {
  const AnimalClassParameters animal = ewe();
  const AnimalState state = steady_ewe();
  const DietQuality diet = pasture_diet();
  const GrazingConditions ground = flat_paddock();

  double previous = -1e9;
  for (const double intake : {0.4, 0.6, 0.8, 1.0, 1.2, 1.4}) {
    const LiveweightResponse response = liveweight_response(animal, state, diet, ground, intake);
    EXPECT_GT(response.liveweight_change_kg, previous) << "at " << intake << " kg DM";
    previous = response.liveweight_change_kg;
  }
}

// TMC Eq. 8: a non-lactating animal losing weight does so at km / 0.8, which is
// a better efficiency than the kgf that applies to gain. So the asymmetry is
// real and the model has to carry it: the same energy gap in either direction
// does not move the animal by the same amount.
TEST(LiveweightResponseTest, LosingWeightUsesADifferentEfficiencyFromGainingIt) {
  const DietQuality diet = pasture_diet();

  // kgf = 0.042 * 10.5 + 0.006 = 0.447; km = 0.7027, so km / 0.8 = 0.8784.
  EXPECT_NEAR(diet.gain_efficiency(), 0.447, 1e-3);
  EXPECT_NEAR(diet.loss_efficiency(), diet.maintenance_efficiency() / 0.8, 1e-12);
  EXPECT_GT(diet.loss_efficiency(), diet.gain_efficiency())
      << "mobilising tissue is more efficient than depositing it";

  const AnimalClassParameters animal = ewe();
  const AnimalState state = steady_ewe();
  const GrazingConditions ground = flat_paddock();
  const EnergyRequirement need = daily_energy_requirement(animal, state, diet, ground);

  // The same surplus and deficit in megajoules, either side of maintenance.
  const double step_kg_dm = 0.2;
  const LiveweightResponse over =
      liveweight_response(animal, state, diet, ground, need.intake_kg_dm + step_kg_dm);
  const LiveweightResponse under =
      liveweight_response(animal, state, diet, ground, need.intake_kg_dm - step_kg_dm);

  EXPECT_GT(over.liveweight_change_kg, 0.0);
  EXPECT_LT(under.liveweight_change_kg, 0.0);
  // The loss is larger in magnitude, because the better efficiency turns the
  // same megajoules into more kilograms.
  EXPECT_GT(std::abs(under.liveweight_change_kg), over.liveweight_change_kg)
      << "gain " << over.liveweight_change_kg << ", loss " << under.liveweight_change_kg;
}

// The energy value of gain depends on the rate, and the rate is what is being
// solved for, so the loop has to settle - and be bounded whether it does or not.
TEST(LiveweightResponseTest, TheRateLoopSettlesWithinItsBound) {
  const AnimalClassParameters animal = ewe();
  const AnimalState state = steady_ewe();

  for (const double intake : {0.3, 0.75, 1.5, 2.5}) {
    const LiveweightResponse response =
        liveweight_response(animal, state, pasture_diet(), flat_paddock(), intake);
    EXPECT_LE(response.iterations, 5) << "at " << intake << " kg DM";
    EXPECT_TRUE(response.converged) << "at " << intake << " kg DM after " << response.iterations;
  }
}

TEST(LiveweightResponseTest, AnImpossibleIntakeOrAnimalIsRefused) {
  const AnimalClassParameters animal = ewe();
  const AnimalState state = steady_ewe();

  EXPECT_THROW(
      static_cast<void>(liveweight_response(animal, state, pasture_diet(), flat_paddock(), -1.0)),
      std::invalid_argument);

  AnimalState weightless = state;
  weightless.liveweight_kg = 0.0;
  EXPECT_THROW(static_cast<void>(
                   liveweight_response(animal, weightless, pasture_diet(), flat_paddock(), 1.0)),
               std::invalid_argument);
}

}  // namespace
}  // namespace paddock::core
