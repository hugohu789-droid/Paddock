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

// **The year's events, each on its own date and no other.**
TEST(FlockTest, TheCalendarIsValidAndEventsFallOnTheirDates) {
  EXPECT_EQ(FlockCalendar{}.invalid_reason(), "");

  FlockCalendar impossible;
  impossible.lambing_month = 2;
  impossible.lambing_day = 30;
  EXPECT_NE(impossible.invalid_reason(), "");

  Flock flock = a_flock();
  const FlockCalendar calendar;
  const FlockRates rates;

  // An ordinary day does nothing at all.
  const FlockDay quiet = flock.step(Date{2024, 5, 15}, calendar, rates);
  EXPECT_FALSE(quiet.anything_happened());
  EXPECT_EQ(flock.head(), 400);
}

// **Lambing.** 400 ewes at Beef + Lamb's 132.3% is 529 lambs, and the deaths
// that Ridler et al. put at two-thirds of the year's losses happen the same day.
TEST(FlockTest, LambingAddsACohortAndCostsSomeEwes) {
  Flock flock = a_flock();
  const FlockCalendar calendar;
  const FlockRates rates;

  const FlockDay lambing = flock.step(Date{2024, 8, 20}, calendar, rates);

  EXPECT_EQ(lambing.born, 529) << "400 ewes at 132.3%";
  EXPECT_EQ(lambing.died, 28) << "400 ewes, 10.5% lost overall, two-thirds of it here";

  // The lambs are their own cohort, at age zero, and are not counted as
  // breeding stock.
  EXPECT_EQ(flock.head(), 400 - 28 + 529);
  EXPECT_EQ(flock.breeding_head(), 400 - 28);
  EXPECT_EQ(flock.cohorts().back().class_name(), "lambs");
  EXPECT_DOUBLE_EQ(flock.cohorts().back().mob.state.age_days, 0.0);
}

// Weaning is the main culling event, and it takes the old ewes.
TEST(FlockTest, WeaningCullsTheOlderEwes) {
  Flock flock = a_flock();
  const FlockCalendar calendar;
  const FlockRates rates;

  const int before = flock.breeding_head();
  const FlockDay weaning = flock.step(Date{2024, 12, 1}, calendar, rates);

  EXPECT_EQ(weaning.culled, 66) << "16.5% of 400";
  EXPECT_EQ(flock.breeding_head(), before - 66);

  // Taken off the top: the six-year-olds went first.
  ASSERT_FALSE(flock.cohorts().empty());
  EXPECT_LT(flock.cohorts().front().mob.head, 80);
}

// **The question the whole age structure exists to answer.** Run ten years of
// the real calendar and see whether a flock feeds itself: lambs become
// hoggets, hoggets become two-tooths, the old draft leaves, and the head count
// neither collapses nor runs away.
TEST(FlockTest, TenYearsOfTheCalendarKeepAFlockGoing) {
  Flock flock = a_flock();
  const FlockCalendar calendar;
  const FlockRates rates;

  int born = 0;
  int died = 0;
  int culled = 0;

  Date day{2024, 7, 1};
  for (int i = 0; i < 365 * 10; ++i) {
    const FlockDay event = flock.step(day, calendar, rates);
    born += event.born;
    died += event.died;
    culled += event.culled;
    day = Date::from_days_since_epoch(day.days_since_epoch() + 1);
  }

  EXPECT_GT(born, 0);
  EXPECT_GT(died, 0);
  EXPECT_GT(culled, 0);

  // **A flock that sells every lamb it breeds cannot replace itself**, and this
  // one keeps them all - so it grows. That is the finding: keeping every lamb
  // is not a management policy, and what this says is that the flock now has
  // somewhere for lambs to go and nothing yet to send them there. Selling them
  // is the drafting rule, which needs a finishing class - the next step.
  EXPECT_GT(flock.head(), 400)
      << "with every lamb kept and nothing drafted, ten years should leave more sheep";

  // And no cohort is older than the cull age, whatever else happened.
  for (const AgeCohort& cohort : flock.cohorts()) {
    EXPECT_LE(cohort.age_years, rates.cull_age_years);
  }
}

}  // namespace
}  // namespace paddock::core
