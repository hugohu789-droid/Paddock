// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// What a pregnant or lactating ewe costs to feed.
///
/// Every equation is OVERSEER's Technical Manual (Wheeler 2018), the same
/// document the rest of `AnimalEnergy` follows, so these tests check the
/// implementation against **published magnitudes** rather than against
/// themselves: a ewe's peak milk yield, her late-pregnancy demand, and the
/// intake those imply are all quantities somebody has measured, and an
/// arithmetic test that only reproduced the formula would pass just as happily
/// with the constants transposed.

#include <gtest/gtest.h>

#include <cmath>

#include <paddock/core/AnimalEnergy.hpp>

namespace paddock::core {
namespace {

/// A Romney ewe. SRW is the manual's own Table 8 figure for the breed.
AnimalClassParameters a_ewe() {
  AnimalClassParameters ewe;
  ewe.class_id = "sheep_ewe";
  ewe.kind = AnimalKind::Sheep;
  ewe.species_factor = 1.0;
  ewe.sex_factor = 1.0;
  ewe.standard_reference_weight_kg = 66.0;
  ewe.grazing_coefficient = 0.0025;
  ewe.gain_energy_ceiling_mj_per_kg = 20.3;
  ewe.gestation_length_days = 150.0;
  ewe.milk_fat_percent = 7.0;
  ewe.milk_protein_percent = 5.8;
  ewe.breed_effect = 0.01;
  return ewe;
}

AnimalState a_ewe_state() {
  AnimalState state;
  state.liveweight_kg = 66.0;
  state.age_days = 1500.0;
  return state;
}

DietQuality pasture() {
  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

GrazingConditions a_paddock() {
  GrazingConditions ground;
  ground.pasture_mass_t_dm_per_ha = 2.0;
  ground.slope_degrees = 0.0;
  ground.area_per_animal_ha = 0.2;
  return ground;
}

// TMC Eq. 3. On the 10.5 MJ/kg diet the rest of the model runs on, kl is 0.62 -
// noticeably better than pregnancy's 0.13 and a little worse than maintenance.
TEST(ReproductionEnergyTest, LactationEfficiencyFollowsTheDiet) {
  EXPECT_NEAR(pasture().lactation_efficiency(), 0.6197, 1e-3);
  EXPECT_LT(kPregnancyEfficiency, pasture().lactation_efficiency())
      << "growing a lamb is the least efficient thing a ewe does with a megajoule";
}

// TMC Eq. 46. Ewe milk carries roughly twice the energy of cow milk, which is
// the check that the sheep constant was used and not the dairy one.
TEST(ReproductionEnergyTest, MilkCarriesAboutFiveMegajoulesAKilogram) {
  EXPECT_NEAR(milk_net_energy_mj_per_kg(a_ewe()), 4.82, 0.02);
}

// TMC Eq. 11-14: a lamb is born at a share of its dam's standard reference
// weight, and the share falls as the litter grows.
TEST(ReproductionEnergyTest, ALambIsBornAtAShareOfItsDamsReferenceWeight) {
  const AnimalClassParameters ewe = a_ewe();

  EXPECT_NEAR(birth_weight_kg(ewe, 1.0), 6.6, 1e-9) << "0.100 of 66 kg";
  EXPECT_NEAR(birth_weight_kg(ewe, 2.0), 5.61, 1e-9) << "0.085 of 66 kg for a twin";
  EXPECT_NEAR(birth_weight_kg(ewe, 3.0), 4.62, 1e-9);

  // A flock's mean litter falls between the published shares, and so does the
  // birth weight it implies.
  const double at_national_average = birth_weight_kg(ewe, 1.323);
  EXPECT_LT(at_national_average, birth_weight_kg(ewe, 1.0));
  EXPECT_GT(at_national_average, birth_weight_kg(ewe, 2.0));
}

// **The shape that matters.** Almost the whole cost of a pregnancy falls in its
// last weeks, which is why a New Zealand farmer's feed budget tightens before
// lambing and not after mating.
TEST(ReproductionEnergyTest, PregnancyCostsAlmostNothingUntilItCostsALot) {
  const AnimalClassParameters ewe = a_ewe();
  AnimalState state = a_ewe_state();
  state.young = 1.0;

  state.days_pregnant = 1;
  const double at_conception = pregnancy_net_energy_mj(ewe, state);
  state.days_pregnant = 75;
  const double at_half = pregnancy_net_energy_mj(ewe, state);
  state.days_pregnant = 150;
  const double at_term = pregnancy_net_energy_mj(ewe, state);

  EXPECT_LT(at_conception, at_term * 0.01) << "under a hundredth of the cost at the start";
  EXPECT_LT(at_half, at_term * 0.2) << "and under a fifth at halfway";
  EXPECT_GT(at_term, at_half);

  // At term, a single-bearing ewe: about 1.1 MJ NE, which over kp of 0.13 is
  // near 9 MJ ME a day. Published late-pregnancy allowances for a single-
  // bearing ewe sit in that region, and this is the assertion that would catch
  // a transposed constant or a gestation length in the wrong unit.
  EXPECT_NEAR(at_term, 1.15, 0.1);
  EXPECT_NEAR(at_term / kPregnancyEfficiency, 8.8, 1.0);
}

TEST(ReproductionEnergyTest, TwinsCostMoreThanASingle) {
  const AnimalClassParameters ewe = a_ewe();
  AnimalState state = a_ewe_state();
  state.days_pregnant = 140;

  state.young = 1.0;
  const double single = pregnancy_net_energy_mj(ewe, state);
  state.young = 2.0;
  const double twins = pregnancy_net_energy_mj(ewe, state);

  EXPECT_GT(twins, single);
  // Two lambs, but each born lighter, so the cost is well under double.
  EXPECT_LT(twins, single * 2.0);
  EXPECT_GT(twins, single * 1.5);
}

TEST(ReproductionEnergyTest, AnEmptyEweCostsNothingToCarryOrToMilk) {
  const AnimalClassParameters ewe = a_ewe();
  const AnimalState empty = a_ewe_state();  // not pregnant, not lactating, no young

  EXPECT_DOUBLE_EQ(pregnancy_net_energy_mj(ewe, empty), 0.0);
  EXPECT_DOUBLE_EQ(lactation_net_energy_mj(ewe, empty, a_paddock()), 0.0);

  // And a class that does not breed never pays either, whatever its state says.
  AnimalClassParameters wether = a_ewe();
  wether.gestation_length_days = 0.0;
  AnimalState pretending = a_ewe_state();
  pretending.days_pregnant = 140;
  pretending.days_lactating = 20;
  pretending.young = 2.0;
  EXPECT_DOUBLE_EQ(pregnancy_net_energy_mj(wether, pretending), 0.0);
  EXPECT_DOUBLE_EQ(lactation_net_energy_mj(wether, pretending, a_paddock()), 0.0);
}

// **Against a measured curve.** A single-bearing ewe peaks near 2 kg of milk a
// day in the first fortnight and is well down by weaning. That shape, not the
// arithmetic, is what this asserts.
TEST(ReproductionEnergyTest, MilkYieldPeaksEarlyAndFallsAway) {
  const AnimalClassParameters ewe = a_ewe();
  AnimalState state = a_ewe_state();
  state.young = 1.0;

  state.days_lactating = 14;
  const double peak = daily_milk_yield_kg(ewe, state, a_paddock());
  state.days_lactating = 60;
  const double middle = daily_milk_yield_kg(ewe, state, a_paddock());
  state.days_lactating = 100;
  const double weaning = daily_milk_yield_kg(ewe, state, a_paddock());

  EXPECT_NEAR(peak, 2.2, 0.3) << "a Romney ewe's published peak is about two litres";
  EXPECT_LT(middle, peak);
  EXPECT_LT(weaning, middle);
  EXPECT_GT(weaning, 0.0) << "she is still milking at weaning, just not much";
}

// The pasture term, which is what makes this worth having in a grazing model at
// all: a ewe on a bare paddock milks less.
TEST(ReproductionEnergyTest, AEweOnABarePaddockGivesLessMilk) {
  const AnimalClassParameters ewe = a_ewe();
  AnimalState state = a_ewe_state();
  state.young = 1.0;
  state.days_lactating = 20;

  GrazingConditions bare = a_paddock();
  bare.pasture_mass_t_dm_per_ha = 1.3;  // the equation's own threshold
  GrazingConditions good = a_paddock();
  good.pasture_mass_t_dm_per_ha = 2.6;

  EXPECT_LT(daily_milk_yield_kg(ewe, state, bare), daily_milk_yield_kg(ewe, state, good));
}

TEST(ReproductionEnergyTest, TwinsAreMilkedHarderThanASingle) {
  const AnimalClassParameters ewe = a_ewe();
  AnimalState state = a_ewe_state();
  state.days_lactating = 20;

  state.young = 1.0;
  const double single = daily_milk_yield_kg(ewe, state, a_paddock());
  state.young = 2.0;
  const double twins = daily_milk_yield_kg(ewe, state, a_paddock());

  EXPECT_GT(twins, single * 1.2) << "TMC Eq. 36 puts a twin-rearing ewe nearly 30% ahead";
}

// **The question the whole step exists to answer.** A ewe that is only ever fed
// maintenance eats about a third of a New Zealand stock unit. With pregnancy
// and lactation charged she should reach it.
TEST(ReproductionEnergyTest, APregnantOrLactatingEweEatsFarMoreThanAnEmptyOne) {
  const AnimalClassParameters ewe = a_ewe();
  const DietQuality diet = pasture();
  const GrazingConditions ground = a_paddock();

  const EnergyRequirement empty = daily_energy_requirement(ewe, a_ewe_state(), diet, ground);

  AnimalState late_pregnant = a_ewe_state();
  late_pregnant.young = 1.323;  // Beef + Lamb's national lambing percentage
  late_pregnant.days_pregnant = 145;
  const EnergyRequirement carrying = daily_energy_requirement(ewe, late_pregnant, diet, ground);

  AnimalState milking = a_ewe_state();
  milking.young = 1.323;
  milking.days_lactating = 20;
  const EnergyRequirement lactating = daily_energy_requirement(ewe, milking, diet, ground);

  // An empty ewe on maintenance: under a kilogram a day, which is what the
  // model used to feed every ewe on the farm every day of the year.
  EXPECT_LT(empty.intake_kg_dm, 1.0);

  // Late pregnancy roughly doubles it; peak lactation more than doubles it
  // again. Published peak-lactation ewe intakes are 2.5 to 3 kg DM a day.
  EXPECT_GT(carrying.intake_kg_dm, empty.intake_kg_dm * 1.7);
  EXPECT_GT(lactating.intake_kg_dm, 2.4);
  EXPECT_LT(lactating.intake_kg_dm, 3.2);

  EXPECT_GT(carrying.pregnancy_me_mj, 0.0);
  EXPECT_DOUBLE_EQ(carrying.lactation_me_mj, 0.0) << "she has not lambed yet";
  EXPECT_GT(lactating.lactation_me_mj, 0.0);
  EXPECT_DOUBLE_EQ(lactating.pregnancy_me_mj, 0.0) << "and she is no longer carrying";

  EXPECT_TRUE(carrying.converged);
  EXPECT_TRUE(lactating.converged);
}

// **Which side of TMC Eq. 1 a term falls on is worth a tenth.** Lactation is
// production and is charged the Eq. 54 maintenance share; pregnancy is not.
TEST(ReproductionEnergyTest, LactationIsProductionAndPregnancyIsNot) {
  const AnimalClassParameters ewe = a_ewe();
  const DietQuality diet = pasture();
  const GrazingConditions ground = a_paddock();

  AnimalState milking = a_ewe_state();
  milking.young = 1.0;
  milking.days_lactating = 20;
  const EnergyRequirement lactating = daily_energy_requirement(ewe, milking, diet, ground);

  const EnergyRequirement empty = daily_energy_requirement(ewe, a_ewe_state(), diet, ground);

  // Maintenance itself rose, because production rose and Eq. 54 charges a tenth
  // of production to maintenance.
  EXPECT_GT(lactating.maintenance_me_mj, empty.maintenance_me_mj);
  EXPECT_NEAR(lactating.maintenance_me_mj - empty.maintenance_me_mj,
              0.1 * lactating.lactation_me_mj, 0.35)
      << "the difference should be about a tenth of the milk, plus what extra chewing costs";

  AnimalState carrying_state = a_ewe_state();
  carrying_state.young = 1.0;
  carrying_state.days_pregnant = 145;
  const EnergyRequirement carrying = daily_energy_requirement(ewe, carrying_state, diet, ground);

  // **Pregnancy moves maintenance too, and by a route worth naming.** It is not
  // the Eq. 54 production share - pregnancy is outside production - but the
  // chewing of Eq. 18: a ewe eating for two chews more, and chewing is part of
  // net maintenance. So the rise is real and it is small, a few percent of what
  // the same megajoules of milk would have cost.
  const double from_pregnancy = carrying.maintenance_me_mj - empty.maintenance_me_mj;
  EXPECT_GT(from_pregnancy, 0.0) << "eating more costs more to chew";
  EXPECT_LT(from_pregnancy, 0.1 * carrying.pregnancy_me_mj * 0.25)
      << "but nothing like the tenth of itself that a production term is charged";

  // What pregnancy adds to the total is its own ME, plus only that chewing.
  EXPECT_NEAR(carrying.total_me_mj, empty.total_me_mj + carrying.pregnancy_me_mj + from_pregnancy,
              1e-9);
}

// **A ewe who eats in order to milk has spent that energy, not stored it.**
//
// `daily_energy_requirement` charges her for the milk and the lamb;
// `liveweight_response` deducted neither, so every megajoule she ate above
// maintenance came back as body fat however hard she was milking. Fed exactly
// what the requirement said she needed, she gained weight - which is the
// arithmetic that took a 66 kg ewe to 80.6 kg in one year on a farm asking her
// for no gain at all (verify.md, E72).
//
// The two functions are one ledger read forwards and backwards. Fed her
// requirement to the gram, a ewe should hold her weight.
//
// **Measured with the deduction removed, this test reports 0.184 kg a day at 14
// days lactating** - about 18 kg over a lactation, which is very nearly the
// whole of the 23 kg a ewe was putting on.
TEST(ReproductionEnergyTest, AEweFedExactlyHerRequirementHoldsHerWeight) {
  const AnimalClassParameters ewe = a_ewe();
  const DietQuality diet = pasture();
  const GrazingConditions ground;

  for (const int day : {0, 14, 40, 80}) {
    AnimalState state = a_ewe_state();
    state.days_lactating = day;
    state.young = day > 0 ? 1.0 : 0.0;

    // What the manual says she needs to hold weight, then exactly that much.
    state.liveweight_change_kg_per_day = 0.0;
    const EnergyRequirement need = daily_energy_requirement(ewe, state, diet, ground);
    const LiveweightResponse got = liveweight_response(ewe, state, diet, ground, need.intake_kg_dm);

    EXPECT_NEAR(got.liveweight_change_kg, 0.0, 0.02)
        << "at " << day << " days lactating she was fed " << need.intake_kg_dm
        << " kg DM to hold weight and changed by " << got.liveweight_change_kg << " kg";
  }
}

// **And a ewe milking on less than she needs takes the difference out of
// herself**, which is what a ewe in lactation on a short paddock does. Before
// the ledger balanced she could not lose weight while lactating at all: the
// milk energy was counted as a surplus.
TEST(ReproductionEnergyTest, AEweMilkingOnShortRationsLosesCondition) {
  const AnimalClassParameters ewe = a_ewe();
  const DietQuality diet = pasture();
  const GrazingConditions ground;

  AnimalState state = a_ewe_state();
  state.days_lactating = 20;
  state.young = 1.0;

  state.liveweight_change_kg_per_day = 0.0;
  const EnergyRequirement need = daily_energy_requirement(ewe, state, diet, ground);
  ASSERT_GT(need.lactation_me_mj, 0.0) << "she should be milking";

  // Three quarters of what she needs.
  const LiveweightResponse got =
      liveweight_response(ewe, state, diet, ground, need.intake_kg_dm * 0.75);

  EXPECT_TRUE(got.losing) << "underfed in lactation, she should be losing";
  EXPECT_LT(got.liveweight_change_kg, 0.0);
  EXPECT_GT(got.lactation_me_mj, 0.0) << "and the milk should be on her bill";
}

}  // namespace
}  // namespace paddock::core
