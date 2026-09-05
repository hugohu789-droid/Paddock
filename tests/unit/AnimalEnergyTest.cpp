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
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>

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
// **Appetite**, checked against the two things the paper says about it that do
// not depend on reading its parameter table: where the quadratic peaks, and
// what Fig. 2 draws.
AnimalClassParameters an_ewe_with_appetite() {
  AnimalClassParameters ewe;
  ewe.standard_reference_weight_kg = 66.0;
  ewe.appetite_scalar_per_day = 0.04;
  ewe.appetite_size_coefficient = 1.7;
  ewe.normal_weight_rate = 0.0157;
  ewe.normal_weight_exponent = 0.27;
  ewe.normal_weight_blend = 0.4;
  ewe.condition_intake_limit = 1.5;
  ewe.appetite_lactation_peak_days = 28.0;
  ewe.appetite_lactation_curve_exponent = 1.4;
  ewe.appetite_lactation_peak_no_young = 0.524;
  ewe.appetite_lactation_peak_one_young = 0.524;
  ewe.appetite_lactation_peak_two_young = 0.707;
  ewe.appetite_lactation_peak_three_young = 0.891;
  return ewe;
}

AnimalState a_mature_ewe(double liveweight_kg) {
  AnimalState state;
  state.liveweight_kg = liveweight_kg;
  state.age_days = 1500.0;
  return state;
}

// **Validation.** The text says potential intake peaks at a relative size of
// 0.85. That is a property of the coefficient alone, and it is how the value
// 1.7 was confirmed against a table whose columns do not survive extraction.
TEST(PotentialIntakeTest, PeaksAtEightyFivePercentOfMatureSize) {
  const AnimalClassParameters ewe = an_ewe_with_appetite();

  // Relative size is a frame, not a weight, so it is swept by growing the
  // animal up rather than by starving it down: a mature ewe reads Z near one
  // however light she is, which is the whole point of Eq. 1a.
  double best = 0.0;
  double best_z = 0.0;
  for (int age = 10; age <= 3000; age += 10) {
    // An animal at or above its frame reports the frame itself, so this reads
    // Brody's ceiling out of Eq. 1 and then stands the ewe exactly on it - a
    // relative condition of one, where neither the condition factor nor Eq. 1a's
    // blend is in play and the quadratic is all that is left.
    AnimalState probe;
    probe.age_days = age;
    probe.liveweight_kg = ewe.standard_reference_weight_kg * 10.0;
    const double frame = normal_weight_kg(ewe, probe);

    AnimalState growing;
    growing.age_days = age;
    growing.liveweight_kg = frame;
    ASSERT_NEAR(relative_condition(ewe, growing), 1.0, 1e-9) << "at age " << age;

    const double z = relative_size(ewe, growing);
    const double intake = potential_intake_kg_dm(ewe, growing);
    if (intake > best) {
      best = intake;
      best_z = z;
    }
  }
  EXPECT_NEAR(best_z, 0.85, 0.02) << "the quadratic should peak at a relative size of 0.85";
}

// **Validation.** Fig. 2 draws a sheep of 50 kg standard reference weight
// peaking a little under 1.5 kg DM a day, on an axis that tops out at 1.6.
TEST(PotentialIntakeTest, MatchesTheFiftyKilogramSheepOfFigureTwo) {
  AnimalClassParameters sheep = an_ewe_with_appetite();
  sheep.standard_reference_weight_kg = 50.0;

  const double at_peak = potential_intake_kg_dm(sheep, a_mature_ewe(0.85 * 50.0));
  EXPECT_NEAR(at_peak, 1.445, 0.01) << "0.04 * 50 * 0.85 * 0.85";
  EXPECT_LT(at_peak, 1.6) << "under the top of the figure's axis";

  // And a smaller sheep eats less, which is the figure's dashed line.
  AnimalClassParameters smaller = sheep;
  smaller.standard_reference_weight_kg = 40.0;
  EXPECT_LT(potential_intake_kg_dm(smaller, a_mature_ewe(0.85 * 40.0)), at_peak);
}

