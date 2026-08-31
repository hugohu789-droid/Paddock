// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// The flock's age structure.
///
/// The rates here are Ridler et al. (2025), New Zealand Veterinary Journal
/// 73:112-123, who measured 34 real New Zealand flocks: a between-flock mean
/// replacement of 29.2%, 10.5% of ewes lost between breeding and mid-lactation,
/// and 16.5% culled at weaning. Round numbers would have tested the arithmetic
/// and nothing about whether this is how a flock turns over.

#include <gtest/gtest.h>

#include <paddock/core/Flock.hpp>

namespace paddock::core {
namespace {

Mob ewes(int head) {
  Mob mob;
  mob.name = "ewes";
  mob.head = head;
  mob.animal.class_id = "sheep_ewe";
  mob.animal.species_factor = 1.0;
  mob.animal.sex_factor = 1.0;
  mob.animal.standard_reference_weight_kg = 65.0;
  mob.animal.grazing_coefficient = 0.0025;
  mob.animal.gain_energy_ceiling_mj_per_kg = 20.3;
  mob.state.liveweight_kg = 60.0;
  mob.state.age_days = 730.0;
  return mob;
}

AgeCohort cohort(int birth_year, int age_years, int head) {
  AgeCohort born;
  born.birth_year = birth_year;
  born.age_years = age_years;
  born.mob = ewes(head);
  return born;
}

/// A flock the way a farmer would describe one: five age classes, oldest first.
Flock a_flock() {
  Flock flock;
  for (int age = 2; age <= 6; ++age) {
    flock.add(cohort(2024 - age, age, 80));
  }
  return flock;
}

TEST(FlockTest, TheShippedRatesAreUsable) {
  EXPECT_EQ(FlockRates{}.invalid_reason(), "");

  FlockRates too_young;
  too_young.cull_age_years = 1;
  EXPECT_NE(too_young.invalid_reason(), "") << "a flock culled before it breeds is not a flock";
}

// A farmer names classes by age, and so should a report.
TEST(FlockTest, ACohortKnowsWhatAFarmerWouldCallIt) {
  EXPECT_EQ(cohort(2024, 0, 10).class_name(), "lambs");
  EXPECT_EQ(cohort(2023, 1, 10).class_name(), "hoggets");
  EXPECT_EQ(cohort(2022, 2, 10).class_name(), "two-tooths");
  EXPECT_EQ(cohort(2020, 4, 10).class_name(), "mixed-age");
}

TEST(FlockTest, TheFlockCountsItsHeadAndItsBreedingHead) {
  Flock flock = a_flock();
  EXPECT_EQ(flock.head(), 400) << "five classes of eighty";
  EXPECT_EQ(flock.breeding_head(), 400) << "all of them are two or older";

  flock.add(cohort(2024, 0, 120));
  EXPECT_EQ(flock.head(), 520);
  EXPECT_EQ(flock.breeding_head(), 400) << "lambs are not put to the ram here";
}

// **Ageing and culling are one step**, so a flock cannot be aged without the
// oldest draft leaving.
TEST(FlockTest, TurningTheYearAgesEveryCohortAndSellsThePastCullAge) {
  Flock flock = a_flock();
  const FlockRates rates;  // cull at six

  const int culled = flock.age_one_year(rates);
  EXPECT_EQ(culled, 80) << "the six-year-olds turned seven and went";
  EXPECT_EQ(flock.head(), 320);

  // Everything left is a year older, and the ages moved with them.
  for (const AgeCohort& c : flock.cohorts()) {
    EXPECT_LE(c.age_years, rates.cull_age_years);
    EXPECT_GE(c.mob.state.age_days, 730.0 + 365.0)
        << "the representative animal has to age with its cohort, or the energy "
           "model keeps feeding a two-tooth forever";
  }
}

// **Ten years without replacements empties the flock**, which is the finding
// that says ageing is actually happening rather than being asserted.
TEST(FlockTest, AFlockWithNoReplacementsAgesOut) {
  Flock flock = a_flock();
  const FlockRates rates;

  int sold = 0;
  for (int year = 0; year < 10; ++year) {
    sold += flock.age_one_year(rates);
  }

  EXPECT_EQ(flock.head(), 0) << "nothing was born, so nothing is left";
  EXPECT_EQ(sold, 400) << "and every one of them was sold rather than lost";
  EXPECT_TRUE(flock.cohorts().empty());
}

// Selling takes the oldest first, which is what a farmer does.
TEST(FlockTest, SellingTakesTheOldestFirst) {
  Flock flock = a_flock();

  const int sold = flock.sell_oldest(100);
  EXPECT_EQ(sold, 100);
  EXPECT_EQ(flock.head(), 300);

  // The six-year-olds are gone entirely and the five-year-olds are down to 60.
  ASSERT_FALSE(flock.cohorts().empty());
  EXPECT_EQ(flock.cohorts().front().age_years, 5);
  EXPECT_EQ(flock.cohorts().front().mob.head, 60);
}

TEST(FlockTest, SellingMoreThanTheFlockHoldsSellsWhatThereIs) {
  Flock flock = a_flock();
  EXPECT_EQ(flock.sell_oldest(10'000), 400);
  EXPECT_EQ(flock.head(), 0);
  EXPECT_EQ(flock.sell_oldest(10), 0) << "an empty flock has nothing to sell";
}

}  // namespace
}  // namespace paddock::core
