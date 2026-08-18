// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// A farmer who decides rather than follows.
//
// The calendar-driven farmer could only do what it was told, so a year that did
// not suit the calendar ended with starved stock or a grazed-out sward and no
// way to say which mattered. This one is given two things it must not allow -
// the sward taken below a cover, the stock left hungry - and buys feed when the
// pasture cannot deliver both.
//
// That changes what a comparison between grazing systems even means. It stops
// being "which system leaves the stock heavier", which a farm short of feed
// answers by starving them, and becomes "which system needs less bought feed to
// get the same stock to the same weight". That is the question a farm accountant
// asks, and the one this model can now answer.
//
// **In physical units only.** Bought feed costs money and stock are sold by the
// kilogram, but this project has no sourced price for either, so the report is
// kilograms. Prices convert them later without changing anything here.

#include <gtest/gtest.h>

#include <string>

#include <paddock/config/ScenarioRun.hpp>

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

core::ManagementPolicy default_policy() {
  core::ManagementPolicy policy;
  policy.minimum_cover_kg_dm_per_ha = 1600.0;
  policy.target_liveweight_gain_kg_per_day = 0.0;
  policy.maximum_graze_days = 3;
  policy.minimum_spell_days = 35;
  policy.rotation_cover_threshold_kg_dm_per_ha = 2200.0;
  policy.supplement_me_mj_per_kg_dm = 10.0;
  policy.may_buy_feed = true;
  return policy;
}

RunSummary run(int head, const core::ManagementPolicy& policy, std::string label) {
  ScenarioBundle bundle = load_scenario(bundle_path());
  bundle.mobs.front().head = head;
  return run_managed_scenario(bundle, policy, pasture_diet(), std::move(label));
}

// The two things the farmer is not allowed to let happen. At a stocking rate the
// farm cannot carry unaided, both still hold - because the farmer buys the
// difference rather than choosing between them.
TEST(ManagedFarmTest, TheStockAreNotStarvedAndTheSwardIsNotGrazedOut) {
  const RunSummary year = run(1400, default_policy(), "managed");

  EXPECT_GT(year.bought_feed_kg_dm(), 0.0)
      << "at this stocking rate the farm cannot carry the mob unaided, so a farmer who "
         "bought nothing was not managing";

  // Stock held, near enough. They are not being grown here - the policy asks for
  // zero gain - so what is checked is that they did not waste away.
  EXPECT_GT(year.closing_liveweight_kg(), year.opening_liveweight_kg() - 2.0)
      << "opened at " << year.opening_liveweight_kg() << " kg, closed at "
      << year.closing_liveweight_kg();

  // And the sward was not taken out from under them.
  EXPECT_GT(year.lowest_cover_kg_dm_per_ha(), 1000.0)
      << "cover reached " << year.lowest_cover_kg_dm_per_ha() << " kg DM/ha";

  GTEST_LOG_(INFO) << "bought " << year.bought_feed_kg_dm() << " kg DM on "
                   << year.days_feed_was_bought() << " days; liveweight "
                   << year.opening_liveweight_kg() << " to " << year.closing_liveweight_kg()
                   << " kg; cover low " << year.lowest_cover_kg_dm_per_ha();
}

// A farm that can carry its stock buys nothing. If the farmer bought feed for a
// farm with grass to spare, the rule would be firing on something other than
// need.
TEST(ManagedFarmTest, AFarmWithFeedToSpareBuysNothing) {
  const RunSummary year = run(300, default_policy(), "lightly stocked");

  EXPECT_DOUBLE_EQ(year.bought_feed_kg_dm(), 0.0)
      << "bought " << year.bought_feed_kg_dm() << " kg DM on a farm that did not need it";
  EXPECT_EQ(year.days_short, 0);
}

// The comparison the farmer makes possible: heavier stocking costs bought feed
// rather than costing condition. That is what a real farm does, and it is the
// number a farm accountant would want.
TEST(ManagedFarmTest, HeavierStockingIsPaidForInBoughtFeedRatherThanInCondition) {
  const RunSummary light = run(600, default_policy(), "600 head");
  const RunSummary heavy = run(1400, default_policy(), "1400 head");

  EXPECT_GT(heavy.bought_feed_kg_dm(), light.bought_feed_kg_dm())
      << "more stock on the same farm should need more bought feed";

  // Both mobs end up in much the same condition, because that is what the
  // farmer was protecting. The cost went into the feed bill instead.
  EXPECT_NEAR(heavy.liveweight_change_kg(), light.liveweight_change_kg(), 3.0)
      << "light " << light.liveweight_change_kg() << " kg, heavy " << heavy.liveweight_change_kg()
      << " kg";

  GTEST_LOG_(INFO) << "600 head bought " << light.bought_feed_kg_dm() << " kg DM; 1400 head bought "
                   << heavy.bought_feed_kg_dm() << " kg DM";
}

// A farmer told not to buy feed cannot protect both things at once, and the
// stock are what gives. Worth testing because it is the counterfactual the
// bought-feed figure is measured against.
TEST(ManagedFarmTest, WithoutTheOptionToBuyTheStockLoseCondition) {
  core::ManagementPolicy no_buying = default_policy();
  no_buying.may_buy_feed = false;

  const RunSummary bought = run(1400, default_policy(), "may buy");
  const RunSummary went_without = run(1400, no_buying, "may not buy");

  EXPECT_DOUBLE_EQ(went_without.bought_feed_kg_dm(), 0.0);
  EXPECT_LT(went_without.closing_liveweight_kg(), bought.closing_liveweight_kg())
      << "a farm that could not buy feed should end with lighter stock";

  GTEST_LOG_(INFO) << "closing liveweight: buying " << bought.closing_liveweight_kg()
                   << " kg, not buying " << went_without.closing_liveweight_kg() << " kg";
}

// Bought feed comes from off the farm, so it has to enter the budget as an
// inflow. If it appeared from nowhere the dry matter budget would not close, and
// a farm could be fed on nothing.
TEST(ManagedFarmTest, BoughtFeedEntersTheBudgetRatherThanAppearingFromNowhere) {
  const RunSummary year = run(1400, default_policy(), "managed");
  constexpr double kTolerance = 1e-9;

  ASSERT_GT(year.bought_feed_kg_dm(), 0.0);

  EXPECT_TRUE(year.ledger.closes(core::Budget::DryMatter, year.closing_cover_kg_dm, kTolerance))
      << year.ledger.report(core::Budget::DryMatter, year.closing_cover_kg_dm);
  EXPECT_TRUE(year.ledger.closes(core::Budget::Nitrogen, year.closing_nitrogen_kg, kTolerance));
  EXPECT_TRUE(year.ledger.closes(core::Budget::Water, year.closing_water_mm, kTolerance));
}

// Every purchase carries its date, its mob and its reason, because a report has
// to say when the farm needed feed and not only how much.
TEST(ManagedFarmTest, EveryPurchaseSaysWhenAndWhyItWasMade) {
  const RunSummary year = run(1400, default_policy(), "managed");
  ASSERT_FALSE(year.purchases.empty());

  for (const core::FeedPurchase& purchase : year.purchases) {
    EXPECT_TRUE(purchase.date.is_valid());
    EXPECT_GT(purchase.kg_dm, 0.0);
    EXPECT_FALSE(purchase.mob_name.empty());
    EXPECT_FALSE(core::to_string(purchase.reason).empty());
  }

  EXPECT_LE(year.days_feed_was_bought(), static_cast<int>(year.dates.size()));
  EXPECT_GT(year.days_feed_was_bought(), 0);
}

}  // namespace
}  // namespace paddock::config