// **The headroom is the point.** An appetite that did not exceed the animal's
// requirement would leave nothing for the availability term to eat into, and
// every mob would be permanently short the moment a paddock was less than
// perfect. This is the number that makes the pairing work (E71).
TEST(PotentialIntakeTest, AMatureEweWantsMoreThanSheNeedsAtMaintenance) {
  const AnimalClassParameters ewe = an_ewe_with_appetite();
  const double appetite = potential_intake_kg_dm(ewe, a_mature_ewe(66.0));
  EXPECT_NEAR(appetite, 1.848, 0.01) << "0.04 * 66 * 1.0 * 0.7";

  // A dry ewe at maintenance eats nearer 1.2 kg DM a day on this diet, so the
  // availability term has to fall below about two thirds before it binds.
  EXPECT_GT(appetite / 1.2, 1.5) << "half again as much appetite as requirement";
}

// **A class that states no scalar has no appetite**, which is every animal file
// written before this one and is how callers know not to apply a ceiling.
TEST(PotentialIntakeTest, ASilentAnimalReportsNoAppetite) {
  AnimalClassParameters silent;
  silent.standard_reference_weight_kg = 66.0;
  EXPECT_DOUBLE_EQ(potential_intake_kg_dm(silent, a_mature_ewe(66.0)), 0.0);
}

// **A ewe in milk eats about half again what a dry ewe eats**, which is the
// factor whose absence stopped intake capacity reaching the farm at all (E71).
// At peak - 28 days, where M is one - GrazPlan Eq. 8 reduces to 1 + C_I19,Y.
TEST(PotentialIntakeTest, ALactatingEweWantsHalfAgainWhatADryOneWants) {
  const AnimalClassParameters ewe = an_ewe_with_appetite();

  const double dry = potential_intake_kg_dm(ewe, a_mature_ewe(66.0));

  AnimalState milking = a_mature_ewe(66.0);
  milking.days_lactating = 28;
  milking.young = 1.0;
  const double at_peak = potential_intake_kg_dm(ewe, milking);

  EXPECT_NEAR(at_peak / dry, 1.524, 0.001) << "1 + C_I19,1 at the peak of the curve";
  EXPECT_GT(at_peak, 2.7) << "about 2.8 kg DM a day, against a requirement nearer 2.6";

  // Twins are hungrier than a single, and triplets hungrier again.
  AnimalState twins = milking;
  twins.young = 2.0;
  AnimalState triplets = milking;
  triplets.young = 3.0;
  EXPECT_GT(potential_intake_kg_dm(ewe, twins), at_peak);
  EXPECT_GT(potential_intake_kg_dm(ewe, triplets), potential_intake_kg_dm(ewe, twins));

  // And the curve comes back down: a ewe four months into lactation is not
  // eating what she ate at a month.
  AnimalState late = milking;
  late.days_lactating = 120;
  EXPECT_LT(potential_intake_kg_dm(ewe, late), at_peak);
  EXPECT_GT(potential_intake_kg_dm(ewe, late), dry) << "but still more than dry";
}

// **A ewe carrying condition eats less, and does not stop.** The point of the
// factor is that her intake falls until it meets what holds her, so she settles
// at a weight rather than growing without limit.
TEST(PotentialIntakeTest, AFatEweEatsLessButKeepsEating) {
  const AnimalClassParameters ewe = an_ewe_with_appetite();

  const double normal = potential_intake_kg_dm(ewe, a_mature_ewe(66.0));
  const double carrying = potential_intake_kg_dm(ewe, a_mature_ewe(76.0));
  const double fat = potential_intake_kg_dm(ewe, a_mature_ewe(85.0));

  EXPECT_LT(carrying, normal) << "condition should take the edge off her appetite";
  EXPECT_LT(fat, carrying) << "and more of it as she carries more";
  EXPECT_GT(fat, 0.0) << "but she does not stop eating";

  // A ewe in milk gets no such brake: she has somewhere to put the energy.
  AnimalState milking = a_mature_ewe(76.0);
  milking.days_lactating = 28;
  milking.young = 1.0;
  EXPECT_GT(potential_intake_kg_dm(ewe, milking), normal);
}

