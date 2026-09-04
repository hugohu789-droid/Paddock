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
#include <cstddef>
#include <string>
#include <vector>

#include <paddock/core/FarmAccount.hpp>
#include <paddock/core/FarmDecision.hpp>

#include "../support/ValueOf.hpp"

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
  // Beef + Lamb's Fact Sheet 260 baleage figure. Without a price the farm
  // refuses to buy at all - which is deliberate, and is what made this test
  // fail the moment the hardcoded constant came out of FarmDecision.cpp.
  prices.supplement_dollars_per_kg_dm = 0.541;
  return prices;
}

FarmOutlook comfortable_farm() {
  FarmOutlook outlook;
  outlook.today = Date{2024, 1, 15};
  outlook.head = 417;
  outlook.liveweight_kg = 55.0;
  outlook.cover_kg_dm_per_ha = 2400.0;
  outlook.minimum_cover_kg_dm_per_ha = 1600.0;
  outlook.consecutive_days_short = 0;
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
  drought.consecutive_days_short = 21;
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
  drought.consecutive_days_short = 60;
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
  EXPECT_EQ(tests::value_of(draft, "a drafting proposal").kind, ActionKind::SellFinishedStock);

  // 41 head at 38 kg liveweight and 43% dressing, at $7.80/kg carcass.
  EXPECT_NEAR(tests::value_of(draft, "a drafting proposal").dollars_in, 41.0 * 38.0 * 0.43 * 7.80,
              0.01);
  EXPECT_DOUBLE_EQ(tests::value_of(draft, "a drafting proposal").dollars_out, 0.0);
}

}  // namespace

// ---------------------------------------------------------------------------
// Consecutive, not cumulative.
//
// **A farm that goes short for three weeks running is in a drought; one that
// went short twenty-one times since July has had a hard year and may be
// standing in grass today.** The rule has always said it meant the first of
// those and was being handed the second, which fired it on the twenty-first
// short day of the run and - because a year-to-date total cannot fall - kept
// it fired every day afterwards. E98.

namespace {

/// Runs a sequence of short and good days past the rule and reports when it
/// asked to sell.
///
/// `pattern` is one character a day: 's' short, '.' fed. The counter is kept
/// the way the run loop keeps it, so this exercises the rule against the state
/// machine rather than against a number somebody set.
std::vector<int> destocking_days(const std::string& pattern, const DecisionPolicy& policy = {}) {
  FarmManager manager = a_manager(policy);
  FarmAccount account(40'000.0, modest_costs(), canterbury_prices(), 80.0);

  std::vector<int> sold_on;
  int consecutive = 0;
  int total = 0;
  for (std::size_t day = 0; day < pattern.size(); ++day) {
    if (pattern[day] == 's') {
      ++total;
      ++consecutive;
    } else {
      consecutive = 0;
    }

    FarmOutlook outlook = comfortable_farm();
    outlook.consecutive_days_short = consecutive;
    outlook.total_days_short = total;

    for (const Proposal& done : manager.decide(outlook, account)) {
      if (done.kind == ActionKind::Destock) {
        sold_on.push_back(static_cast<int>(day));
      }
    }
  }
  return sold_on;
}

}  // namespace

// Twenty short days in a row is not yet the farmer's threshold.
TEST(FarmDecisionTest, TwentyConsecutiveShortDaysDoNotSell) {
  EXPECT_TRUE(destocking_days(std::string(20, 's')).empty());
}

// Twenty-one is.
TEST(FarmDecisionTest, TwentyOneConsecutiveShortDaysDoSell) {
  const std::vector<int> sold = destocking_days(std::string(21, 's'));
  ASSERT_FALSE(sold.empty()) << "three weeks with the stock short and nothing was sold";
  EXPECT_EQ(sold.front(), 20) << "sold on a different day than the twenty-first";
}

// **One good day resets it**, which is the whole of the fix: ten short, a day
// of feed, eleven short is twenty-one short days and no drought.
TEST(FarmDecisionTest, AGoodDayInTheMiddleResetsTheCount) {
  const std::string pattern = std::string(10, 's') + "." + std::string(11, 's');
  EXPECT_TRUE(destocking_days(pattern).empty())
      << "twenty-one short days with a fed day among them was read as three weeks running";
}

// Scattered short days totalling well past the threshold never fire it.
TEST(FarmDecisionTest, ScatteredShortDaysNeverSellHoweverManyThereAre) {
  // Two short, one fed, over a hundred days: about seventy short days and never
  // more than two in a row.
  std::string pattern;
  for (int week = 0; week < 35; ++week) {
    pattern += "ss.";
  }
  const int short_days = static_cast<int>(std::count(pattern.begin(), pattern.end(), 's'));
  ASSERT_GT(short_days, 21) << "the pattern does not exceed the threshold in total";

  EXPECT_TRUE(destocking_days(pattern).empty())
      << short_days << " short days scattered over a season sold stock";
}

// **And the sale does not repeat every day afterwards.** Once the run is
// broken the count is zero, so a farm that got rain the day after selling is
// not asked to sell again on the strength of a total it can never work off.
TEST(FarmDecisionTest, TheSaleDoesNotRepeatOnceTheRunIsBroken) {
  const std::string pattern = std::string(21, 's') + std::string(30, '.');
  const std::vector<int> sold = destocking_days(pattern);

  ASSERT_EQ(sold.size(), 1U) << "sold " << sold.size()
                             << " times: the trigger stayed on after the drought broke";
  EXPECT_EQ(sold.front(), 20);
}

// A drought that goes on does keep selling, which is right - that is a farm
// still carrying more stock than it can feed - and it is a different statement
// from selling because of a total that cannot fall.
TEST(FarmDecisionTest, ADroughtThatContinuesKeepsSelling) {
  const std::vector<int> sold = destocking_days(std::string(25, 's'));
  EXPECT_GT(sold.size(), 1U);
  EXPECT_EQ(sold.front(), 20);
}

// The two counts are separate fields and the rule reads only one of them. A
// year-to-date total past the threshold, with nobody short today, sells
// nothing.
TEST(FarmDecisionTest, TheCumulativeTotalDoesNotDecideAnything) {
  FarmManager manager = a_manager();
  FarmAccount account(40'000.0, modest_costs(), canterbury_prices(), 80.0);

  FarmOutlook outlook = comfortable_farm();
  outlook.consecutive_days_short = 0;
  outlook.total_days_short = 300;

  for (const Proposal& done : manager.decide(outlook, account)) {
    EXPECT_NE(done.kind, ActionKind::Destock)
        << "a season's worth of short days sold stock on a day the farm was fed";
  }
}

}  // namespace paddock::core
