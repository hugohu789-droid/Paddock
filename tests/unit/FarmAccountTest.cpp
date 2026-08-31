// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// The farm's cash, against the figures it is built from.
///
/// The operating costs here are Beef + Lamb New Zealand's Class 6
/// Marlborough-Canterbury means for 2024-25, the same numbers
/// `data/economics/canterbury-sheep.toml` carries. A test that used round
/// numbers would prove the arithmetic and nothing about whether a Canterbury
/// sheep farm can pay its bills.

#include <gtest/gtest.h>

#include <paddock/core/FarmAccount.hpp>
#include <paddock/core/SimulationClock.hpp>

namespace paddock::core {
namespace {

/// B+LNZ Class 6 S.I. Finishing Breeding, Marlborough-Canterbury, 2024-25 mean.
OperatingCosts canterbury_sheep() {
  OperatingCosts costs;
  costs.wages_and_salaries = 87.31;
  costs.animal_health = 55.04;
  costs.weed_and_pest = 51.64;
  costs.shearing = 46.10;
  costs.fertiliser = 153.62;
  costs.lime = 5.51;
  costs.seeds = 38.21;
  costs.vehicles_and_fuel = 78.84;
  costs.electricity = 9.58;
  costs.feed_and_grazing = 68.36;
  costs.dog_expenses = 9.23;
  costs.cultivation_and_sowing = 39.65;
  costs.cash_crop = 2.30;
  costs.repairs_and_maintenance = 73.30;
  costs.irrigation_charges = 61.50;
  costs.cartage = 18.66;
  costs.administration = 45.42;
  costs.insurance_and_acc = 38.58;
  costs.rates = 45.01;
  costs.interest = 219.18;
  costs.rent = 26.85;
  costs.depreciation = 106.52;
  return costs;
}

Prices canterbury_prices() {
  Prices prices;
  prices.lamb_dollars_per_kg_carcass = 7.80;
  prices.wool_dollars_per_kg = 3.80;
  prices.cull_ewe_dollars_per_head = 90.0;
  return prices;
}

constexpr double kHectares = 80.0;

TEST(FarmAccountTest, TheShippedCostsAreUsable) {
  EXPECT_EQ(canterbury_sheep().invalid_reason(), "");
  EXPECT_EQ(canterbury_prices().invalid_reason(), "");
}

// **What a Canterbury sheep farm actually spends**, from the survey's own
// lines. Pinned so that a change to any line has to be deliberate.
TEST(FarmAccountTest, TheOperatingCostComesToWhatTheSurveySays) {
  const OperatingCosts costs = canterbury_sheep();

  // Everything but interest, rent and depreciation - the EBITRm basis B+LNZ
  // rank farms on, and the default this model charges.
  EXPECT_NEAR(costs.annual_per_hectare(), 927.86, 0.01);

  // With a mortgage, rent and depreciation on top, which a scenario has to ask
  // for. Interest alone is nearly a quarter of it.
  OperatingCosts geared = costs;
  geared.charge_interest = true;
  geared.charge_rent = true;
  geared.charge_depreciation = true;
  EXPECT_NEAR(geared.annual_per_hectare(), 1280.41, 0.01);
}

// The point of the whole account: a year costs money, and the model can say how
// much.
TEST(FarmAccountTest, AYearOfOperatingCostLeavesTheBalanceWhereArithmeticSays) {
  FarmAccount account(400.0 * kHectares, canterbury_sheep(), canterbury_prices(), kHectares);
  EXPECT_DOUBLE_EQ(account.balance(), 32'000.0);

  const Date opening{2023, 7, 1};
  for (int i = 0; i < 365; ++i) {
    account.charge_day(Date::from_days_since_epoch(opening.days_since_epoch() + i));
  }

  // 927.86 $/ha over 80 ha is $74,229 a year.
  EXPECT_NEAR(account.total_for(LedgerReason::OperatingCost), -74'228.8, 1.0);
  EXPECT_NEAR(account.balance(), 32'000.0 - 74'228.8, 1.0);

  // **And that is the finding, not a failure of the test.** Six months of
  // operating cash does not carry a farm through a year on costs alone: this
  // farm is insolvent by about day 158 unless something is sold. Revenue is
  // what the rest of the model has to supply, and until it does, an account
  // like this one can only say what a year costs.
  EXPECT_TRUE(account.is_insolvent());
}

TEST(FarmAccountTest, SellingStockAndWoolIsWhatKeepsItSolvent) {
  FarmAccount account(400.0 * kHectares, canterbury_sheep(), canterbury_prices(), kHectares);

  const Date opening{2023, 7, 1};
  for (int i = 0; i < 365; ++i) {
    const Date day = Date::from_days_since_epoch(opening.days_since_epoch() + i);
    account.charge_day(day);

    // One shearing and one draft of lambs, at the prices in the file. 417 ewes
    // at 5 kg of wool, and 380 lambs at 18 kg carcass.
    if (day.month == 11 && day.day == 1) {
      account.record(day, LedgerReason::SoldWool, 417.0 * 5.0 * 3.80, "shorn the ewes");
    }
    if (day.month == 2 && day.day == 1) {
      account.record(day, LedgerReason::SoldStock, 380.0 * 18.0 * 7.80, "drafted the lambs");
    }
  }

  EXPECT_GT(account.total_for(LedgerReason::SoldStock), 0.0);
  EXPECT_GT(account.total_for(LedgerReason::SoldWool), 0.0);
  EXPECT_FALSE(account.is_insolvent())
      << "a year's wool and one draft of lambs should cover a year's costs; balance "
      << account.balance();
}

// A farm that cannot pay its bills should be able to say which bills, and when
// it came closest.
TEST(FarmAccountTest, TheAccountRemembersWhereTheMoneyWentAndWhenItWasLowest) {
  FarmAccount account(10'000.0, canterbury_sheep(), canterbury_prices(), kHectares);

  const Date first{2023, 7, 1};
  account.charge_day(first);
  account.record(Date{2023, 7, 2}, LedgerReason::BoughtFeed, -2'500.0, "baleage, 10 t");
  account.record(Date{2023, 7, 3}, LedgerReason::SoldStock, 4'000.0, "cull ewes");

  EXPECT_NEAR(account.total_for(LedgerReason::BoughtFeed), -2'500.0, 1e-9);
  EXPECT_NEAR(account.total_for(LedgerReason::SoldStock), 4'000.0, 1e-9);
  EXPECT_EQ(account.entries().size(), 3U);
  EXPECT_EQ(account.entries().front().reason, LedgerReason::OperatingCost);

  // Lowest was after the feed went out and before the stock came in.
  EXPECT_EQ(account.lowest_on().to_iso_string(), "2023-07-02");
  EXPECT_LT(account.lowest_balance(), account.opening_balance());
}

TEST(FarmAccountTest, NegativeCostsAndPricesAreRefused) {
  OperatingCosts costs = canterbury_sheep();
  costs.fertiliser = -1.0;
  EXPECT_NE(costs.invalid_reason(), "");

  Prices prices = canterbury_prices();
  prices.wool_dollars_per_kg = -1.0;
  EXPECT_NE(prices.invalid_reason(), "");

  EXPECT_NE(OperatingCosts{}.invalid_reason(), "")
      << "a farm with no operating cost at all is not something this model can speak about";
}

}  // namespace
}  // namespace paddock::core
