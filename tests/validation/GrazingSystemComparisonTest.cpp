// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Set stocking against rotation, on one farm.
//
// This is the first thing the closed loop is actually for, and it is the reason
// the loop had to close: until a mob's weight responded to what it ate and a
// farmer moved it, the two systems produced identical numbers.
//
// The comparison changes exactly one thing. Same farm, same weather, same stock,
// same stocking rate, same diet - only the calendar differs. Anything else and
// the difference could not be attributed to the management, which is the whole
// claim.
//
// Smith and Dawson (1976) measured such a comparison on a Tauranga property at
// equal stocking rate and reported seven kilograms of hogget liveweight in
// favour of rotation by November. Their evidence is weaker than this project's
// other targets - one property, not a replicated trial, in a paper arguing for
// the system it recommends - so it fixes a plausible magnitude rather than a
// number to hit. See docs/validation/verify.md.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>

#include <paddock/config/ScenarioRun.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::config {
namespace {

std::string bundle_path() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-grazed";
}

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

struct Comparison {
  RunSummary set_stocked;
  RunSummary rotational;
};

/// The bundle's own stocking rate, or another one.
///
/// Stocking rate is the parameter that decides whether this comparison has
/// anything to compare, so it is a parameter of the comparison rather than
/// something buried in the bundle.
Comparison compare(int head) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.mobs.front().head = head;

  // Smith and Dawson's rule for the rotational arm: graze no more than three
  // days, spell 35. Set stocking takes no parameters, being the absence of a
  // rotation rather than a different one.
  return {
      run_scenario(bundle, whole_run_calendar(bundle.range, core::GrazingSystem::SetStocking, 0, 0),
                   pasture_diet(), "set stocking"),
      run_scenario(bundle, whole_run_calendar(bundle.range, core::GrazingSystem::Rotational, 3, 35),
                   pasture_diet(), "rotational")};
}

/// Enough stock that the farm cannot feed them without help from the
/// management. Found by running the model, not chosen: at the bundle's own 500
/// head neither system goes short on any day of the year.
constexpr int kLimitingHead = 1400;

// **A grazing system only matters when feed is short.** At the bundle's own
// stocking rate the farm carries the mob easily under either management, both
// arms report no day short all year, and the two answers differ by three
// micrograms of liveweight.
//
// That is the model being right rather than being useless, and it is why the
// rest of these tests run at a heavier stocking rate. Smith and Dawson's
// comparison was on a working farm at a stocking rate where feed was limiting;
// a comparison run where it is not would show nothing and would pass anyway.
TEST(GrazingSystemComparisonTest, WithFeedToSpareTheSystemsAreIndistinguishable) {
  // **250, where this used to say 500** (verify.md, E80). The comment above was
  // written when the bundle ran a synthetic generator drawing 700-930 mm and a
  // sward whose radiation use efficiency was a placeholder 1.5. On real Selwyn
  // weather and the sourced sward the same farm carries far fewer: at 500 head
  // both arms now go short, and 250 is where "feed to spare" begins again.
  // Found by running the model, as the original was.
  // 200 with E83, where this said 250 and originally 500. Each drop is the farm
  // getting more honest rather than the test getting easier: a sourced turnover
  // response carries a little less leaf than the fitted leaf lifespan it
  // replaced, so "feed to spare" begins at a lighter rate again.
  const Comparison result = compare(200);

  EXPECT_EQ(result.set_stocked.days_short, 0);
  EXPECT_EQ(result.rotational.days_short, 0);
  EXPECT_NEAR(result.set_stocked.liveweight_change_kg(), result.rotational.liveweight_change_kg(),
              0.01)
      << "with feed to spare a mob eats its fill either way";
}

