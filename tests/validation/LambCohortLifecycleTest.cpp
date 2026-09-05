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
#include <utility>
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

/// A flock of `classes` breeding cohorts and a lamb template distinct from
/// them, so that a lamb built from the wrong one would be visible.
///
/// **The class count is the capacity knob.** `cohorts_` is a vector, and how
/// many cohorts a flock opens with decides whether the weaning append
/// reallocates. One class is the tight case that used to break.
Flock a_flock_of(int classes, int ewes) {
  Flock flock;

  Mob ewe_mob;
  ewe_mob.name = "ewes";
  ewe_mob.animal.standard_reference_weight_kg = 66.0;
  ewe_mob.animal.normal_weight_rate = 0.0157;
  ewe_mob.animal.normal_weight_exponent = 0.27;
  ewe_mob.animal.normal_weight_blend = 0.4;
  ewe_mob.state.liveweight_kg = 55.0;
  ewe_mob.state.age_days = 1500.0;

  for (int i = 0; i < classes; ++i) {
    AgeCohort cohort;
    cohort.birth_year = 2019 - i;
    cohort.age_years = 4;
    cohort.mob = ewe_mob;
    cohort.mob.head = ewes / classes;
    cohort.mob.name = "ewes " + std::to_string(cohort.birth_year);
    flock.add(std::move(cohort));
  }

  Mob lamb_mob = ewe_mob;
  lamb_mob.name = "lamb template";
  lamb_mob.head = 0;
  lamb_mob.state.liveweight_kg = 0.0;
  lamb_mob.state.age_days = 0.0;
  flock.set_lamb_template(lamb_mob);
  return flock;
}