// **Normal weight is a frame, and a frame does not shrink in a drought.** This
// is GrazPlan Eq. 1a, and it is what lets an animal come out of a hard season
// light rather than permanently small.
TEST(PotentialIntakeTest, ALightEweKeepsTheFrameSheGrew) {
  const AnimalClassParameters ewe = an_ewe_with_appetite();

  const AnimalState well_fed = a_mature_ewe(66.0);
  const AnimalState pinched = a_mature_ewe(52.0);

  EXPECT_GT(normal_weight_kg(ewe, pinched), pinched.liveweight_kg)
      << "her frame should be bigger than she is";
  EXPECT_LT(relative_condition(ewe, pinched), 1.0) << "so she reads as light for it";
  EXPECT_NEAR(relative_condition(ewe, well_fed), 1.0, 0.05);

  // And relative size never passes one, however heavy she gets.
  EXPECT_LE(relative_size(ewe, a_mature_ewe(120.0)), 1.0);
}

// **What a short paddock does to intake**, checked against GrazPlan's own
// Fig. 4 rather than against anything this farm produces.
AnimalClassParameters a_grazing_sheep() {
  AnimalClassParameters sheep;
  sheep.intake_availability_rate_per_kg_dm = 0.00112;
  sheep.intake_grazing_time_increase = 0.6;
  sheep.intake_grazing_time_rate_per_kg_dm = 0.00112;
  return sheep;
}

// **Validation.** The paper plots relative time spent grazing against herbage
// weight and its upper line starts at 1.6 on a bare paddock. Equation 17 gives
// 1 + C_R5 there, so the figure fixes C_R5 at 0.6 - which is how the sheep row
// of Table 2 was read, since the table does not survive text extraction with
// its columns intact. This asserts the reading.
TEST(RelativeIntakeTest, AnimalGrazesHalfAsLongAgainWhenThereIsNothingToEat) {
  const AnimalClassParameters sheep = a_grazing_sheep();

  // Rate is zero on bare ground, so the product is zero however long it grazes.
  EXPECT_DOUBLE_EQ(relative_intake(sheep, 0.0), 0.0);

  // Approaching zero the time term is its full 1 + C_R5 and the rate term is
  // C_R4 * B to first order, so the product tends to 1.6 * C_R4 * B. Taken at a
  // herbage weight small enough that the second-order term of the exponential
  // is out of sight: this is a check on the limit, not on a real paddock.
  const double trace = 0.01;
  EXPECT_NEAR(relative_intake(sheep, trace), 1.6 * 0.00112 * trace, 1e-9)
      << "the upper line of Fig. 4 starts at 1.6";
}

// **Validation.** The middle line of Fig. 4 - the product - rises steeply and is
// approaching but has not reached one by the right-hand edge of the plot at
// 2,500 kg DM/ha.
TEST(RelativeIntakeTest, RisesWithHerbageAndIsNearlyUnrestrictedByTwoAndAHalfTonnes) {
  const AnimalClassParameters sheep = a_grazing_sheep();

  double previous = 0.0;
  for (int step = 1; step <= 30; ++step) {
    const double herbage = step * 100.0;
    const double now = relative_intake(sheep, herbage);
    EXPECT_GT(now, previous) << "more grass should never mean less intake, at " << herbage;
    EXPECT_LE(now, 1.05) << "and should not promise more than the animal wanted, at " << herbage;
    previous = now;
  }

  EXPECT_GT(relative_intake(sheep, 2500.0), 0.90) << "nearly unrestricted at 2,500 kg DM/ha";
  EXPECT_LT(relative_intake(sheep, 2500.0), 1.00) << "but not quite there, as the figure shows";

  // A short paddock is a real restriction and not a rounding error: at 500 kg
  // DM/ha a ewe gets about two thirds of what she came for.
  EXPECT_NEAR(relative_intake(sheep, 500.0), 0.66, 0.05);
}

