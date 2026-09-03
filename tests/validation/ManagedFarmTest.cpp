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
#include <cstdlib>
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
  // **The floor moved because the farm did** (verify.md, E80). This asserted
  // 1,000 kg DM/ha when the bundle ran a synthetic generator drawing 700-930 mm
  // and a sward with a placeholder radiation use efficiency of 1.5. On real
  // Selwyn weather and the sourced sward the year bottoms near 740, and that low
  // is not the stock eating it out: it is 769 at 400 head and 733 at 1,200, a
  // 36 kg spread across a threefold stocking range. The winter trough is
  // senescence, not grazing.
  EXPECT_GT(grazed_to_the_line.lowest_cover_kg_dm_per_ha(), 600.0)
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
  // **The floor moved because the farm did** (verify.md, E80). This asserted
  // 1,000 kg DM/ha when the bundle ran a synthetic generator drawing 700-930 mm
  // and a sward with a placeholder radiation use efficiency of 1.5. On real
  // Selwyn weather and the sourced sward the year bottoms near 740, and that low
  // is not the stock eating it out: it is 769 at 400 head and 733 at 1,200, a
  // 36 kg spread across a threefold stocking range. The winter trough is
  // senescence, not grazing.
  EXPECT_GT(year.lowest_cover_kg_dm_per_ha(), 600.0)
      << "cover reached " << year.lowest_cover_kg_dm_per_ha() << " kg DM/ha";

  GTEST_LOG_(INFO) << "bought " << year.bought_feed_kg_dm() << " kg DM on "
                   << year.days_feed_was_bought() << " days; liveweight "
                   << year.opening_liveweight_kg() << " to " << year.closing_liveweight_kg()
                   << " kg; cover low " << year.lowest_cover_kg_dm_per_ha();
}