/// The single-class flock the earlier tests were written against.
Flock a_flock_with_ewes_and_a_lamb_template(int ewes) {
  return a_flock_of(1, ewes);
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

/// **The weaning split is correct at every flock size** (verify.md, E118).
///
/// This was the characterisation of a defect and is now the contract. The
/// weaning block used to `push_back` into `cohorts_` from inside a range-for
/// over `cohorts_`; when the vector reallocated, the loop's reference dangled
/// and `cohort.mob.head = kept` and `cohort.is_finishing = false` were lost, so
/// the lamb cohort kept its whole head *and* stayed finishing stock beside the
/// new one. Whether that happened depended on `std::vector` capacity - which is
/// to say on allocator history rather than on anything this model describes,
/// and is why it is a determinism defect before it is a flock-accounting one.
///
/// **A two-cohort flock is the capacity-tight case**: the vector holds exactly
/// its two cohorts, so the append during the loop had to reallocate. It is
/// swept here alongside larger flocks, because a fix that only worked where the
/// bug never fired would prove nothing.
TEST(LambCohortLifecycleTest, TheWeaningSplitIsCorrectAtEveryFlockSize) {
  for (const int classes : {1, 2, 3, 4, 5, 8}) {
    Flock flock = a_flock_of(classes, 400);

    int born = 0;
    bool weaned = false;
    for (Date day{2023, 7, 1}; day.days_since_epoch() <= Date{2023, 12, 5}.days_since_epoch();
         day = Date::from_days_since_epoch(day.days_since_epoch() + 1)) {
      const FlockDay record = flock.step(day, a_calendar(), the_rates());
      born += record.born;
      if (record.kept_to_finish <= 0) {
        continue;
      }
      weaned = true;

      // **The finishing draft appears exactly once.**
      int finishing_cohorts = 0;
      int finishing_head = 0;
      int lamb_head_left_breeding = 0;
      for (const AgeCohort& cohort : flock.cohorts()) {
        if (cohort.mob.head <= 0) {
          continue;
        }
        if (cohort.is_finishing) {
          ++finishing_cohorts;
          finishing_head += cohort.mob.head;
        } else if (cohort.birth_year == day.year) {
          // **The replacements stayed put**, in the cohort they were born into,
          // and are no longer finishing stock.
          lamb_head_left_breeding += cohort.mob.head;
        }
      }

      EXPECT_EQ(finishing_cohorts, 1) << "at " << classes << " opening classes";
      EXPECT_EQ(finishing_head, record.kept_to_finish) << "at " << classes << " opening classes";
      EXPECT_EQ(lamb_head_left_breeding, record.kept_as_replacements)
          << "at " << classes << " opening classes: the replacements are next year's flock";

      // **Head is conserved across the split**, but for what was explicitly
      // sold: every lamb born is now a replacement, a finisher or a store sale.
      EXPECT_EQ(record.kept_as_replacements + record.kept_to_finish + record.sold_store, born)
          << "at " << classes << " opening classes: the split neither invents nor loses a lamb";
      break;
    }
    EXPECT_TRUE(weaned) << "at " << classes << " opening classes the flock never reached weaning";
  }
}

/// **Age and liveweight survive the split, at the capacity-tight size too.**
///
/// Compared between the two halves rather than across the day: `step` ages the
/// flock before it weans it, so a before-and-after reading would differ by the
/// day's ageing and prove nothing. What must hold is that the finishers and the
/// replacements come out of the split describing the same animals.
TEST(LambCohortLifecycleTest, TheSplitCarriesAgeAndLiveweightAtTheCapacityTightSize) {
  Flock flock = a_flock_of(1, 400);

  for (Date day{2023, 7, 1}; day.days_since_epoch() <= Date{2023, 12, 5}.days_since_epoch();
       day = Date::from_days_since_epoch(day.days_since_epoch() + 1)) {
    const FlockDay record = flock.step(day, a_calendar(), the_rates());
    if (record.kept_to_finish <= 0) {
      continue;
    }

    const AgeCohort* finishers = nullptr;
    const AgeCohort* replacements = nullptr;
    for (const AgeCohort& cohort : flock.cohorts()) {
      if (cohort.birth_year != day.year || cohort.mob.head <= 0) {
        continue;
      }
      if (cohort.is_finishing) {
        finishers = &cohort;
      } else {
        replacements = &cohort;
      }
    }

    ASSERT_NE(finishers, nullptr) << "a finishing cohort has to come out of the split";
    ASSERT_NE(replacements, nullptr) << "and so do the replacements";
    EXPECT_DOUBLE_EQ(finishers->mob.state.age_days, replacements->mob.state.age_days)
        << "the split makes two cohorts of one crop, not two crops";
    EXPECT_DOUBLE_EQ(finishers->mob.state.liveweight_kg, replacements->mob.state.liveweight_kg);
    EXPECT_EQ(finishers->age_years, replacements->age_years);
    EXPECT_GT(finishers->mob.state.age_days, 0.0) << "and they are the season's lambs, aged";
    return;
  }
  FAIL() << "the flock never reached weaning";
}

/// **The same flock gives the same answer whatever its capacity history.**
///
/// Cohort counts differ, so the totals cannot be compared; what must not differ
/// is the shape of the split. Before the fix, one of these read two finishing
/// cohorts and the others read one.
TEST(LambCohortLifecycleTest, TheSplitShapeDoesNotDependOnCapacityHistory) {
  const auto shape_of = [](int classes) {
    Flock flock = a_flock_of(classes, 400);
    for (Date day{2023, 7, 1}; day.days_since_epoch() <= Date{2023, 12, 5}.days_since_epoch();
         day = Date::from_days_since_epoch(day.days_since_epoch() + 1)) {
      const FlockDay record = flock.step(day, a_calendar(), the_rates());
      if (record.kept_to_finish > 0) {
        int finishing = 0;
        for (const AgeCohort& cohort : flock.cohorts()) {
          if (cohort.is_finishing && cohort.mob.head > 0) {
            ++finishing;
          }
        }
        return finishing;
      }
    }
    return -1;
  };

  const int reference = shape_of(1);
  EXPECT_EQ(reference, 1);
  for (const int classes : {2, 3, 4, 5, 8, 13}) {
    EXPECT_EQ(shape_of(classes), reference) << "at " << classes << " opening classes";
  }

  // And twice through the same size is the same answer, which is the ordinary
  // determinism claim standing where it could not before.
  EXPECT_EQ(shape_of(1), shape_of(1));
  EXPECT_EQ(shape_of(5), shape_of(5));
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
