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

#include <algorithm>
#include <stdexcept>
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

/// A stocking rate this farm carries without strain, so that what the target
/// gain does is not hidden behind a mob that is short of feed whatever it is
/// asked for. 300 head is the rate the lightly stocked case below uses.
constexpr int kComfortableHead = 300;

RunSummary run(int head, const core::ManagementPolicy& policy, std::string label) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.mobs.front().head = head;
  return run_managed_scenario(bundle, policy, pasture_diet(), std::move(label));
}

// The claim [management] makes: a bundle that names its farmer's rules runs the
// same way as one handed those rules by its caller.
//
// If these two diverged, the section would be decoration - a manifest that says
// what the farm was run under while the farm was run under something else.
TEST(ManagedFarmTest, ABundlesOwnRulesGiveTheSameRunAsThoseRulesPassedIn) {
  const ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  // if-and-FAIL rather than ASSERT_TRUE, so the assertion is visible to more
  // than a person: clang-tidy's optional analysis cannot see through a gtest
  // macro, and it disagrees across platforms about whether it can.
  if (!bundle.management.has_value()) {
    FAIL() << "this test is about the bundle carrying a policy, and it carries none";
  }

  const RunSummary from_bundle = run_managed_scenario(bundle, pasture_diet(), "from the bundle");
  const RunSummary passed_in =
      run_managed_scenario(bundle, *bundle.management, pasture_diet(), "passed in");

  EXPECT_EQ(from_bundle.cover_kg_dm_per_ha, passed_in.cover_kg_dm_per_ha);
  EXPECT_EQ(from_bundle.liveweight_kg, passed_in.liveweight_kg);
  EXPECT_DOUBLE_EQ(from_bundle.bought_feed_kg_dm(), passed_in.bought_feed_kg_dm());
  EXPECT_EQ(from_bundle.days_short, passed_in.days_short);
  EXPECT_EQ(from_bundle.moves, passed_in.moves);
}

// And a bundle that names none says so rather than inventing one. Inventing the
// rules a farm was run under is the thing this section exists to stop.
TEST(ManagedFarmTest, ABundleWithNoRulesRefusesToBeRunByAFarmer) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.management.reset();

  EXPECT_THROW(static_cast<void>(run_managed_scenario(bundle, pasture_diet(), "no policy")),
               std::runtime_error);
}

namespace {

int days_rotating(const RunSummary& run) {
  return static_cast<int>(std::count(run.system_each_day.begin(), run.system_each_day.end(),
                                     core::GrazingSystem::Rotational));
}

RunSummary run_preferring(core::GrazingPreference preference, std::string label) {
  core::ManagementPolicy policy = default_policy();
  policy.preference = preference;
  return run(1400, policy, std::move(label));
}

}  // namespace

// A farmer told never to rotate never does, whatever the cover says.
//
// Smith and Dawson (1976) name set stocking for lambing, when demand is highest
// and the whole farm is wanted at once. A farm may be run that way all year,
// and the model should be able to show what it costs rather than quietly
// deciding otherwise.
TEST(ManagedFarmTest, AFarmerWhoNeverRotatesNeverDoes) {
  const RunSummary year =
      run_preferring(core::GrazingPreference::AlwaysSetStock, "always set stocked");

  EXPECT_EQ(days_rotating(year), 0);
  EXPECT_EQ(year.moves, 0) << "set stocking gives the mob the whole farm, so there is nothing to "
                              "move it between";
}

// And one who prefers to rotate does it on days the cover rule would not have.
TEST(ManagedFarmTest, AFarmerWhoPrefersRotationRotatesMoreThanTheCoverRuleWould) {
  const RunSummary by_cover = run_preferring(core::GrazingPreference::ByCover, "by cover");
  const RunSummary preferring =
      run_preferring(core::GrazingPreference::PreferRotation, "prefer rotation");

  EXPECT_GT(days_rotating(preferring), days_rotating(by_cover));

  GTEST_LOG_(INFO) << "days rotating: by cover " << days_rotating(by_cover) << ", preferring "
                   << days_rotating(preferring) << " of " << by_cover.dates.size();
}

// The scenario's own calendar, which a managed run used to ignore completely.
//
// canterbury-grazed sets stock from 20 August to 28 October for lambing and
// rotates the rest of the year. A farmer told to follow it should show exactly
// that shape, and one deciding from cover should not.
TEST(ManagedFarmTest, AFarmerCanFollowTheScenariosOwnCalendar) {
  const RunSummary following =
      run_preferring(core::GrazingPreference::FollowCalendar, "following the calendar");
  ASSERT_EQ(following.dates.size(), following.system_each_day.size());

  const core::Date lambing{2023, 9, 15};
  const core::Date summer{2024, 2, 15};
  core::GrazingSystem on_lambing = core::GrazingSystem::Rotational;
  core::GrazingSystem in_summer = core::GrazingSystem::SetStocking;
  for (std::size_t day = 0; day < following.dates.size(); ++day) {
    if (following.dates[day] == lambing) {
      on_lambing = following.system_each_day[day];
    }
    if (following.dates[day] == summer) {
      in_summer = following.system_each_day[day];
    }
  }

  EXPECT_EQ(on_lambing, core::GrazingSystem::SetStocking)
      << "the calendar sets stock over lambing and the farmer did not";
  EXPECT_EQ(in_summer, core::GrazingSystem::Rotational)
      << "the calendar rotates through the dry season and the farmer did not";
}