// The two systems have to produce different farms. Before the loop closed they
// did not, and a test that only checked the numbers were plausible would have
// passed then too.
TEST(GrazingSystemComparisonTest, TheTwoSystemsProduceDifferentFarms) {
  const Comparison result = compare(kLimitingHead);

  ASSERT_EQ(result.set_stocked.cover_kg_dm_per_ha.size(),
            result.rotational.cover_kg_dm_per_ha.size());

  EXPECT_EQ(result.set_stocked.moves, 0) << "set stocking moves nobody";
  EXPECT_GT(result.rotational.moves, 50) << "a year of three-day grazings is a lot of shifts";

  EXPECT_NE(result.set_stocked.closing_liveweight_kg(), result.rotational.closing_liveweight_kg())
      << "the two managements ended with the same animal, so nothing is being compared";
}

// Rotation rests paddocks and set stocking does not, which is the mechanism
// everything else follows from. Under set stocking every paddock is grazed every
// day, so none of them ever rests.
TEST(GrazingSystemComparisonTest, OnlyRotationRestsAnything) {
  const Comparison result = compare(kLimitingHead);

  // The set stocked mob has the run of the farm, so it never "moves" and its
  // recorded paddock never changes.
  const int first = result.set_stocked.paddock_of_first_mob.front();
  for (const int paddock : result.set_stocked.paddock_of_first_mob) {
    ASSERT_EQ(paddock, first) << "a set stocked mob should not be shifted between paddocks";
  }

  // The rotating mob works its way round.
  const std::set<int> visited(result.rotational.paddock_of_first_mob.begin(),
                              result.rotational.paddock_of_first_mob.end());
  EXPECT_GT(visited.size(), 20U) << "a year of rotation should reach most of the farm";
}

// **What the model reproduces, and what it does not.**
//
// Smith and Dawson give three reasons rotation beats set stocking: erect
// grasses and palatable legumes are encouraged, winter leaf area is greater, and
// summer roots reach deeper moisture. This model has the second of those and
// neither of the others, because grazing here is not selective and the sward
// carries no species-composition dynamics beyond two pools.
//
// So the pasture-side benefit shows up and the animal-side one does not, and
// pretending otherwise by tuning until rotation wins would be inventing a
// result. What is asserted is what the model actually demonstrates.
TEST(GrazingSystemComparisonTest, RotationGrowsMorePastureWhichIsTheMechanismItHas) {
  const Comparison result = compare(kLimitingHead);

  // Resting paddocks leaves more standing, which is the leaf-area argument the
  // source makes and the one mechanism this model carries.
  EXPECT_GT(result.rotational.mean_cover_kg_dm_per_ha(),
            result.set_stocked.mean_cover_kg_dm_per_ha())
      << "rotation did not grow more pasture, so even the mechanism the model has is missing";
  EXPECT_GT(result.rotational.lowest_cover_kg_dm_per_ha(),
            result.set_stocked.lowest_cover_kg_dm_per_ha())
      << "and it should not be grazed as low at its worst";
}