// **Verification.** A cow's mouth is bigger, so it clears a given sward faster
// than a sheep does - GrazPlan gives cattle a lower rate coefficient, which
// makes the same herbage weight less restrictive per unit of appetite.
TEST(RelativeIntakeTest, CattleAndSheepDifferAsTheirCoefficientsDo) {
  const AnimalClassParameters sheep = a_grazing_sheep();
  AnimalClassParameters cow;
  cow.intake_availability_rate_per_kg_dm = 0.00078;
  cow.intake_grazing_time_increase = 0.6;
  cow.intake_grazing_time_rate_per_kg_dm = 0.00074;

  EXPECT_LT(relative_intake(cow, 1000.0), relative_intake(sheep, 1000.0))
      << "a cow needs more standing grass than a sheep to graze unrestricted";
}

// **An animal that states no coefficients keeps the appetite it always had**,
// which is what every species file written before this carries.
TEST(RelativeIntakeTest, ASilentAnimalIsUnrestricted) {
  const AnimalClassParameters silent;
  for (int step = 0; step <= 12; ++step) {
    const double herbage = step * 250.0;
    EXPECT_DOUBLE_EQ(relative_intake(silent, herbage), 1.0) << "at " << herbage;
  }
}

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
  // Regression pins, and the bracket quoted in docs/validation/verify.md: about 11 MJ/kg
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
  // eats. See docs/validation/verify.md for what is still missing.
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

// TMC Table 30, and the slope boundaries this project maps onto it. The values
// are published (Nicol and Brookes 2007 via OVERSEER v6.3); the degrees where
// one class becomes the next are this model's decision, so both are pinned.
TEST(WalkingDistanceTest, EachTopographyClassGetsItsPublishedDistance) {
  struct Case {
    double slope_degrees;
    double horizontal_km;
    double vertical_km;
  };

  // A representative slope from inside each LUC band, and the two degrees on
  // either side of every boundary.
  const std::vector<Case> cases{
      {0.0, 0.5, 0.0},   {3.0, 0.5, 0.0},   {7.0, 0.5, 0.0},  // flat: LUC A and B
      {8.0, 1.0, 0.1},   {15.0, 1.0, 0.1},                    // rolling: LUC C
      {16.0, 1.5, 0.15}, {25.0, 1.5, 0.15},                   // easy hill: LUC D and E
      {26.0, 2.0, 0.2},  {40.0, 2.0, 0.2},                    // steep hill: LUC F and G
  };

  for (const Case& one : cases) {
    const WalkingDistance walk = walking_distance_on(one.slope_degrees);
    EXPECT_DOUBLE_EQ(walk.horizontal_km_per_day, one.horizontal_km)
        << "horizontal distance at " << one.slope_degrees << " degrees";
    EXPECT_DOUBLE_EQ(walk.vertical_km_per_day, one.vertical_km)
        << "vertical distance at " << one.slope_degrees << " degrees";
  }
}

// A terrain model that produced a negative slope would be broken, and reading it
// as steep would quietly charge the animals for it.
TEST(WalkingDistanceTest, NegativeSlopeIsReadAsFlatRatherThanSteep) {
  const WalkingDistance walk = walking_distance_on(-5.0);
  EXPECT_DOUBLE_EQ(walk.horizontal_km_per_day, 0.5);
  EXPECT_DOUBLE_EQ(walk.vertical_km_per_day, 0.0);
}

