// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// The farmer's economic decisions.
///
/// These check the three things the proposal design exists to buy, because
/// anything less would pass just as well against rules that acted directly:
/// that a farm cannot be pushed below zero, that a decision carries its reason,
/// and that a rule is testable without a farm at all.

#include <gtest/gtest.h>

#include <algorithm>

#include <paddock/core/FarmAccount.hpp>
#include <paddock/core/FarmDecision.hpp>

namespace paddock::core {
namespace {

OperatingCosts modest_costs() {
  OperatingCosts costs;
  costs.fertiliser = 153.62;
  costs.animal_health = 55.04;
  costs.shearing = 46.10;
  costs.wages_and_salaries = 87.31;
  return costs;
}

Prices canterbury_prices() {
  Prices prices;
  prices.lamb_dollars_per_kg_carcass = 7.80;
  prices.wool_dollars_per_kg = 3.80;
  prices.cull_ewe_dollars_per_head = 90.0;
  return prices;
}

FarmOutlook comfortable_farm() {
  FarmOutlook outlook;
  outlook.today = Date{2024, 1, 15};
  outlook.head = 417;
  outlook.liveweight_kg = 55.0;
  outlook.cover_kg_dm_per_ha = 2400.0;
  outlook.minimum_cover_kg_dm_per_ha = 1600.0;
  outlook.days_short = 0;
  outlook.hectares = 80.0;
  outlook.balance_dollars = 40'000.0;
  outlook.daily_operating_cost_dollars = 203.0;
  return outlook;
}

FarmManager a_manager(const DecisionPolicy& policy = {}) {
  return {policy, standard_rules(policy)};
}

TEST(FarmDecisionTest, TheDefaultPolicyIsUsable) {
  EXPECT_EQ(DecisionPolicy{}.invalid_reason(), "");
}

// A farm with feed, weight and money does nothing dramatic. The mob is breeding
// ewes, so nothing is drafted however heavy they are.
TEST(FarmDecisionTest, AComfortableFarmNeitherBuysNorSells) {
  FarmAccount account(40'000.0, modest_costs(), canterbury_prices(), 80.0);
  FarmManager manager = a_manager();

  const std::vector<Proposal> done = manager.decide(comfortable_farm(), account);
  EXPECT_TRUE(done.empty()) << "nothing was wrong, so nothing should have happened";
  EXPECT_DOUBLE_EQ(account.balance(), 40'000.0);
}

// **The drought answer.** Three weeks short of feed and the farmer sells,
// whatever the bank balance says.
TEST(FarmDecisionTest, ThreeWeeksShortOfFeedSellsStock) {
  FarmAccount account(40'000.0, modest_costs(), canterbury_prices(), 80.0);
  FarmManager manager = a_manager();

  FarmOutlook drought = comfortable_farm();
  drought.days_short = 21;
  drought.cover_kg_dm_per_ha = 1200.0;

  const std::vector<Proposal> done = manager.decide(drought, account);
  const auto sold = std::find_if(done.begin(), done.end(),
                                 [](const Proposal& p) { return p.kind == ActionKind::Destock; });
  ASSERT_NE(sold, done.end()) << "a mob three weeks short of feed should be cut";

  EXPECT_EQ(sold->head, 83) << "a fifth of 417";
  EXPECT_GT(sold->dollars_in, 0.0);
  EXPECT_NE(sold->because.find("21 days short of feed"), std::string::npos)
      << "the reason has to reach the ledger, or a report cannot say why the farm shrank";
  EXPECT_GT(account.balance(), 40'000.0);
}

// **The solvency answer, and the one a threshold cannot express.** The feed is
// short but so is the money, so the farmer sells rather than buys - the
// opposite of what the same farm with cash would do.
TEST(FarmDecisionTest, AFarmShortOfCashSellsWhereAFarmWithCashBuys) {
  const DecisionPolicy policy;
  FarmOutlook pinched = comfortable_farm();
  pinched.cover_kg_dm_per_ha = 1400.0;

  // With money in the bank: buys feed, sells nothing.
  {
    FarmAccount account(40'000.0, modest_costs(), canterbury_prices(), 80.0);
    FarmManager manager = a_manager(policy);
    const std::vector<Proposal> done = manager.decide(pinched, account);

    EXPECT_TRUE(std::any_of(done.begin(), done.end(),
                            [](const Proposal& p) { return p.kind == ActionKind::BuyFeed; }));
    EXPECT_FALSE(std::any_of(done.begin(), done.end(),
                             [](const Proposal& p) { return p.kind == ActionKind::Destock; }));
  }

  // Same farm, same feed, a fortnight of cash: sells instead.
  {
    FarmOutlook broke = pinched;
    broke.balance_dollars = 14.0 * broke.daily_operating_cost_dollars;

    FarmAccount account(broke.balance_dollars, modest_costs(), canterbury_prices(), 80.0);
    FarmManager manager = a_manager(policy);
    const std::vector<Proposal> done = manager.decide(broke, account);

    EXPECT_FALSE(std::any_of(done.begin(), done.end(), [](const Proposal& p) {
      return p.kind == ActionKind::BuyFeed;
    })) << "a farmer a fortnight from empty does not buy baleage";
    const auto sold = std::find_if(done.begin(), done.end(),
                                   [](const Proposal& p) { return p.kind == ActionKind::Destock; });
    ASSERT_NE(sold, done.end());
    EXPECT_NE(sold->because.find("stay solvent"), std::string::npos)
        << "and the reason should say which of the two things went wrong";
  }
}

// **The constraint the whole design is for.** A purchase that would overdraw
// the account is refused rather than applied, so no sequence of decisions can
// put the farm below zero.
TEST(FarmDecisionTest, NoDecisionCanTakeTheFarmBelowZero) {
  FarmAccount account(5.0, modest_costs(), canterbury_prices(), 80.0);
  FarmManager manager = a_manager();

  FarmOutlook nearly_broke = comfortable_farm();
  nearly_broke.cover_kg_dm_per_ha = 1000.0;
  nearly_broke.balance_dollars = 5.0;
  nearly_broke.head = 40;  // below minimum_head, so it cannot destock its way out

  const std::vector<Proposal> refused = manager.decide(nearly_broke, account);
  EXPECT_TRUE(refused.empty()) << "nothing it could afford, so nothing it did";
  EXPECT_FALSE(account.is_insolvent())
      << "a proposal that would overdraw has to be refused, not applied; balance "
      << account.balance();
}

// The farm cannot be emptied one fifth at a time.
TEST(FarmDecisionTest, DestockingStopsAtTheFloor) {
  DecisionPolicy policy;
  policy.minimum_head = 50;

  FarmOutlook drought = comfortable_farm();
  drought.days_short = 60;
  drought.head = 55;

  FarmAccount account(40'000.0, modest_costs(), canterbury_prices(), 80.0);
  FarmManager manager = a_manager(policy);

  const std::vector<Proposal> done = manager.decide(drought, account);
  const auto sold = std::find_if(done.begin(), done.end(),
                                 [](const Proposal& p) { return p.kind == ActionKind::Destock; });
  ASSERT_NE(sold, done.end());
  EXPECT_LE(sold->head, 5) << "it may sell down to the floor and no further";

  drought.head = 50;
  const std::vector<Proposal> at_floor = manager.decide(drought, account);
  EXPECT_FALSE(std::any_of(at_floor.begin(), at_floor.end(), [](const Proposal& p) {
    return p.kind == ActionKind::Destock;
  })) << "at the floor it stops entirely";
}

// **A rule is a value, so it can be checked without a farm.** This is the third
// thing the design buys, and it is what makes the arithmetic above auditable.
TEST(FarmDecisionTest, ARuleCanBeAskedInIsolation) {
  const DecisionPolicy policy;
  const std::vector<DecisionRule> rules = standard_rules(policy);
  ASSERT_EQ(rules.size(), 4U);

  FarmOutlook fat = comfortable_farm();
  fat.liveweight_kg = 42.0;
  fat.is_finishing_class = true;

  // The drafting rule is the second, and it neither needs an account nor
  // touches one.
  const std::optional<Proposal> draft = rules[1](fat, canterbury_prices());
  ASSERT_TRUE(draft.has_value());
  EXPECT_EQ(draft.value().kind, ActionKind::SellFinishedStock);

  // 41 head at 38 kg liveweight and 43% dressing, at $7.80/kg carcass.
  EXPECT_NEAR(draft.value().dollars_in, 41.0 * 38.0 * 0.43 * 7.80, 0.01);
  EXPECT_DOUBLE_EQ(draft.value().dollars_out, 0.0);
}

}  // namespace
}  // namespace paddock::core