// The animal side, recorded rather than asserted away.
//
// At a stocking rate this farm cannot carry, rotation leaves the stock lighter
// than set stocking does, which is the opposite of what Smith and Dawson
// measured. The reason is mechanical and worth stating: a mob confined to two
// hectares of ninety-six cannot eat 1050 kg of dry matter a day however the
// moves are scheduled, while a set stocked mob with the run of the farm can
// always find it. The model has no counterweight because the things that make
// set stocking harmful over a year - selective grazing, clover being grazed
// out, no recovery - are exactly the ones it does not model.
//
// This is a limitation with a name, not a tuning problem. Open items 10 and 12,
// and B17 in the backlog.
TEST(GrazingSystemComparisonTest, TheAnimalSideAdvantageIsNotReproducedAndTheReasonIsKnown) {
  const Comparison result = compare(kLimitingHead);

  const double advantage =
      result.rotational.liveweight_change_kg() - result.set_stocked.liveweight_change_kg();

  GTEST_LOG_(INFO) << "at " << kLimitingHead << " head: set stocked "
                   << result.set_stocked.liveweight_change_kg() << " kg, rotational "
                   << result.rotational.liveweight_change_kg() << " kg; days short "
                   << result.set_stocked.days_short << " against " << result.rotational.days_short;

  // Rotation is behind, and this records by how much so that a change which
  // fixes it - selectivity, species dynamics, a cleverer farmer - shows up here
  // as a failure asking to be looked at rather than passing unnoticed.
  EXPECT_LT(advantage, 0.0)
      << "rotation now leads on liveweight; if that is real, the "
         "mechanism that changed should be named in docs/validation/verify.md "
         "and this test rewritten";
  EXPECT_GT(advantage, -12.0) << "and the gap has grown beyond anything explicable";

  // The confinement is the cause, and it is visible in the shortfall days.
  //
  // **This used to be exactly zero, and charging the walking is what changed
  // it.** Once TMC Eq. 24 was fed a distance (see WalkingDistance), every
  // animal costs about a megajoule a day more, and at 1400 head this farm
  // stopped being comfortably inside its feed supply - three days of the year
  // it is not. That is the model getting more expensive, not the comparison
  // breaking: a handful of days against the rotational arm's sixty-odd leaves
  // the contrast the test is about entirely intact.
  // **The contrast rather than an absolute** (verify.md, E80). This asserted
  // fewer than ten shortfall days for the set-stocked arm, which was a fact
  // about the over-productive farm this bundle used to describe. On real
  // weather both arms go short at 1400 head - 99 days against 151 - and the
  // comparison is not weaker for it: the confined mob is short half as often
  // again, and loses 19.4 kg against 13.9. What the test is about is the gap,
  // and the gap is wider than it was.
  EXPECT_LT(result.set_stocked.days_short, result.rotational.days_short * 3 / 4)
      << "a mob with the run of the farm should find feed more often than a confined one";
  EXPECT_GT(result.rotational.days_short, 20)
      << "and a confined one should go short, which is what drives the difference";
}

// Both arms still close their budgets. A comparison built on a run that lost
// dry matter would be comparing two errors.
TEST(GrazingSystemComparisonTest, BothArmsStillCloseTheirBudgets) {
  const Comparison result = compare(kLimitingHead);
  constexpr double kTolerance = 1e-9;

  for (const RunSummary* run : {&result.set_stocked, &result.rotational}) {
    EXPECT_TRUE(run->ledger.closes(core::Budget::DryMatter, run->closing_cover_kg_dm, kTolerance))
        << run->label << ": "
        << run->ledger.report(core::Budget::DryMatter, run->closing_cover_kg_dm);
    EXPECT_TRUE(run->ledger.closes(core::Budget::Nitrogen, run->closing_nitrogen_kg, kTolerance))
        << run->label;
    EXPECT_TRUE(run->ledger.closes(core::Budget::Water, run->closing_water_mm, kTolerance))
        << run->label;
  }
}

// The artefact. CI keeps this, so a change that alters how the two systems
// compare is visible as a plot rather than as a number in a log.
TEST(GrazingSystemComparisonTest, WritesTheComparisonForInspection) {
  const Comparison result = compare(kLimitingHead);

  const std::string path =
      std::string(PADDOCK_VALIDATION_OUTPUT_DIR) + "/grazing-system-comparison.csv";
  std::ofstream out(path);
  ASSERT_TRUE(out) << "cannot write " << path;

  out << "day,date,set_stocked_cover,rotational_cover,set_stocked_liveweight,"
         "rotational_liveweight\n";
  for (std::size_t day = 0; day < result.set_stocked.dates.size(); ++day) {
    out << day << ',' << result.set_stocked.dates[day].to_iso_string() << ','
        << result.set_stocked.cover_kg_dm_per_ha[day] << ','
        << result.rotational.cover_kg_dm_per_ha[day] << ',' << result.set_stocked.liveweight_kg[day]
        << ',' << result.rotational.liveweight_kg[day] << '\n';
  }
  out.close();

  GTEST_LOG_(INFO) << "wrote " << path;
}

}  // namespace
}  // namespace paddock::config
