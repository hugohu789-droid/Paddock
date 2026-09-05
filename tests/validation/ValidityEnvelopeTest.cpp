// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// **The line past which this model has no animal to compare itself with**
// (verify.md, E115 and E116).
//
// Not a mortality threshold. Nothing dies at it, nothing is clamped at it, and
// the trajectory either side of it is the same trajectory. What changes is what
// a report is allowed to claim, and it changes through the provenance path that
// already existed: below the boundary the animal and money indicators become
// `Placeholder`, which the demo layer renders as *do not quote*.
//
// The boundary itself is not this project's. GrazPlan, quoting SCA (1990), puts
// SRW at the middle of the condition-score range and one score at 0.15 of
// normal weight, so score 0 - the bottom of the published 0-5 scale - is a
// relative condition of 0.625. The equivalent liveweight is whatever that ratio
// means for a given animal, and is deliberately not a constant anywhere.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include <paddock/config/EconomicsConfig.hpp>
#include <paddock/config/FarmDashboard.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/config/SpeciesConfig.hpp>
#include <paddock/core/AnimalEnergy.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::config {
namespace {

std::string bundle_path() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/demo-irrigation-off";
}

core::ManagementPolicy a_policy_that_buys() {
  core::ManagementPolicy policy;
  policy.minimum_cover_kg_dm_per_ha = 1200.0;
  policy.rotation_cover_threshold_kg_dm_per_ha = 2200.0;
  policy.supplement_me_mj_per_kg_dm = 10.0;
  policy.may_buy_feed = true;
  return policy;
}

ScenarioBundle a_bundle(int head) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.mobs.front().head = head;
  return bundle;
}

/// The priced path, which is what `paddock dashboard` and the E114 ladder both
/// use: a bundle that names its economics is run with them (E79). The flock
/// behaves differently without a feed market to buy in, so an unpriced run
/// would not be measuring the farm this test is about.
RunSummary run(const ScenarioBundle& bundle, const core::ManagementPolicy& policy,
               std::string label) {
  const FarmEconomics economics = load_economics(bundle.economics_path);
  return run_managed_scenario(bundle, policy, bundle.diet, std::move(label),
                              business_from(bundle, economics));
}

/// The shipped ewe, for the arithmetic tests below.
core::AnimalClassParameters the_shipped_ewe() {
  const std::vector<SpeciesDefinition> species =
      load_species_directory(std::string(PADDOCK_DATA_DIR) + "/species");
  for (const SpeciesDefinition& each : species) {
    if (each.name == "sheep_ewe") {
      return each.energy;
    }
  }
  return {};
}

core::AnimalState a_mature_ewe(double liveweight_kg) {
  core::AnimalState state;
  state.liveweight_kg = liveweight_kg;
  state.age_days = 1500.0;
  state.young = 1.0;
  return state;
}

// **Verification.** The mapping is two sentences of GrazPlan's and one
// subtraction. If either constant moved, this is what would say so.
TEST(ValidityEnvelopeTest, TheBoundaryIsTheBottomOfThePublishedConditionScoreScale) {
  EXPECT_DOUBLE_EQ(core::kLowestSupportedRelativeCondition, 0.625);
  EXPECT_DOUBLE_EQ(core::condition_score(1.0), 2.5) << "SRW is the middle of the range";
  EXPECT_DOUBLE_EQ(core::condition_score(core::kLowestSupportedRelativeCondition), 0.0)
      << "and 0.625 is score 0, the bottom of it";
  EXPECT_NEAR(core::condition_score(0.775), 1.0, 1e-9) << "one score is 0.15 of normal weight";
}

// **The equivalent liveweight is a property of the animal, not a number.**
TEST(ValidityEnvelopeTest, TheEquivalentLiveweightFollowsTheAnimalsNormalWeight) {
  core::AnimalClassParameters ewe = the_shipped_ewe();
  ASSERT_GT(ewe.standard_reference_weight_kg, 0.0) << "the shipped ewe did not load";

  const double shipped = core::liveweight_at_relative_condition(
      ewe, a_mature_ewe(55.0), core::kLowestSupportedRelativeCondition);
  EXPECT_NEAR(shipped, 26.4, 0.1) << "the 66 kg ewe leaves the scale near 26.4 kg";

  // A bigger frame moves it, and nothing in the code had to be told.
  core::AnimalClassParameters bigger = ewe;
  bigger.standard_reference_weight_kg = 80.0;
  const double for_the_bigger_ewe = core::liveweight_at_relative_condition(
      bigger, a_mature_ewe(70.0), core::kLowestSupportedRelativeCondition);
  EXPECT_GT(for_the_bigger_ewe, shipped + 2.0)
      << "a heavier animal leaves the scale at a heavier weight, because the boundary is a ratio";

  // And the weight it returns really is at the boundary.
  EXPECT_NEAR(core::relative_condition(ewe, a_mature_ewe(shipped)),
              core::kLowestSupportedRelativeCondition, 1e-6);
}