// The point of E10: the term exists, is summed into maintenance, and is no
// longer zero for an animal standing on real ground.
TEST(WalkingDistanceTest, ActivityReachesTheRequirementOnSlopingGround) {
  AnimalClassParameters animal;
  animal.class_id = "sheep_ewe";
  animal.species_factor = 1.0;
  animal.sex_factor = 1.0;
  animal.standard_reference_weight_kg = 65.0;
  animal.grazing_coefficient = 0.0025;
  animal.gain_energy_ceiling_mj_per_kg = 20.3;

  AnimalState state;
  state.liveweight_kg = 60.0;
  state.age_days = 1200.0;

  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;

  GrazingConditions flat;
  flat.pasture_mass_t_dm_per_ha = 2.0;
  flat.area_per_animal_ha = 0.2;
  flat.slope_degrees = 0.0;
  const WalkingDistance on_flat = walking_distance_on(flat.slope_degrees);
  flat.horizontal_km_per_day = on_flat.horizontal_km_per_day;
  flat.vertical_km_per_day = on_flat.vertical_km_per_day;

  GrazingConditions hill = flat;
  hill.slope_degrees = 30.0;
  const WalkingDistance on_hill = walking_distance_on(hill.slope_degrees);
  hill.horizontal_km_per_day = on_hill.horizontal_km_per_day;
  hill.vertical_km_per_day = on_hill.vertical_km_per_day;

  const EnergyRequirement on_the_flat = daily_energy_requirement(animal, state, diet, flat);
  const EnergyRequirement on_the_hill = daily_energy_requirement(animal, state, diet, hill);

  EXPECT_GT(on_the_flat.activity_net_mj, 0.0) << "the activity term is fed on flat ground too";
  EXPECT_GT(on_the_hill.activity_net_mj, on_the_flat.activity_net_mj)
      << "a steep paddock costs more walking than a terrace";
  EXPECT_GT(on_the_hill.total_me_mj, on_the_flat.total_me_mj)
      << "and that reaches what the animal has to eat";
}

// **The two lactation timings are independent, and these four tests are the
// proof that keeps them so** (verify.md, E110).
//
// The model carries two curves that both have the word lactation in them and
// answer different questions. Milk yield is OVERSEER TMC Eq. 35, whose peak is
// welded into its own fitted constants. Appetite is GrazPlan Eq. 8, whose peak
// is `appetite_lactation_peak_days`. Before E110 one of them was called
// `lactation_peak_days`, which read as a claim about the other; it never
// produced a wrong number and it did mislead two reviews. These assert the
// separation the rename describes, so it cannot quietly stop being true.

/// A ewe far enough into lactation for both curves to be live.
AnimalState a_lactating_ewe(int days_lactating, double young = 1.0) {
  AnimalState state;
  state.liveweight_kg = 55.0;
  state.age_days = 1500.0;
  state.days_lactating = days_lactating;
  state.young = young;
  return state;
}

GrazingConditions an_ordinary_paddock() {
  GrazingConditions ground;
  ground.pasture_mass_t_dm_per_ha = 2.5;
  return ground;
}

/// The day a series of daily values is largest.
int day_of_peak(const std::function<double(int)>& value, int last_day) {
  int best_day = 1;
  double best = value(1);
  for (int day = 2; day <= last_day; ++day) {
    if (value(day) > best) {
      best = value(day);
      best_day = day;
    }
  }
  return best_day;
}

// **Verification.** Move the appetite parameter across four values spanning
// sheep, a doubled sheep and GrazPlan's cattle, and the milk series must not
// move by a single bit. This is the assertion that would have caught the
// misreading directly.
TEST(LactationTimingTest, MilkYieldTimingIgnoresTheAppetiteParameter) {
  const GrazingConditions ground = an_ordinary_paddock();

  std::vector<double> reference;
  for (int day = 1; day <= 100; ++day) {
    AnimalClassParameters ewe = an_ewe_with_appetite();
    ewe.milk_fat_percent = 7.0;
    ewe.milk_protein_percent = 5.5;
    ewe.gestation_length_days = 147.0;
    reference.push_back(daily_milk_yield_kg(ewe, a_lactating_ewe(day), ground));
  }
  ASSERT_GT(reference[13], 0.0) << "the curve has to be live for this to mean anything";

  for (const double peak_days : {14.0, 28.0, 56.0, 624.0}) {
    for (int day = 1; day <= 100; ++day) {
      AnimalClassParameters ewe = an_ewe_with_appetite();
      ewe.milk_fat_percent = 7.0;
      ewe.milk_protein_percent = 5.5;
      ewe.gestation_length_days = 147.0;
      ewe.appetite_lactation_peak_days = peak_days;
      // Bit-identical, not merely close: no appetite parameter is an input to
      // Eq. 35, so the two series come from the same arithmetic.
      EXPECT_EQ(daily_milk_yield_kg(ewe, a_lactating_ewe(day), ground),
                reference[static_cast<std::size_t>(day - 1)])
          << "milk yield moved on day " << day << " when the appetite peak was set to "
          << peak_days;
    }
  }
}

