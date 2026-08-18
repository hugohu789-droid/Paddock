// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// What a grazing animal needs to eat.
//
// The numbers here are of three kinds and each assertion says which:
//
//  * Validation - a value someone else published. Frater et al.'s worked lamb
//    and Simpson's (1978b) maintenance figure are both checks against people
//    who did not write this code.
//  * Verification - arithmetic a reader can repeat. 1 + tan(45) is 2.
//  * Regression pin - came out of this implementation, kept so a change has to
//    be deliberate.

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

#include <paddock/core/AnimalEnergy.hpp>

namespace paddock::core {
namespace {

/// A pasture diet of 10.5 MJ ME/kg DM, which is the diet Frater et al. state.
DietQuality pasture_diet() {
  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  diet.milk_fraction = 0.0;
  return diet;
}

/// A weaned lamb, as far as Frater et al. describe one.
///
/// K is 1.0 for sheep, which is the one species factor CSIRO and Nicol and
/// Brookes agree on. S is 1.0 - the paper does not say, and 1.0 is the value
/// for females and castrates. SpGraze is 0 because Frater cost grazing as a
/// flat 15% on maintenance rather than through the chewing and movement
/// equations, and this test reproduces their method, not the manual's.
AnimalClassParameters lamb(double standard_reference_weight_kg) {
  AnimalClassParameters animal;
  animal.class_id = "lamb";
  animal.species_factor = 1.0;
  animal.sex_factor = 1.0;
  animal.standard_reference_weight_kg = standard_reference_weight_kg;
  animal.grazing_coefficient = 0.0;
  animal.gain_energy_ceiling_mj_per_kg = 20.3;
  return animal;
}

AnimalState growing_lamb() {
  AnimalState state;
  state.liveweight_kg = 28.0;
  state.age_days = 100.0;
  state.liveweight_change_kg_per_day = 0.1;
  return state;
}

/// Frater et al.'s method: maintenance plus 15% for grazing, then the cost of
/// gain, with the production share of maintenance the manual adds at Eq. 54.
double frater_total_me(double standard_reference_weight_kg) {
  const AnimalClassParameters animal = lamb(standard_reference_weight_kg);
  const AnimalState state = growing_lamb();
  const DietQuality diet = pasture_diet();

  const double basal_me = basal_net_energy_mj(animal, state) / diet.maintenance_efficiency();
  const double gain_me = liveweight_change_net_energy_mj(animal, state) / diet.gain_efficiency();
  return (basal_me * 1.15) + (0.1 * gain_me) + gain_me;
}

// VALIDATION. Frater, Howarth and McEwen, J. NZ Grasslands, Table 1: a weaned
// lamb of 28 kg gaining 100 g/day at 100 days old on a 10.5 MJ ME/kg DM diet
// needs 9.72 MJ ME/day by Nicol and Brookes (2007), 9.71 by CSIRO (2007), and
// 8.77 by OVERSEER.
//
// The paper does not state the standard reference weight, and the answer moves
// with it, so the honest test is that the published figure falls inside the
// range a New Zealand ewe's mature weight plausibly spans rather than that the
// model hits 9.72 on a number chosen to make it do so.
TEST(AnimalEnergyTest, ReproducesFraterWorkedLambWithinTheUnstatedReferenceWeight) {
  const double at_60 = frater_total_me(60.0);
  const double at_65 = frater_total_me(65.0);

  // A heavier mature weight makes a 28 kg lamb less mature, and immature gain
  // is leaner and so cheaper. The ordering is biology and holds regardless of
  // this code.
  EXPECT_GT(at_60, at_65) << "at 60 kg " << at_60 << ", at 65 kg " << at_65;

  EXPECT_LT(at_65, 9.72) << "at 65 kg SRW: " << at_65;
  EXPECT_GT(at_60, 9.72) << "at 60 kg SRW: " << at_60;

  // And the whole range is close to the published value, not merely straddling
  // it: 5% either side of 9.72.
  EXPECT_NEAR(at_60, 9.72, 0.49) << at_60;
  EXPECT_NEAR(at_65, 9.72, 0.49) << at_65;

  GTEST_LOG_(INFO) << "Frater lamb: " << at_60 << " MJ ME/day at SRW 60, " << at_65
                   << " at SRW 65; published 9.72";
}

// VALIDATION, against a different author two decades earlier. The manual quotes
// Simpson (1978b) as reporting maintenance of 0.40 MJ ME per kg lwt^0.75 for
// sheep. The basal net rate of 0.28 divided by km on a pasture diet has to
// reproduce it, and there is no free parameter in between.
//
// This is also what settles which way round the manual's Eq. 5 and Eq. 6 go.
TEST(AnimalEnergyTest, MaintenanceMatchesSimpsonsPublishedFigureForSheep) {
  const DietQuality diet = pasture_diet();

  // qm = 10.5 / 18.4 = 0.5707; km = 0.35 qm + 0.503.
  EXPECT_NEAR(diet.energy_density(), 0.5707, 1e-4);
  EXPECT_NEAR(diet.maintenance_efficiency(), 0.7027, 1e-4);

  const double maintenance_me_per_metabolic_kg = 0.28 / diet.maintenance_efficiency();
  EXPECT_NEAR(maintenance_me_per_metabolic_kg, 0.40, 0.005)
      << "Simpson (1978b) gives 0.40 for sheep; got " << maintenance_me_per_metabolic_kg;
}

// VERIFICATION: 1 + tan(theta) at angles whose tangents are known.
TEST(AnimalEnergyTest, TheSlopeFactorIsOnePlusTheTangent) {
  EXPECT_DOUBLE_EQ(slope_movement_factor(0.0), 1.0);
  EXPECT_NEAR(slope_movement_factor(45.0), 2.0, 1e-12);
  EXPECT_NEAR(slope_movement_factor(30.0), 1.0 + (1.0 / std::sqrt(3.0)), 1e-12);
  // The figures quoted in the header, so the documentation is tested too.
  EXPECT_NEAR(slope_movement_factor(10.0), 1.18, 0.005);
  EXPECT_NEAR(slope_movement_factor(20.0), 1.36, 0.005);
}

// The whole reason the terrain work feeds the livestock model: the same animal
// on the same feed costs more on a hill. The direction is the manual's Eq. 23
// and does not depend on this implementation.
TEST(AnimalEnergyTest, SteeperGroundCostsMoreToGrazeThanFlat) {
  const AnimalClassParameters animal = lamb(65.0);
  const AnimalState state = growing_lamb();

  GrazingConditions flat;
  flat.pasture_mass_t_dm_per_ha = 2.0;
  flat.area_per_animal_ha = 0.1;
  flat.slope_degrees = 0.0;

  GrazingConditions steep = flat;
  steep.slope_degrees = 25.0;

  const double on_flat = movement_net_energy_mj(state, flat);
  const double on_steep = movement_net_energy_mj(state, steep);

  EXPECT_GT(on_steep, on_flat);
  // The ratio is exactly the slope factor, since nothing else changed.
  EXPECT_NEAR(on_steep / on_flat, slope_movement_factor(25.0), 1e-12);
}

// A heavier cover means less walking for the same intake - TMC Eq. 22.
TEST(AnimalEnergyTest, AHeavierCoverCostsLessWalking) {
  const AnimalState state = growing_lamb();

  GrazingConditions sparse;
  sparse.pasture_mass_t_dm_per_ha = 1.2;
  sparse.area_per_animal_ha = 0.1;

  GrazingConditions heavy = sparse;
  heavy.pasture_mass_t_dm_per_ha = 3.0;

  EXPECT_GT(movement_net_energy_mj(state, sparse), movement_net_energy_mj(state, heavy));
}

// VERIFICATION: climbing costs 0.028 MJ per kg per km against 0.0026 on the
// flat, so a kilometre up is about eleven flat ones.
TEST(AnimalEnergyTest, ClimbingCostsAboutElevenTimesWalking) {
  const AnimalState state = growing_lamb();

  GrazingConditions walking;
  walking.horizontal_km_per_day = 1.0;

  GrazingConditions climbing;
  climbing.vertical_km_per_day = 1.0;

  EXPECT_NEAR(activity_net_energy_mj(state, climbing) / activity_net_energy_mj(state, walking),
              0.028 / 0.0026, 1e-12);
}

// TMC Eq. 17 with the floor Freer et al. put on it. Without the floor an
// animal old enough would need no energy at all, which is the failure a floor
// exists to prevent.
TEST(AnimalEnergyTest, TheAgeFactorFallsAndThenStops) {
  EXPECT_DOUBLE_EQ(age_factor(0.0), 1.0);
  EXPECT_NEAR(age_factor(365.0), std::exp(-0.00008 * 365.0), 1e-12);
  EXPECT_GT(age_factor(365.0), age_factor(3650.0));
  // exp(-0.00008 a) reaches 0.84 at about 2179 days, and never goes below.
  EXPECT_DOUBLE_EQ(age_factor(100000.0), 0.84);
}

// The energy in a kilogram of gain rises with maturity, because an older animal
// lays down fat where a young one lays down protein and water. TMC Eq. 44.
TEST(AnimalEnergyTest, GainCostsMoreAsAnAnimalApproachesItsMatureWeight) {
  const AnimalClassParameters animal = lamb(65.0);

  AnimalState young = growing_lamb();
  young.liveweight_kg = 13.0;  // maturity 0.2

  AnimalState nearly_mature = growing_lamb();
  nearly_mature.liveweight_kg = 65.0;  // maturity 1.0

  const double young_evg = energy_value_of_gain_mj_per_kg(animal, young);
  const double mature_evg = energy_value_of_gain_mj_per_kg(animal, nearly_mature);

  EXPECT_GT(mature_evg, young_evg);
  // Regression pins, and the bracket quoted in docs/verify.md: about 11 MJ/kg
  // early and about 26 near mature weight.
  EXPECT_NEAR(young_evg, 11.4, 0.6) << young_evg;
  EXPECT_NEAR(mature_evg, 26.5, 0.6) << mature_evg;
}

// The chewing loop has to settle, and the manual caps it at five passes whether
// it does or not. A requirement that depended on the pass count would not be
// reproducible.
TEST(AnimalEnergyTest, TheChewingIterationConvergesWithinTheManualsFivePasses) {
  // A DRY cow, and the distinction is not pedantry: lactation (TMC Eq. 33-35)
  // and pregnancy (Eq. 26-32) are not implemented, so this model has no way to
  // represent a milking cow at all. The intake below is right for an animal at
  // maintenance plus a little gain and would be roughly half what a cow in milk
  // eats. See docs/verify.md for what is still missing.
  AnimalClassParameters cow;
  cow.class_id = "dry_cow";
  cow.species_factor = 1.4;  // CSIRO (2007) for dairy
  cow.sex_factor = 1.0;
  cow.standard_reference_weight_kg = 500.0;
  cow.grazing_coefficient = 0.0025;  // TMC Eq. 20
  cow.gain_energy_ceiling_mj_per_kg = 16.5;

  AnimalState state;
  state.liveweight_kg = 480.0;
  state.age_days = 1500.0;
  state.liveweight_change_kg_per_day = 0.2;

  GrazingConditions ground;
  ground.pasture_mass_t_dm_per_ha = 2.4;
  ground.area_per_animal_ha = 0.3;
  ground.slope_degrees = 8.0;
  ground.horizontal_km_per_day = 1.5;
  ground.vertical_km_per_day = 0.05;

  const EnergyRequirement need = daily_energy_requirement(cow, state, pasture_diet(), ground);

  EXPECT_TRUE(need.converged) << "took " << need.iterations << " passes";
  EXPECT_LE(need.iterations, 5);
  EXPECT_GT(need.chewing_net_mj, 0.0) << "a grazing animal chews";
  // TMC Eq. 19 is the definition of intake, so this is arithmetic rather than
  // a claim about cows.
  EXPECT_NEAR(need.intake_kg_dm, need.total_me_mj / 10.5, 1e-12);

  GTEST_LOG_(INFO) << "dry cow: " << need.total_me_mj << " MJ ME/day, " << need.intake_kg_dm
                   << " kg DM/day, " << need.iterations << " passes";

  // A sanity bracket rather than a pin: a dry cow of this weight sits between
  // maintenance and about twice it. Simpson (1978b) gives 0.55 MJ ME per kg
  // lwt^0.75 for cattle, which is 56 MJ/day at 480 kg.
  EXPECT_GT(need.total_me_mj, 0.55 * std::pow(480.0, 0.75))
      << "below maintenance for a 480 kg animal";
  EXPECT_LT(need.total_me_mj, 2.0 * 0.55 * std::pow(480.0, 0.75));
}

// A diet that carries no energy would need an infinite intake, and a silent
// infinity here would surface as a farm that eats the country.
TEST(AnimalEnergyTest, AnImpossibleDietOrAnimalIsRefused) {
  const AnimalClassParameters animal = lamb(65.0);
  const AnimalState state = growing_lamb();
  const GrazingConditions ground;

  DietQuality no_energy = pasture_diet();
  no_energy.metabolisable_energy_mj_per_kg_dm = 0.0;
  EXPECT_THROW(static_cast<void>(daily_energy_requirement(animal, state, no_energy, ground)),
               std::invalid_argument);

  // Feed cannot carry more metabolisable than gross energy.
  DietQuality impossible = pasture_diet();
  impossible.metabolisable_energy_mj_per_kg_dm = 25.0;
  EXPECT_THROW(static_cast<void>(daily_energy_requirement(animal, state, impossible, ground)),
               std::invalid_argument);

  AnimalState weightless = state;
  weightless.liveweight_kg = 0.0;
  EXPECT_THROW(
      static_cast<void>(daily_energy_requirement(animal, weightless, pasture_diet(), ground)),
      std::invalid_argument);

  AnimalClassParameters unnamed = animal;
  unnamed.class_id.clear();
  EXPECT_THROW(static_cast<void>(daily_energy_requirement(unnamed, state, pasture_diet(), ground)),
               std::invalid_argument);
}

// The species factor is the one place the two primary sources disagree, so the
// model has to carry whichever a farm chooses rather than assume. Beef at 1.4
// (CSIRO) against 1.3 (Nicol and Brookes) is about 8% of maintenance.
TEST(AnimalEnergyTest, TheSpeciesFactorChangesMaintenanceByTheAmountTheSourcesDisagreeOn) {
  AnimalState state;
  state.liveweight_kg = 400.0;
  state.age_days = 900.0;

  AnimalClassParameters csiro = lamb(600.0);
  csiro.class_id = "beef_csiro";
  csiro.species_factor = 1.4;

  AnimalClassParameters nicol_brookes = csiro;
  nicol_brookes.class_id = "beef_nicol_brookes";
  nicol_brookes.species_factor = 1.3;

  const double with_csiro = basal_net_energy_mj(csiro, state);
  const double with_nb = basal_net_energy_mj(nicol_brookes, state);

  EXPECT_NEAR(with_csiro / with_nb, 1.4 / 1.3, 1e-12);
  EXPECT_NEAR((with_csiro - with_nb) / with_nb, 0.0769, 1e-3) << "about 8%";
}

}  // namespace
}  // namespace paddock::core