// **Inclusive.** Exactly at the bottom of the scale is still on the scale.
TEST(ValidityEnvelopeTest, TheBoundaryItselfIsInside) {
  const core::AnimalClassParameters ewe = the_shipped_ewe();
  const double at_the_line = core::liveweight_at_relative_condition(
      ewe, a_mature_ewe(55.0), core::kLowestSupportedRelativeCondition);

  RunSummary::AnimalDomain domain;
  domain.lowest_relative_condition = core::relative_condition(ewe, a_mature_ewe(at_the_line));
  EXPECT_GE(domain.lowest_relative_condition, core::kLowestSupportedRelativeCondition - 1e-9);
  EXPECT_TRUE(domain.inside()) << "no crossing was recorded, so the run is inside";
}

// **The flagship, which is what a customer sees.**
TEST(ValidityEnvelopeTest, TheFlagshipEweStaysComfortablyInside) {
  const ScenarioBundle bundle = a_bundle(417);
  const RunSummary summary = run(bundle, *bundle.management, "flagship off");

  EXPECT_TRUE(summary.animal_domain.inside());
  EXPECT_FALSE(summary.animal_domain.first_crossing.has_value());
  EXPECT_GT(summary.animal_domain.lowest_relative_condition, 0.85)
      << "the trough is a thin ewe, not an unsupported one";
  EXPECT_GT(core::condition_score(summary.animal_domain.lowest_relative_condition), 1.5)
      << "and it is well above score 0";
}

