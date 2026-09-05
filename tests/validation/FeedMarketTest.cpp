// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// **The causal chain a finite feed market is supposed to produce** (verify.md,
// E114).
//
//     management requests supplement
//       -> the market supplies all, part, or none
//       -> the farmer offers what arrived
//       -> the animal eats what its appetite allows
//       -> whatever is still missing stays visible as a feed-supply shortfall
//       -> destocking may fire on the existing consecutive-shortage policy
//
// The point of the class is the third arrow and the fifth. The market decides
// what the farm can *get*; it must never decide what an animal can *eat*, and
// it must never make a shortage disappear. E55 measured what the absence of it
// did: profit rising monotonically to 11.9 SU/ha with `days short of feed`
// reading zero at every rung, because feed always arrived.

#include <gtest/gtest.h>

#include <string>
#include <utility>

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

core::ManagementPolicy a_policy_that_buys() {
  core::ManagementPolicy policy;
  policy.minimum_cover_kg_dm_per_ha = 1200.0;
  policy.rotation_cover_threshold_kg_dm_per_ha = 2200.0;
  policy.supplement_me_mj_per_kg_dm = 10.0;
  policy.may_buy_feed = true;
  return policy;
}

/// Stocked hard enough that this farm cannot feed itself, so the market is
/// actually asked for something.
constexpr int kHeavyHead = 900;

RunSummary run(int head, const core::ManagementPolicy& policy, std::string label) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.mobs.front().head = head;
  return run_managed_scenario(bundle, policy, pasture_diet(), std::move(label));
}

// **The old behaviour, named rather than assumed.** An undeclared market is
// unlimited, every request is filled, and the summary says which it was.
TEST(FeedMarketTest, AnUndeclaredMarketFillsEveryRequestAndReportsThatItIsUnlimited) {
  const RunSummary summary = run(kHeavyHead, a_policy_that_buys(), "unlimited");

  EXPECT_FALSE(summary.supplement_market_is_finite);
  EXPECT_GT(summary.supplement_requested_kg_dm, 0.0)
      << "this stocking rate has to ask for feed, or the test proves nothing";
  EXPECT_DOUBLE_EQ(summary.supplement_unfilled_kg_dm, 0.0);
  EXPECT_EQ(summary.supplement_market_short_days, 0);
}

// **The case E55 could not produce.** The same farm, the same stocking, a market
// that runs out - and the shortage arrives instead of the feed.
TEST(FeedMarketTest, AMarketShortageBecomesAFeedSupplyShortage) {
  const RunSummary unlimited = run(kHeavyHead, a_policy_that_buys(), "unlimited");

  core::ManagementPolicy finite = a_policy_that_buys();
  // A tenth of what this farm asked for, so the market certainly binds. The
  // number is a test fixture, not a claim about the New Zealand feed trade.
  finite.supplement_market.available_kg_dm = unlimited.supplement_requested_kg_dm / 10.0;
  const RunSummary constrained = run(kHeavyHead, finite, "finite");

  EXPECT_TRUE(constrained.supplement_market_is_finite);
  EXPECT_LT(constrained.supplement_purchased_kg_dm, unlimited.supplement_purchased_kg_dm)
      << "a market that runs out sells less";
  EXPECT_GT(constrained.supplement_unfilled_kg_dm, 0.0)
      << "and what it could not sell stays on the books";
  EXPECT_GT(constrained.supplement_market_short_days, 0);

  // The chain's fifth arrow: the feed that did not arrive shows up as stock
  // that did not get fed, rather than quietly not mattering.
  EXPECT_GT(constrained.feed_supply_short_days, unlimited.feed_supply_short_days)
      << "feed that never arrived has to leave the mob short";
}

// **Purchased never exceeds the market, and offered never exceeds purchased.**
TEST(FeedMarketTest, PurchasedFeedCannotExceedTheMarketAndOfferedCannotExceedPurchased) {
  const RunSummary reference = run(kHeavyHead, a_policy_that_buys(), "reference");

  for (const double fraction : {0.0, 0.05, 0.25, 0.75}) {
    core::ManagementPolicy finite = a_policy_that_buys();
    const double stock = reference.supplement_requested_kg_dm * fraction;
    finite.supplement_market.available_kg_dm = stock;
    const RunSummary summary = run(kHeavyHead, finite, "capped");

    EXPECT_LE(summary.supplement_purchased_kg_dm, stock + 1e-6)
        << "bought more than the market held, at fraction " << fraction;
    EXPECT_LE(summary.supplement_purchased_kg_dm, summary.supplement_requested_kg_dm + 1e-6)
        << "bought more than was asked for, at fraction " << fraction;

    // Offered is what the mobs were handed, which is the stack plus what was
    // bought. It can never be more than that, and what the stock actually ate
    // can never be more than what they were offered.
    const double offered = summary.supplement_purchased_kg_dm + summary.conserved_fed_kg_dm;
    EXPECT_LE(summary.bought_feed_kg_dm(), offered + 1e-6)
        << "consumed more than was offered, at fraction " << fraction;
  }
}

