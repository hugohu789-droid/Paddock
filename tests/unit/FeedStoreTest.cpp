// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// Feed cut in a good month and fed back in a bad one.
///
/// **The mechanism that turns a good year's growth into a bad year's feed.**
/// Without it this farm's intake ran almost flat across a decade - 2,437, 2,432
/// and 2,425 kg DM/ha in the driest, an ordinary and the wettest year - while
/// growth ran 5.3 to 9.4 tonnes, so utilisation read 46% down to 26% for the
/// same farm doing the same thing. A wet spring simply grew grass that died.

#include <gtest/gtest.h>

#include <paddock/core/FeedStore.hpp>

namespace paddock::core {
namespace {

TEST(FeedStoreTest, TheShippedLossesAndPolicyAreUsable) {
  EXPECT_EQ(ConservationLosses{}.invalid_reason(), "");
  EXPECT_EQ(ConservationPolicy{}.invalid_reason(), "");

  ConservationLosses everything_lost;
  everything_lost.cutting_loss_fraction = 1.0;
  EXPECT_NE(everything_lost.invalid_reason(), "") << "a loss of everything is not a loss";

  ConservationPolicy mowing;
  mowing.cut_to_cover_kg_dm_per_ha = mowing.surplus_cover_kg_dm_per_ha + 1.0;
  EXPECT_NE(mowing.invalid_reason(), "")
      << "a cut that leaves more than it started with is not a cut";
}

// **A stack is not a bank account.** Dry matter is lost on the way in and on the
// way out, so what a sheep eats is well under what the mower took.
TEST(FeedStoreTest, WhatReachesTheAnimalIsLessThanWhatWasCut) {
  const ConservationLosses losses;  // 10% cutting, 10% storage, 5% feeding out
  FeedStore store;

  const double into_stack = store.add(1'000.0, losses);
  EXPECT_NEAR(into_stack, 810.0, 1e-9) << "1,000 cut, 10% left in the paddock, 10% lost in the "
                                          "stack";
  EXPECT_NEAR(store.held_kg_dm(), 810.0, 1e-9);
  EXPECT_NEAR(store.cut_kg_dm(), 1'000.0, 1e-9);
  EXPECT_NEAR(store.lost_kg_dm(), 190.0, 1e-9);

  // Asked for what should reach the mob, not for what leaves the stack: 100 kg
  // in front of stock needs 105.3 out of the pit.
  const double eaten = store.take(100.0, losses);
  EXPECT_NEAR(eaten, 100.0, 1e-9);
  EXPECT_NEAR(store.held_kg_dm(), 810.0 - (100.0 / 0.95), 1e-9);
  EXPECT_NEAR(store.fed_kg_dm(), 100.0, 1e-9);

  // **The whole chain**, which is what a farmer is really deciding about: of a
  // kilogram standing in the paddock, 77% of it gets eaten.
  EXPECT_NEAR(losses.kept_fraction(), 0.9 * 0.9 * 0.95, 1e-12);
  EXPECT_NEAR(losses.kept_fraction(), 0.7695, 1e-4);
}

// A stack cannot hand out what it does not hold.
TEST(FeedStoreTest, AnEmptyStackFeedsNothing) {
  const ConservationLosses losses;
  FeedStore store;

  EXPECT_DOUBLE_EQ(store.take(500.0, losses), 0.0);
  EXPECT_DOUBLE_EQ(store.add(0.0, losses), 0.0);
  EXPECT_DOUBLE_EQ(store.add(-100.0, losses), 0.0) << "a negative cut is not a cut";

  store.add(100.0, losses);
  const double all_of_it = store.take(10'000.0, losses);
  EXPECT_GT(all_of_it, 0.0);
  EXPECT_LT(all_of_it, 100.0) << "and what comes out is less than what went in";
  EXPECT_NEAR(store.held_kg_dm(), 0.0, 1e-9);
}

// **Silage is a poorer feed than the grass it came from**, which is half the
// cost of conserving and the half a dry-matter figure alone would hide.
TEST(FeedStoreTest, SilageIsWorthLessThanThePastureItWasCutFrom) {
  const ConservationLosses losses;

  // NZGA measured New Zealand pasture silage at 9.3 to 9.6 MJ ME/kg DM against
  // pasture's 10.5, at 65.5 to 67.9% digestibility.
  EXPECT_GT(losses.silage_energy_mj_per_kg_dm, 9.3);
  EXPECT_LT(losses.silage_energy_mj_per_kg_dm, 9.6);
  EXPECT_LT(losses.silage_energy_mj_per_kg_dm, 10.5) << "conserving costs quality, not only bulk";
  EXPECT_GT(losses.silage_digestibility_percent, 65.5);
  EXPECT_LT(losses.silage_digestibility_percent, 67.9);
}

// **The window, and why it runs past the silage season.** NZGA put high-quality
// pasture silage at "harvest pasture by the first week in November"; a New
// Zealand farm does not stop conserving then, it starts making hay.
TEST(FeedStoreTest, TheCuttingWindowIsSpringAndSummerAndNotWinter) {
  const ConservationPolicy policy;

  EXPECT_TRUE(policy.may_cut_on(10, 15)) << "October, the silage season";
  EXPECT_TRUE(policy.may_cut_on(12, 20)) << "December, the hay season";
  EXPECT_TRUE(policy.may_cut_on(1, 20)) << "and January, which is where this farm's surplus is";

  EXPECT_FALSE(policy.may_cut_on(3, 1)) << "autumn: what is standing is next winter's feed";
  EXPECT_FALSE(policy.may_cut_on(7, 1)) << "and nobody cuts in July";

  // A farm that does not conserve is the same farm with one flag off, which is
  // what makes this a management choice rather than a code path.
  ConservationPolicy never;
  never.conserves = false;
  EXPECT_FALSE(never.may_cut_on(10, 15));
}

}  // namespace
}  // namespace paddock::core
