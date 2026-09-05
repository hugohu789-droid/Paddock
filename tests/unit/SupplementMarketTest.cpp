// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// **The merchant who may not have it** (verify.md, E114).
//
// Until this class existed, a farm that could pay could farm without grass:
// management worked out a shortfall, called it a purchase, and the feed
// appeared. E55 measured what that did - profit rising monotonically to 11.9
// SU/ha on a Canterbury dryland block, with `days short of feed` reading zero at
// every rung - and concluded that a stocking policy built on it would optimise
// against a constraint that does not bind.
//
// These tests fix the four quantities apart from each other. Requested is what
// management wanted; purchased is what the market had; offered is what the
// farmer put in front of the stock; consumed is what the animal could eat. The
// market decides the second and touches none of the others.

#include <gtest/gtest.h>

#include <limits>
#include <utility>

#include <paddock/core/SupplementMarket.hpp>

namespace paddock::core {
namespace {

Date a_day(int day) {
  return Date{2023, 11, day};
}

SupplementMarketPolicy holding(double kg_dm) {
  SupplementMarketPolicy policy;
  policy.available_kg_dm = kg_dm;
  return policy;
}

// **Verification.** The old behaviour, kept and named. An absent quantity is
// unlimited, and a run reports which it had rather than assuming.
TEST(SupplementMarketTest, AnUndeclaredMarketIsUnlimitedAndSaysSo) {
  SupplementMarket market;

  EXPECT_FALSE(market.is_finite());
  EXPECT_EQ(market.remaining_kg_dm(), std::numeric_limits<double>::infinity());
  EXPECT_DOUBLE_EQ(market.buy(a_day(1), 50'000.0), 50'000.0);
  EXPECT_DOUBLE_EQ(market.buy(a_day(2), 50'000.0), 50'000.0);
  EXPECT_DOUBLE_EQ(market.total_unfilled_kg_dm(), 0.0);
  EXPECT_EQ(market.short_days(), 0);
  EXPECT_FALSE(market.is_exhausted());
}

// **The case E55 could not produce.** Demand that looks unlimited, against a
// market that is not.
TEST(SupplementMarketTest, UnlimitedLookingDemandMeetsAFiniteMarket) {
  SupplementMarket market(holding(1'000.0));

  double supplied = 0.0;
  for (int day = 1; day <= 30; ++day) {
    supplied += market.buy(a_day(day), 10'000.0);
  }

  EXPECT_DOUBLE_EQ(supplied, 1'000.0) << "the market cannot sell what it does not have";
  EXPECT_DOUBLE_EQ(market.total_requested_kg_dm(), 300'000.0);
  EXPECT_DOUBLE_EQ(market.total_purchased_kg_dm(), 1'000.0);
  EXPECT_DOUBLE_EQ(market.total_unfilled_kg_dm(), 299'000.0)
      << "and the gap stays on the books rather than disappearing";
  EXPECT_EQ(market.short_days(), 30);
}

TEST(SupplementMarketTest, APartlyFilledRequestReturnsWhatThereIs) {
  SupplementMarket market(holding(800.0));

  EXPECT_DOUBLE_EQ(market.buy(a_day(1), 500.0), 500.0) << "filled";
  EXPECT_DOUBLE_EQ(market.buy(a_day(2), 500.0), 300.0) << "partly filled";
  EXPECT_EQ(market.short_days(), 1) << "only the day that could not be filled counts";
  EXPECT_DOUBLE_EQ(market.remaining_kg_dm(), 0.0);
}

TEST(SupplementMarketTest, AnExhaustedMarketSuppliesNothingAndKeepsCounting) {
  SupplementMarket market(holding(100.0));
  EXPECT_DOUBLE_EQ(market.buy(a_day(1), 100.0), 100.0);
  ASSERT_TRUE(market.is_exhausted());

  EXPECT_DOUBLE_EQ(market.buy(a_day(2), 250.0), 0.0);
  EXPECT_DOUBLE_EQ(market.buy(a_day(3), 250.0), 0.0);
  EXPECT_DOUBLE_EQ(market.total_unfilled_kg_dm(), 500.0);
  EXPECT_EQ(market.short_days(), 2) << "a market with nothing left is short every day it is asked";
}

TEST(SupplementMarketTest, PurchasedFeedCannotExceedMarketStock) {
  for (const double stock : {0.0, 1.0, 250.0, 10'000.0}) {
    SupplementMarket market(holding(stock));
    for (int day = 1; day <= 20; ++day) {
      const double got = market.buy(a_day(day), 5'000.0);
      EXPECT_GE(got, 0.0);
      EXPECT_LE(got, 5'000.0) << "never more than was asked for";
    }
    EXPECT_LE(market.total_purchased_kg_dm(), stock + 1e-9)
        << "never more than the market held, at stock " << stock;
  }
}

// **A window is a management constraint too**, and a request outside it is a
// request that failed rather than one that was never made.
TEST(SupplementMarketTest, OutsideItsWindowTheMarketSuppliesNothing) {
  SupplementMarketPolicy policy = holding(5'000.0);
  policy.window = DateRange{Date{2023, 11, 10}, Date{2023, 11, 20}};
  SupplementMarket market(policy);

  EXPECT_DOUBLE_EQ(market.buy(a_day(9), 100.0), 0.0) << "before it opens";
  EXPECT_DOUBLE_EQ(market.buy(a_day(10), 100.0), 100.0) << "on the first day";
  EXPECT_DOUBLE_EQ(market.buy(a_day(20), 100.0), 100.0) << "on the last day";
  EXPECT_DOUBLE_EQ(market.buy(a_day(21), 100.0), 0.0) << "after it closes";

  EXPECT_EQ(market.short_days(), 2);
  EXPECT_DOUBLE_EQ(market.total_unfilled_kg_dm(), 200.0);
  EXPECT_DOUBLE_EQ(market.remaining_kg_dm(), 4'800.0)
      << "a closed market sells nothing, and loses nothing";
}

// **Same inputs, same outputs, twice.** There is no random draw here and no
// dependence on call order beyond the arithmetic, which is why the run buys
// once a day for the whole farm rather than once per mob.
TEST(SupplementMarketTest, TheSameRequestsGiveTheSameResultTwice) {
  const auto run = [] {
    SupplementMarket market(holding(1'234.5));
    double total = 0.0;
    for (int day = 1; day <= 25; ++day) {
      total += market.buy(a_day(day), 60.0 + static_cast<double>(day));
    }
    return std::pair<double, int>{total, market.short_days()};
  };

  const auto first = run();
  const auto second = run();
  EXPECT_EQ(first.first, second.first) << "bit-identical, not merely close";
  EXPECT_EQ(first.second, second.second);
}

TEST(SupplementMarketTest, ANegativeQuantityIsRefusedRatherThanRun) {
  SupplementMarketPolicy policy;
  policy.available_kg_dm = -1.0;
  EXPECT_FALSE(policy.validation_error().empty());

  SupplementMarketPolicy inverted = holding(10.0);
  inverted.window = DateRange{Date{2023, 11, 20}, Date{2023, 11, 10}};
  EXPECT_FALSE(inverted.validation_error().empty());

  EXPECT_TRUE(holding(0.0).validation_error().empty()) << "a market with nothing in it is legal";
}

// A request of nothing is not a shortage.
TEST(SupplementMarketTest, AskingForNothingIsNotAShortage) {
  SupplementMarket market(holding(0.0));
  EXPECT_DOUBLE_EQ(market.buy(a_day(1), 0.0), 0.0);
  EXPECT_EQ(market.short_days(), 0);
  EXPECT_DOUBLE_EQ(market.total_requested_kg_dm(), 0.0);
}

}  // namespace
}  // namespace paddock::core