// At the floor, what the farmer buys.
//
// Buying the whole demand asks the pasture for nothing, so the sward is left to
// grow back. Grazing down to the line buys only the difference, so cover sits
// at the floor instead of climbing away from it. Less feed, less grass: the
// trade a farmer actually makes.
TEST(ManagedFarmTest, GrazingDownToTheFloorBuysLessFeedAndLeavesLessGrass) {
  core::ManagementPolicy whole = default_policy();
  whole.floor_purchase = core::FloorPurchase::WholeDemand;
  core::ManagementPolicy holding = default_policy();
  holding.floor_purchase = core::FloorPurchase::HoldAtFloor;

  const RunSummary bought_the_lot = run(1400, whole, "whole demand");
  const RunSummary grazed_to_the_line = run(1400, holding, "hold at the floor");

  EXPECT_LT(grazed_to_the_line.bought_feed_kg_dm(), bought_the_lot.bought_feed_kg_dm());
  EXPECT_LT(grazed_to_the_line.mean_cover_kg_dm_per_ha(), bought_the_lot.mean_cover_kg_dm_per_ha());

  // Neither may take the sward through the floor. That is the whole point of
  // the floor, and it is the assertion that would catch a rule that grazed too
  // far in the name of buying less.
  EXPECT_GT(grazed_to_the_line.lowest_cover_kg_dm_per_ha(), 1000.0)
      << "cover reached " << grazed_to_the_line.lowest_cover_kg_dm_per_ha() << " kg DM/ha";

  GTEST_LOG_(INFO) << "bought: whole demand " << bought_the_lot.bought_feed_kg_dm()
                   << " kg DM, holding at the floor " << grazed_to_the_line.bought_feed_kg_dm()
                   << " kg DM; mean cover " << bought_the_lot.mean_cover_kg_dm_per_ha() << " and "
                   << grazed_to_the_line.mean_cover_kg_dm_per_ha() << " kg DM/ha";
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

// The farmer's target gain has to reach the animals.
//
// It used to reach only the decision about how much feed to BUY, and on a farm
// with grass to spare that is no decision at all: nothing was bought, the mob
// ate to maintenance, and a year later every ewe stood on exactly her opening
// weight with the target sitting in the panel doing nothing. The failure was
// silent and looked like a model that simply held stock steady.
TEST(ManagedFarmTest, AskingForGainGetsGainWhenTheGrassAllowsIt) {
  core::ManagementPolicy holding = default_policy();
  holding.target_liveweight_gain_kg_per_day = 0.0;

  core::ManagementPolicy growing = default_policy();
  growing.target_liveweight_gain_kg_per_day = 0.1;

  const RunSummary held = run(kComfortableHead, holding, "holding");
  const RunSummary grown = run(kComfortableHead, growing, "growing");

  EXPECT_NEAR(held.liveweight_change_kg(), 0.0, 1.0)
      << "asked to hold weight, the mob should end the year near where it started";
  EXPECT_GT(grown.liveweight_change_kg(), held.liveweight_change_kg() + 5.0)
      << "held " << held.liveweight_change_kg() << " kg, grown " << grown.liveweight_change_kg()
      << " kg";

  GTEST_LOG_(INFO) << "liveweight change over the year: holding " << held.liveweight_change_kg()
                   << " kg, target 0.1 kg/day " << grown.liveweight_change_kg() << " kg";
}

// And that it is paid for. Feeding for gain takes more grass off the farm, so
// something has to give: less cover, more bought feed, or both. A run that
// produced gain out of the same feed would be gain from nowhere.
TEST(ManagedFarmTest, GainIsPaidForInGrassOrInBoughtFeed) {
  const core::ManagementPolicy holding = default_policy();
  core::ManagementPolicy growing = default_policy();
  growing.target_liveweight_gain_kg_per_day = 0.1;

  const RunSummary held = run(kComfortableHead, holding, "holding");
  const RunSummary grown = run(kComfortableHead, growing, "growing");

  EXPECT_GT(grown.eaten_kg_dm, held.eaten_kg_dm)
      << "a mob fed for gain has to eat more: held " << held.eaten_kg_dm << " kg DM, grown "
      << grown.eaten_kg_dm << " kg DM";

  const bool cover_fell = grown.mean_cover_kg_dm_per_ha() < held.mean_cover_kg_dm_per_ha();
  const bool bought_more = grown.bought_feed_kg_dm() > held.bought_feed_kg_dm();
  EXPECT_TRUE(cover_fell || bought_more)
      << "the extra feed came from nowhere: cover " << held.mean_cover_kg_dm_per_ha() << " to "
      << grown.mean_cover_kg_dm_per_ha() << " kg DM/ha, bought " << held.bought_feed_kg_dm()
      << " to " << grown.bought_feed_kg_dm() << " kg DM";
}

}  // namespace paddock::config
