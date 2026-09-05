// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// **Diagnosis of open item 21: is the 4.8 kg lamb a starving animal or a broken
// record?** (verify.md, E116 and E117.)
//
// E116's validity check flagged a finishing lamb cohort at a relative condition
// of 0.228 - about 4.8 kg against a frame of some 45 kg - in the no-market
// ladder at 800 breeding ewes. Two readings were open: the model's energy
// balance really does take a lamb there when nothing is fed and nothing dies,
// or the cohort's age and liveweight had come apart through birth, weaning,
// splitting or sale.
//
// These tests decide it. They change no behaviour; they fix the lifecycle
// invariants in place so that a future change cannot quietly break what this
// investigation found to be sound.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <paddock/config/EconomicsConfig.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/Flock.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::core {
namespace {

FlockCalendar a_calendar() {
  return FlockCalendar{};
}

FlockRates the_rates() {
  return FlockRates{};
}

/// A ewe cohort old enough to breed, and a lamb template distinct from it, so
/// that a lamb built from the wrong one would be visible.
Flock a_flock_with_ewes_and_a_lamb_template(int ewes) {
  Flock flock;

  Mob ewe_mob;
  ewe_mob.name = "ewes";
  ewe_mob.head = ewes;
  ewe_mob.animal.standard_reference_weight_kg = 66.0;
  ewe_mob.animal.normal_weight_rate = 0.0157;
  ewe_mob.animal.normal_weight_exponent = 0.27;
  ewe_mob.animal.normal_weight_blend = 0.4;
  ewe_mob.state.liveweight_kg = 55.0;
  ewe_mob.state.age_days = 1500.0;

  AgeCohort ewes_cohort;
  ewes_cohort.birth_year = 2019;
  ewes_cohort.age_years = 4;
  ewes_cohort.mob = ewe_mob;
  flock.add(ewes_cohort);

  Mob lamb_mob = ewe_mob;
  lamb_mob.name = "lamb template";
  lamb_mob.head = 0;
  lamb_mob.state.liveweight_kg = 0.0;
  lamb_mob.state.age_days = 0.0;
  flock.set_lamb_template(lamb_mob);
  return flock;
}

/// The finishing cohort the run would put on the paddock: the first one marked
/// finishing, which is what both `ScenarioRun`'s read-out and its write-back
/// pick.
const AgeCohort* the_finishing_cohort(const Flock& flock) {
  for (const AgeCohort& cohort : flock.cohorts()) {
    if (cohort.is_finishing) {
      return &cohort;
    }
  }
  return nullptr;
}

/// **Invariant: age never decreases, for any cohort, on any day.**
TEST(LambCohortLifecycleTest, AgeNeverDecreases) {
  Flock flock = a_flock_with_ewes_and_a_lamb_template(400);
  std::vector<double> previous;

  for (Date day{2023, 7, 1}; day.days_since_epoch() <= Date{2024, 6, 30}.days_since_epoch();
       day = Date::from_days_since_epoch(day.days_since_epoch() + 1)) {
    flock.step(day, a_calendar(), the_rates());

    std::vector<double> ages;
    for (const AgeCohort& cohort : flock.cohorts()) {
      ages.push_back(cohort.mob.state.age_days);
      EXPECT_GE(cohort.mob.state.age_days, 0.0) << "on " << day.to_iso_string();
    }
    // A cohort that survives keeps its place in the oldest-first order, so a
    // like-for-like comparison holds while the count does not change.
    if (ages.size() == previous.size()) {
      for (std::size_t i = 0; i < ages.size(); ++i) {
        EXPECT_GE(ages[i], previous[i]) << "age went backwards on " << day.to_iso_string();
      }
    }
    previous = ages;
  }
}

/// **Invariant: weaning transfers the cohort's liveweight, it does not reset
/// it.** This is hypothesis 1 and hypothesis 5, tested directly: a lamb is
/// grown to a known weight from outside, the way the farm grows it, and the
/// weaning split must carry that weight across.
TEST(LambCohortLifecycleTest, WeaningCarriesTheLiveweightAcrossTheSplit) {
  Flock flock = a_flock_with_ewes_and_a_lamb_template(400);

  double grown_to = 0.0;
  bool saw_lambs = false;
  double weight_before_weaning = 0.0;
  double weight_after_weaning = -1.0;

  for (Date day{2023, 7, 1}; day.days_since_epoch() <= Date{2024, 6, 30}.days_since_epoch();
       day = Date::from_days_since_epoch(day.days_since_epoch() + 1)) {
    // What the farm did to the lambs yesterday, written back exactly as
    // `keep_the_books` writes it.
    if (saw_lambs) {
      for (AgeCohort& cohort : flock.cohorts_for_update()) {
        if (cohort.is_finishing) {
          cohort.mob.state.liveweight_kg = grown_to;
          break;
        }
      }
    }

    const bool weaning_today =
        day.month == a_calendar().weaning_month && day.day == a_calendar().weaning_day;
    if (weaning_today) {
      const AgeCohort* before = the_finishing_cohort(flock);
      weight_before_weaning = before != nullptr ? before->mob.state.liveweight_kg : 0.0;
    }

    const FlockDay record = flock.step(day, a_calendar(), the_rates());
    if (record.born > 0) {
      saw_lambs = true;
      const AgeCohort* born = the_finishing_cohort(flock);
      ASSERT_NE(born, nullptr);
      EXPECT_DOUBLE_EQ(born->mob.state.age_days, 0.0) << "a lamb is born at age zero";
      EXPECT_GT(born->mob.state.liveweight_kg, 0.0) << "and at a birth weight, not at nothing";
      grown_to = born->mob.state.liveweight_kg;
    }
    if (weaning_today) {
      const AgeCohort* after = the_finishing_cohort(flock);
      if (after != nullptr) {
        weight_after_weaning = after->mob.state.liveweight_kg;
      }
    }

    // The farm adds a little every day, which is what makes a reset visible.
    if (saw_lambs) {
      grown_to += 0.25;
    }
  }

  ASSERT_GT(weight_before_weaning, 0.0) << "the flock has to reach weaning for this to mean "
                                           "anything";
  ASSERT_GE(weight_after_weaning, 0.0) << "and a finishing cohort has to survive it";
  EXPECT_DOUBLE_EQ(weight_after_weaning, weight_before_weaning)
      << "weaning splits a cohort and must carry its liveweight across, not restart it";
}

/// **The defect this investigation found, characterised rather than fixed**
/// (verify.md, E117, open item 22).
///
/// `Flock::step`'s weaning block does `cohorts_.push_back(...)` from inside a
/// range-for over `cohorts_`. When that reallocates, the loop variable dangles
/// and the two writes after it - `cohort.mob.head = kept` and
/// `cohort.is_finishing = false` - are lost. The lamb cohort then keeps its
/// whole head *and* stays finishing stock beside the new finishing cohort, so
/// the flock carries the finished draft twice.
///
/// **Whether it fires depends on `std::vector` capacity**, which is why it has
/// never been seen: the shipped flock opens with five ewe cohorts and has room
/// to spare, and the test below shows that. A two-cohort flock does not.
///
/// This asserts the **wrong** behaviour on purpose, so that the repository
/// records what it currently does. Fixing it will fail this test, which is the
/// point: whoever fixes it should come here and say so.
TEST(LambCohortLifecycleTest, ASmallFlockLosesTheWeaningSplitWrites) {
  Flock flock = a_flock_with_ewes_and_a_lamb_template(400);

  int finishing_cohorts_after_weaning = 0;
  for (Date day{2023, 7, 1}; day.days_since_epoch() <= Date{2023, 12, 5}.days_since_epoch();
       day = Date::from_days_since_epoch(day.days_since_epoch() + 1)) {
    const FlockDay record = flock.step(day, a_calendar(), the_rates());
    if (record.kept_to_finish > 0) {
      for (const AgeCohort& cohort : flock.cohorts()) {
        if (cohort.is_finishing && cohort.mob.head > 0) {
          ++finishing_cohorts_after_weaning;
        }
      }
      break;
    }
  }

  EXPECT_EQ(finishing_cohorts_after_weaning, 2)
      << "if this now reads 1 the defect has been fixed - delete this test and turn "
         "TheShippedFlockSplitsItsLambsCorrectlyAtWeaning into the general invariant";
}

/// **Invariant: a cohort's age and liveweight come from the same animal.** A
/// newborn carries the lamb template's parameters and its own zero age, never a
/// ewe's age with a lamb's weight.
TEST(LambCohortLifecycleTest, ANewbornTakesItsAgeAndItsWeightFromTheSameAnimal) {
  Flock flock = a_flock_with_ewes_and_a_lamb_template(400);

  for (Date day{2023, 7, 1}; day.days_since_epoch() <= Date{2023, 9, 30}.days_since_epoch();
       day = Date::from_days_since_epoch(day.days_since_epoch() + 1)) {
    const FlockDay record = flock.step(day, a_calendar(), the_rates());
    if (record.born > 0) {
      const AgeCohort* born = the_finishing_cohort(flock);
      ASSERT_NE(born, nullptr);
      EXPECT_DOUBLE_EQ(born->mob.state.age_days, 0.0);
      EXPECT_LT(born->mob.state.liveweight_kg, 10.0) << "a lamb, not a ewe";
      EXPECT_DOUBLE_EQ(born->mob.state.liveweight_change_kg_per_day, 0.0)
          << "and it does not inherit the dam's gain";
      EXPECT_EQ(born->birth_year, day.year);
      return;
    }
  }
  FAIL() << "no lambs were born, so nothing was tested";
}

}  // namespace
}  // namespace paddock::core