// **A run that leaves the domain is marked, and keeps running.**
TEST(ValidityEnvelopeTest, LeavingTheDomainIsRecordedAndDoesNotStopTheRun) {
  core::ManagementPolicy starved = a_policy_that_buys();
  starved.supplement_market.available_kg_dm = 0.0;
  const ScenarioBundle bundle = a_bundle(1'300);
  const RunSummary summary = run(bundle, starved, "outside");

  ASSERT_FALSE(summary.animal_domain.inside()) << "1,300 head with no feed to buy has to cross";
  EXPECT_TRUE(summary.animal_domain.first_crossing.has_value());
  EXPECT_LT(summary.animal_domain.lowest_relative_condition,
            core::kLowestSupportedRelativeCondition);
  EXPECT_FALSE(summary.animal_domain.cohort.empty()) << "the cohort is named";
  EXPECT_GT(summary.animal_domain.boundary_liveweight_kg, 0.0)
      << "and so is the liveweight the boundary means for it";

  // The run finished: the trajectory was not stopped, clamped or killed.
  EXPECT_EQ(summary.dates.size(), summary.cover_kg_dm_per_ha.size());
  EXPECT_GT(summary.dates.size(), 360U) << "a full year still ran";
  EXPECT_GT(summary.eaten_kg_dm, 0.0);
}

// **Deterministic after crossing.** The boundary is a measurement, so a run
// that crosses it replays exactly like one that does not.
TEST(ValidityEnvelopeTest, ARunThatCrossesStillReplaysExactly) {
  core::ManagementPolicy starved = a_policy_that_buys();
  starved.supplement_market.available_kg_dm = 0.0;
  const ScenarioBundle bundle = a_bundle(1'300);

  const RunSummary first = run(bundle, starved, "first");
  const RunSummary second = run(bundle, starved, "second");

  ASSERT_FALSE(first.animal_domain.inside());
  EXPECT_EQ(first.animal_domain.lowest_relative_condition,
            second.animal_domain.lowest_relative_condition)
      << "bit-identical, not merely close";
  EXPECT_EQ(first.eaten_kg_dm, second.eaten_kg_dm);
  EXPECT_EQ(first.closing_head, second.closing_head);
  EXPECT_EQ(first.animal_domain.first_crossing->to_iso_string(),
            second.animal_domain.first_crossing->to_iso_string());
}

// **What the page may say.** Animal and money go to DoNotQuote; pasture, water
// and environment keep exactly the standing they had.
TEST(ValidityEnvelopeTest, OutsideTheDomainOnlyTheAnimalAndMoneyFiguresAreDowngraded) {
  const ScenarioBundle bundle = a_bundle(1'300);

  core::ManagementPolicy fed = a_policy_that_buys();
  const RunSummary inside = run(bundle, fed, "inside");
  const FarmDashboard before = build_dashboard(bundle, inside, "inside");

  core::ManagementPolicy starved = a_policy_that_buys();
  starved.supplement_market.available_kg_dm = 0.0;
  const RunSummary outside = run(bundle, starved, "outside");
  const FarmDashboard after = build_dashboard(bundle, outside, "outside");

  ASSERT_TRUE(inside.animal_domain.inside());
  ASSERT_FALSE(outside.animal_domain.inside());
  EXPECT_TRUE(before.animal_domain_warning.empty());
  EXPECT_FALSE(after.animal_domain_warning.empty());

  const auto trust_of = [](const FarmDashboard& board, const std::string& name) {
    for (const Indicator& indicator : board.all_indicators()) {
      if (indicator.name == name) {
        return indicator.trust;
      }
    }
    return Provenance::Direct;
  };

  // The ground does not become less true because the stock did.
  for (const char* untouched : {"Pasture grown", "Rainfall", "Evapotranspiration", "Drainage"}) {
    EXPECT_EQ(trust_of(after, untouched), trust_of(before, untouched))
        << untouched << " must keep its standing";
  }

  // What the stock did, and what it was worth, must not be quotable.
  for (const char* downgraded : {"Eaten", "Utilisation", "Days short of feed", "Closing stock"}) {
    EXPECT_EQ(trust_of(after, downgraded), Provenance::Placeholder)
        << downgraded << " must not be quotable outside the domain";
  }
  EXPECT_EQ(trust_of(after, "Closing balance"), Provenance::Placeholder)
      << "a balance built on an animal this model cannot describe is not a balance";

  // The warning names what a reader needs: when, which mob, and how far.
  EXPECT_NE(after.animal_domain_warning.find("0.625"), std::string::npos);
  EXPECT_NE(after.animal_domain_warning.find(outside.animal_domain.cohort), std::string::npos);
  EXPECT_NE(after.animal_domain_warning.find("not be read as"), std::string::npos);
  EXPECT_NE(as_text(after).find("Outside the supported animal-production domain"),
            std::string::npos)
      << "and it has to reach the page, not just the struct";
}

// **A stocking comparison with one bad rung reports no optimum at all.**
TEST(ValidityEnvelopeTest, AStockingComparisonRefusesAnOptimumWhenAnyRungIsOutside) {
  core::ManagementPolicy starved = a_policy_that_buys();
  starved.supplement_market.available_kg_dm = 0.0;

  const ScenarioBundle light_bundle = a_bundle(417);
  const ScenarioBundle heavy_bundle = a_bundle(1'300);
  const RunSummary light = run(light_bundle, starved, "417");
  const RunSummary heavy = run(heavy_bundle, starved, "1300");

  ASSERT_TRUE(light.animal_domain.inside());
  ASSERT_FALSE(heavy.animal_domain.inside());

  EXPECT_TRUE(may_report_stocking_optimum({&light}))
      << "a ladder entirely inside the envelope may be ranked";
  EXPECT_FALSE(may_report_stocking_optimum({&light, &heavy}))
      << "one rung outside withholds the ranking for the whole comparison, not just that rung";
}

// **An unsourced market quantity says what it is, every time.**
TEST(ValidityEnvelopeTest, AFiniteMarketIsLabelledASensitivityAssumption) {
  const ScenarioBundle bundle = a_bundle(900);

  const RunSummary unlimited = run(bundle, a_policy_that_buys(), "unlimited");
  EXPECT_TRUE(build_dashboard(bundle, unlimited, "unlimited").supplement_market_warning.empty())
      << "a run given no market quantity is making no assumption to warn about";

  core::ManagementPolicy finite = a_policy_that_buys();
  finite.supplement_market.available_kg_dm = 20'000.0;
  const RunSummary capped = run(bundle, finite, "capped");
  const FarmDashboard board = build_dashboard(bundle, capped, "capped");

  EXPECT_NE(board.supplement_market_warning.find("Sensitivity assumption"), std::string::npos);
  EXPECT_NE(board.supplement_market_warning.find("not a New Zealand market figure"),
            std::string::npos);
  EXPECT_NE(as_text(board).find("Supplement market is an assumption"), std::string::npos);
}

}  // namespace
}  // namespace paddock::config