// **Verification.** `kMilkYieldPeakDays` is 0.41/0.0287 by derivation; this
// checks the curve the constant describes actually peaks there.
TEST(LactationTimingTest, MilkYieldPeaksWhereTmcEq35PutsIt) {
  AnimalClassParameters ewe = an_ewe_with_appetite();
  ewe.milk_fat_percent = 7.0;
  ewe.milk_protein_percent = 5.5;
  ewe.gestation_length_days = 147.0;
  const GrazingConditions ground = an_ordinary_paddock();

  EXPECT_NEAR(kMilkYieldPeakDays, 14.29, 0.01) << "0.41/0.0287";

  const int peak = day_of_peak(
      [&](int day) { return daily_milk_yield_kg(ewe, a_lactating_ewe(day), ground); }, 120);

  // The constant is 14.29, so the largest whole day is 14.
  EXPECT_EQ(peak, static_cast<int>(kMilkYieldPeakDays))
      << "TMC Eq. 35 peaks at 0.41/0.0287 days and nothing in this model configures it";
}

// **Verification.** The appetite peak is wherever its own parameter says, and
// the assertion sweeps three values so it cannot pass by coincidence at 28.
TEST(LactationTimingTest, AppetitePeaksOnItsOwnParameter) {
  for (const double peak_days : {14.0, 28.0, 56.0}) {
    AnimalClassParameters ewe = an_ewe_with_appetite();
    ewe.appetite_lactation_peak_days = peak_days;

    const int peak = day_of_peak(
        [&](int day) {
          AnimalState state = a_lactating_ewe(day);
          // Hold the frame still: this is a statement about the lactation term,
          // not about a ewe growing across four months.
          state.age_days = 1500.0;
          return potential_intake_kg_dm(ewe, state);
        },
        200);

    EXPECT_EQ(peak, static_cast<int>(peak_days))
        << "GrazPlan Eq. 8 peaks where M = 1, so appetite peaks on C_I8 itself";
  }
}

// **Verification, and the clearest statement of why the old name was wrong.**
// GrazPlan gives cattle C_I8 = 624. A cow's milk peaks near day 60, so read as
// a lactation peak the number is absurd; read as the appetite time constant it
// is simply the published cattle value. The milk constant is unmoved by it.
TEST(LactationTimingTest, TheCattleConstantIsNotAMilkPeak) {
  AnimalClassParameters cow = an_ewe_with_appetite();
  cow.appetite_lactation_peak_days = 624.0;
  cow.appetite_lactation_curve_exponent = 1.72;
  cow.appetite_lactation_peak_no_young = 0.4162;
  cow.appetite_lactation_peak_one_young = 0.4162;

  EXPECT_GT(cow.appetite_lactation_peak_days, 365.0)
      << "longer than a whole lactation, which only makes sense as an appetite constant";
  EXPECT_LT(kMilkYieldPeakDays, 15.0) << "and the milk peak is unaffected by any of it";

  // The appetite curve is still rising at a year, which is what a 624-day time
  // constant means and what no milk curve does.
  const AnimalState at_six_months = a_lactating_ewe(180);
  const AnimalState at_a_year = a_lactating_ewe(365);
  EXPECT_GT(potential_intake_kg_dm(cow, at_a_year), potential_intake_kg_dm(cow, at_six_months))
      << "C_I8 = 624 is a time constant, not the day anything peaks in the field";
}

}  // namespace
}  // namespace paddock::core