// **The buy rule fires on need, and the evidence is that it scales with need.**
//
// This asked for exactly zero purchases at 300 head, which the farm managed
// when it ran a synthetic generator drawing 700-930 mm and a sward with a
// placeholder radiation use efficiency of 1.5. On real Selwyn weather and the
// sourced sward, zero is not reachable at any stocking rate: a mob confined to
// one paddock under a three-day graze and a 35-day spell will meet a day the
// paddock is short of it, and the farmer tops it up (verify.md, E80).
//
// What can still be checked - and is the thing the test was ever about - is
// that the rule is answering need rather than firing on its own. It buys 31 kg
// DM a head at 100 and 61 at 300: three times the stock, six times the feed.
// A rule firing on something other than need would not bend like that.
TEST(ManagedFarmTest, TheBuyRuleAnswersNeedAndNothingElse) {
  const RunSummary light = run(100, default_policy(), "lightly stocked");
  const RunSummary heavier = run(300, default_policy(), "three times the stock");

  // 90 kg a head with E83, where this said 40. Turnover on its own temperature
  // response turns leaf over faster in a Canterbury summer than the leaf
  // lifespan it replaced, so there is less standing and the farmer tops up more.
  EXPECT_LT(light.bought_feed_kg_dm() / 100.0, 90.0)
      << "a lightly stocked farm bought " << (light.bought_feed_kg_dm() / 100.0)
      << " kg DM a head, which is not a top-up";
  EXPECT_GT(heavier.bought_feed_kg_dm(), light.bought_feed_kg_dm() * 2.0)
      << "three times the stock should need more than twice the feed, not the same";

  // And whatever it bought, it was enough: nobody went short.
  EXPECT_EQ(light.days_short, 0);
  EXPECT_EQ(heavier.days_short, 0);
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
// **The target is still an instruction and not an aspiration, and this holds
// the gap** (verify.md, E77).
//
// A farmer can write any number in the panel, and what a mob actually puts on
// should be bounded by what it can eat. Grazing is now bounded that way - E75
// put GrazPlan's appetite and availability into the paddock. **Bought feed is
// not.** Feed carried out to a trough is capped at what the mob is being fed
// *for*, so a farmer who asks for half a kilogram a day simply buys his way
// there: no ewe gains 182 kg in a year, and this one does, to the decimal.
//
// It is the same hole as E55, seen from the animal's end rather than the
// accountant's: bought feed is the one intake in this model that nothing
// bounds, which is why profit rises with every extra ewe.
//
// **Capping the trough at the animal's appetite was tried and is not here.**
// It works - the year comes back at 163 kg rather than 182 - but a lactating
// ewe's appetite runs only about 8% above her requirement, and the
// availability term takes 10% at ordinary cover, so she runs a small permanent
// deficit, loses 3.6 kg over a year where she used to hold, and the farmer
// destocks into it. Closing this wants the lactation factor's LA and LB terms,
// which need a body-condition history this model does not keep.
TEST(ManagedFarmTest, AnImpossibleTargetIsBoughtRatherThanRefused) {
  core::ManagementPolicy asking = default_policy();
  asking.target_liveweight_gain_kg_per_day = 0.5;

  const RunSummary got = run(kComfortableHead, asking, "asking");

  GTEST_LOG_(INFO) << "asked for 0.5 kg a day over a year - 182 kg - and got "
                   << got.liveweight_change_kg() << " kg";

  // Held from both sides. Under the upper bound means somebody has bounded the
  // target and should say so here; over it means something has come loose.
  EXPECT_GT(got.liveweight_change_kg(), 150.0)
      << "the flock returned " << got.liveweight_change_kg()
      << " kg. If this has fallen, the trough has been capped - raise the bound and record it";
  EXPECT_LT(got.liveweight_change_kg(), 200.0);
}

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

// **Three floors, because Beef + Lamb New Zealand give three.** A ewe in
// mid-pregnancy can be held to 800 kg DM/ha and a ewe in milk cannot be held
// below 1,200, and a model with one number either starves the second or buys
// feed to defend a cover the first does not need. The single 1,600 this
// replaced was neither: it was a dairy residual on a sheep farm, and
// canterbury-grazed still says where it came from - "the only reason to prefer
// them to any other round number: they are the ones the loop is known to settle
// under".
TEST(ManagedFarmTest, TheCoverFloorFollowsTheEwesYear) {
  const core::ManagementPolicy policy;

  // Through lactation: the binding one. "Individual paddocks should be
  // set-stocked so covers never go below 1200 kg DM/ha during lactation."
  EXPECT_DOUBLE_EQ(policy.minimum_cover_on(core::Date{2024, 9, 15}), 1200.0);
  EXPECT_DOUBLE_EQ(policy.minimum_cover_on(core::Date{2024, 11, 20}), 1200.0);

  // After weaning and through mid-pregnancy a ewe can be held far lower:
  // "post-grazing covers of around 800 kg DM/ha (a sward height of 2cm)".
  EXPECT_DOUBLE_EQ(policy.minimum_cover_on(core::Date{2024, 12, 15}), 800.0);
  EXPECT_DOUBLE_EQ(policy.minimum_cover_on(core::Date{2025, 5, 1}), 800.0);
  EXPECT_DOUBLE_EQ(policy.minimum_cover_on(core::Date{2025, 7, 20}), 800.0);

  // And lifted three weeks out from lambing: "residuals should be lifted to
  // 1000-1100 kg DM/ha".
  EXPECT_DOUBLE_EQ(policy.minimum_cover_on(core::Date{2024, 8, 5}), 1000.0);

  // **The lift happens, and in the right direction.** A flat floor gives the
  // same answer on all four days, which is what this replaced.
  EXPECT_LT(policy.minimum_cover_on(core::Date{2024, 6, 1}),
            policy.minimum_cover_on(core::Date{2024, 8, 5}));
  EXPECT_LT(policy.minimum_cover_on(core::Date{2024, 8, 5}),
            policy.minimum_cover_on(core::Date{2024, 9, 15}));
}

// **A wet year makes silage and a dry year does not**, which is the whole point
// of a conservation policy and the reason it was worth building.
//
// Before this the farm's intake ran almost flat across the decade - 2,437,
// 2,432 and 2,425 kg DM/ha in the driest, an ordinary and the wettest year -
// while growth ran 5.3 to 9.4 tonnes. It could buy feed and it could not make
// any, so a wet spring grew grass that died where it stood.
TEST(ManagedFarmTest, AWetYearCutsASurplusAndADryYearHasNoneToCut) {
  const core::ConservationPolicy policy;

  // The dry year's mean cover peaks at about 1,850 kg DM/ha inside the cutting
  // window and never reaches the surplus this policy names; the wet year's
  // reaches it in December.
  EXPECT_GT(policy.surplus_cover_kg_dm_per_ha, 1'850.0)
      << "a trigger under this would cut a dry year's winter feed";
  EXPECT_LT(policy.surplus_cover_kg_dm_per_ha, 2'400.0)
      << "and one over it never fires on a sheep farm at all - 2,600 was tried, and did not";

  // **A cut leaves a grazeable sward**, not a lawn: what it takes the paddock
  // down to has to stand above the floors a ewe is held to.
  EXPECT_GT(policy.cut_to_cover_kg_dm_per_ha, core::ManagementPolicy{}.minimum_cover_kg_dm_per_ha)
      << "a cut that took the farm below its lactation floor would be mowing, not harvesting";
}

}  // namespace paddock::config