// **The market must not be able to feed an animal past its appetite.** With feed
// freely available the binding constraint on a lactating ewe is still her own
// intake capacity, and that count is unmoved by how much feed the merchant has.
TEST(FeedMarketTest, FeedOnHandDoesNotRaiseWhatAnAnimalCanEat) {
  const RunSummary unlimited = run(kHeavyHead, a_policy_that_buys(), "unlimited");

  core::ManagementPolicy generous = a_policy_that_buys();
  // Ten times what the farm asked for: the market can never bind here.
  generous.supplement_market.available_kg_dm = unlimited.supplement_requested_kg_dm * 10.0;
  const RunSummary summary = run(kHeavyHead, generous, "generous");

  EXPECT_TRUE(summary.supplement_market_is_finite);
  EXPECT_DOUBLE_EQ(summary.supplement_unfilled_kg_dm, 0.0) << "a market this large never binds";

  // A market that never binds must reproduce the unlimited run exactly. This is
  // the assertion that says the market constrains management and nothing else:
  // if any physiology had been touched, these would drift.
  EXPECT_EQ(summary.feed_supply_short_days, unlimited.feed_supply_short_days);
  EXPECT_DOUBLE_EQ(summary.eaten_kg_dm, unlimited.eaten_kg_dm);
  EXPECT_EQ(summary.closing_head, unlimited.closing_head);

  // Note what is *not* asserted here: that this bundle records any
  // capacity-limited day at all. It records none - `canterbury-grazed` carries a
  // dry mob, and an animal that is not milking rarely meets its own ceiling.
  // The claim being fixed is the equality above: however much feed the merchant
  // has, the capacity count does not move, because appetite is physiology and
  // the market is not. `IntakeCausalityTest` is where the ceiling itself binds.
  EXPECT_EQ(summary.intake_capacity_limited_days, unlimited.intake_capacity_limited_days);
}

// **Same bundle, same market, same numbers.** No random draw enters here, and
// the market is asked once a day for the whole farm rather than once per mob,
// so nothing depends on the order mobs sit in.
TEST(FeedMarketTest, AConstrainedRunReplaysExactly) {
  core::ManagementPolicy finite = a_policy_that_buys();
  finite.supplement_market.available_kg_dm = 25'000.0;

  const RunSummary first = run(kHeavyHead, finite, "first");
  const RunSummary second = run(kHeavyHead, finite, "second");

  EXPECT_EQ(first.supplement_requested_kg_dm, second.supplement_requested_kg_dm);
  EXPECT_EQ(first.supplement_purchased_kg_dm, second.supplement_purchased_kg_dm);
  EXPECT_EQ(first.supplement_unfilled_kg_dm, second.supplement_unfilled_kg_dm);
  EXPECT_EQ(first.supplement_market_short_days, second.supplement_market_short_days);
  EXPECT_EQ(first.feed_supply_short_days, second.feed_supply_short_days);
  EXPECT_EQ(first.eaten_kg_dm, second.eaten_kg_dm) << "bit-identical, not merely close";
  EXPECT_EQ(first.closing_head, second.closing_head);
}

// **A market holding nothing is not the same as a farmer who will not buy**, and
// both have to be expressible. One is a merchant with an empty yard; the other
// is a decision.
TEST(FeedMarketTest, AnEmptyMarketAndAFarmerWhoWillNotBuyAreDifferentStates) {
  core::ManagementPolicy empty_market = a_policy_that_buys();
  empty_market.supplement_market.available_kg_dm = 0.0;
  const RunSummary nothing_to_buy = run(kHeavyHead, empty_market, "empty market");

  core::ManagementPolicy will_not_buy = a_policy_that_buys();
  will_not_buy.may_buy_feed = false;
  const RunSummary refuses = run(kHeavyHead, will_not_buy, "will not buy");

  EXPECT_GT(nothing_to_buy.supplement_requested_kg_dm, 0.0)
      << "the farmer still asks, and the asking is what the report needs to show";
  EXPECT_DOUBLE_EQ(nothing_to_buy.supplement_purchased_kg_dm, 0.0);

  EXPECT_DOUBLE_EQ(refuses.supplement_requested_kg_dm, 0.0)
      << "a farmer who will not buy never asks in the first place";
}

}  // namespace
}  // namespace paddock::config
