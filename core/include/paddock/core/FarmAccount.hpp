// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <paddock/core/SimulationClock.hpp>

/// A farm's cash, day by day.
///
/// **This is the thing the rest of the model has been missing.** Until now a
/// farm could grow grass, feed stock and buy supplement, and none of it cost
/// anything - so every question that begins "is it worth" had no answer, which
/// is the limitation this project's own README names first. Money is what makes
/// destocking a decision rather than an arbitrary rule: a farmer sells stock
/// because feeding them costs more than they are worth, not because a threshold
/// said so.
///
/// **What it is not.** It is a cash account, not a balance sheet. Land, plant
/// and stock on hand are not in it. Interest, rent and depreciation are carried
/// as expenditure lines and charged only when a scenario says the farm has
/// them, because Beef + Lamb New Zealand rank farms on a measure defined before
/// all three - and charging a mortgage to a farm nobody said was mortgaged
/// would be inventing a balance sheet.
namespace paddock::core {

/// One line of operating cost, dollars per hectare per year.
///
/// Named rather than summed, because a farm that cannot pay its bills should be
/// able to say which bills. The names are Beef + Lamb New Zealand's own.
struct OperatingCosts {
  double wages_and_salaries = 0.0;
  double animal_health = 0.0;
  double weed_and_pest = 0.0;
  double shearing = 0.0;
  double fertiliser = 0.0;
  double lime = 0.0;
  double seeds = 0.0;
  double vehicles_and_fuel = 0.0;
  double electricity = 0.0;
  double feed_and_grazing = 0.0;
  double dog_expenses = 0.0;
  double cultivation_and_sowing = 0.0;
  double cash_crop = 0.0;
  double repairs_and_maintenance = 0.0;
  double irrigation_charges = 0.0;
  double cartage = 0.0;
  double administration = 0.0;
  double insurance_and_acc = 0.0;
  double rates = 0.0;

  /// Charged only when the scenario says the farm carries them.
  double interest = 0.0;
  double rent = 0.0;
  double depreciation = 0.0;

  bool charge_interest = false;
  bool charge_rent = false;
  bool charge_depreciation = false;

  /// Everything the farm pays in a year, dollars per hectare.
  [[nodiscard]] double annual_per_hectare() const noexcept;

  /// Empty when the costs are usable; otherwise what is wrong.
  [[nodiscard]] std::string invalid_reason() const;
};

/// What stock and fibre are worth.
struct Prices {
  double lamb_dollars_per_kg_carcass = 0.0;
  double wool_dollars_per_kg = 0.0;
  double cull_ewe_dollars_per_head = 0.0;

  /// What a kilogram of bought feed costs, dry matter.
  ///
  /// **The only thing standing between this farm and unlimited feed**, and it
  /// used to be a constant in FarmDecision.cpp with a comment claiming it came
  /// from the economics file. It did not.
  ///
  /// Beef + Lamb New Zealand's Fact Sheet 260 puts baleage at 54.1 cents a
  /// kilogram of dry matter - and, on the line above it in the same table,
  /// pasture at 10. A farm buying its way through a deficit pays five times
  /// what the same feed costs it standing in the paddock.
  ///
  /// Zero means no price was given, and the farm cannot buy: a purchase at an
  /// unstated price is a purchase whose cost nobody can check.
  double supplement_dollars_per_kg_dm = 0.0;

  [[nodiscard]] std::string invalid_reason() const;
};

/// Why money moved. Kept with every entry, because a farm account that cannot
/// say what a payment was for is a number rather than a record.
enum class LedgerReason : std::uint8_t {
  OperatingCost,
  BoughtFeed,
  SoldStock,
  SoldWool,
};

[[nodiscard]] std::string to_string(LedgerReason reason);

struct CashEntry {
  Date date;
  LedgerReason reason = LedgerReason::OperatingCost;

  /// Negative for money out, positive for money in.
  double dollars = 0.0;
  std::string detail;
};

/// The account itself.
class FarmAccount {
 public:
  FarmAccount(double opening_balance_dollars, OperatingCosts costs, Prices prices, double hectares);

  /// Charges one day of operating cost.
  ///
  /// **Evenly across the year, because a survey mean carries no seasonality.**
  /// Real fertiliser and shearing bills arrive in lumps; B+LNZ publish an
  /// annual figure and nothing about when it is paid, so spreading it is the
  /// only thing that does not invent a cash-flow pattern. It makes the account
  /// smoother than a real one, and a farm close to the edge in this model would
  /// be closer to it in life rather than further.
  void charge_day(const Date& date);

  void record(const Date& date, LedgerReason reason, double dollars, std::string detail);

  [[nodiscard]] double balance() const noexcept { return balance_; }

  [[nodiscard]] double opening_balance() const noexcept { return opening_balance_; }

  [[nodiscard]] const std::vector<CashEntry>& entries() const noexcept { return entries_; }

  [[nodiscard]] const Prices& prices() const noexcept { return prices_; }

  /// **The farm is insolvent, in the only sense this model can mean.** It has
  /// run out of cash. It may still own land, plant and stock worth far more
  /// than it owes - this account holds none of that - so what this reports is a
  /// farm that cannot pay this week's bills, not a farm that is worthless.
  [[nodiscard]] bool is_insolvent() const noexcept { return balance_ < 0.0; }

  /// The lowest the balance ever went, and when. What a report quotes when it
  /// says how close a year came.
  [[nodiscard]] double lowest_balance() const noexcept { return lowest_; }

  [[nodiscard]] const Date& lowest_on() const noexcept { return lowest_on_; }

  /// Totals by reason, for a report that has to say where the money went.
  [[nodiscard]] double total_for(LedgerReason reason) const;

 private:
  double opening_balance_ = 0.0;
  double balance_ = 0.0;
  double hectares_ = 0.0;
  double daily_operating_cost_ = 0.0;
  double lowest_ = 0.0;
  Date lowest_on_{};

  OperatingCosts costs_;
  Prices prices_;
  std::vector<CashEntry> entries_;
};

}  // namespace paddock::core