namespace paddock::config {
namespace {

/// **The shipped flock splits correctly, which is why no scenario is affected.**
///
/// `business_from` opens the flock with `cull_age_years - 1` ewe cohorts - five
/// for the shipped rates - and that is enough spare capacity that the weaning
/// `push_back` does not reallocate. This is the invariant that matters to every
/// bundle in `data/scenarios/`, and it holds.
TEST(LambCohortLifecycleTest, TheShippedFlockSplitsItsLambsCorrectlyAtWeaning) {
  for (const int head : {417, 800, 1300}) {
    ScenarioBundle bundle = tests::load_on_flat_ground(std::string(PADDOCK_DATA_DIR) +
                                                       "/scenarios/demo-irrigation-off");
    bundle.mobs.front().head = head;
    const FarmEconomics economics = load_economics(bundle.economics_path);
    FarmBusiness business = business_from(bundle, economics);
    ASSERT_GE(business.flock.cohorts().size(), 3U)
        << "the shipped flock has to open with an age structure for this to hold";

    bool weaned = false;
    for (core::Date day = bundle.range.first;
         day.days_since_epoch() <= bundle.range.last.days_since_epoch();
         day = core::Date::from_days_since_epoch(day.days_since_epoch() + 1)) {
      const core::FlockDay record = business.flock.step(day, business.calendar, business.rates);
      if (record.kept_to_finish <= 0) {
        continue;
      }
      weaned = true;
      int finishing = 0;
      for (const core::AgeCohort& cohort : business.flock.cohorts()) {
        if (cohort.is_finishing && cohort.mob.head > 0) {
          ++finishing;
        }
        EXPECT_GE(cohort.mob.head, 0) << "at " << head << " head";
      }
      EXPECT_EQ(finishing, 1) << "at " << head
                              << " head: two finishing cohorts would put one cohort's state on "
                                 "the paddock and the farm's answer into another";
      break;
    }
    EXPECT_TRUE(weaned) << "at " << head << " head the run never reached weaning";
  }
}

/// **Characterisation of the run that raised open item 21.**
///
/// The lamb weans at the same weight as every other rung of the ladder and then
/// loses it over the summer. That is what distinguishes a starvation trajectory
/// from a reset, and it is the measurement the diagnosis rests on: a reset would
/// show a lamb that never reached weaning weight, or one that dropped to birth
/// weight in a day.
TEST(LambCohortLifecycleTest, TheEightHundredHeadLambWeansNormallyAndThenStarves) {
  ScenarioBundle bundle =
      tests::load_on_flat_ground(std::string(PADDOCK_DATA_DIR) + "/scenarios/demo-irrigation-off");
  bundle.mobs.front().head = 800;

  core::ManagementPolicy policy = *bundle.management;
  policy.supplement_market.available_kg_dm = 0.0;

  const FarmEconomics economics = load_economics(bundle.economics_path);
  const RunSummary run = run_managed_scenario(bundle, policy, bundle.diet, "800 no market",
                                              business_from(bundle, economics));

  // It weaned at a normal weight. Nothing was reset on the way there.
  EXPECT_GT(run.lamb_weaning_weight_kg, 25.0)
      << "the lamb reached a normal weaning weight, so its growth was not being discarded";

  // And then it fell away, well past the bottom of the published scale.
  ASSERT_FALSE(run.animal_domain.inside());
  EXPECT_LT(run.animal_domain.cohort_liveweight_kg, 10.0);
  EXPECT_LT(run.animal_domain.lowest_relative_condition, 0.3);

  // The fall is the distance between those two, and it happened after weaning:
  // the crossing is months later, not on the weaning day.
  ASSERT_TRUE(run.animal_domain.first_crossing.has_value());
  EXPECT_GT(run.animal_domain.first_crossing->month, 1)
      << "a crossing on the weaning day itself would point at the transition rather than the "
         "energy balance";
}

}  // namespace
}  // namespace paddock::config
