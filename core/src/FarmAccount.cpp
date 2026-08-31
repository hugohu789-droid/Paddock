// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <utility>

#include <paddock/core/FarmAccount.hpp>

namespace paddock::core {

namespace {

/// Days a year's costs are spread over. 365 rather than the run's own length,
/// so a leap year does not quietly cost 0.27% more than a common one.
constexpr double kDaysInYear = 365.0;

}  // namespace

double OperatingCosts::annual_per_hectare() const noexcept {
  double total = wages_and_salaries + animal_health + weed_and_pest + shearing + fertiliser + lime +
                 seeds + vehicles_and_fuel + electricity + feed_and_grazing + dog_expenses +
                 cultivation_and_sowing + cash_crop + repairs_and_maintenance + irrigation_charges +
                 cartage + administration + insurance_and_acc + rates;
  if (charge_interest) {
    total += interest;
  }
  if (charge_rent) {
    total += rent;
  }
  if (charge_depreciation) {
    total += depreciation;
  }
  return total;
}

std::string OperatingCosts::invalid_reason() const {
  const std::array<double, 22> lines{wages_and_salaries,
                                     animal_health,
                                     weed_and_pest,
                                     shearing,
                                     fertiliser,
                                     lime,
                                     seeds,
                                     vehicles_and_fuel,
                                     electricity,
                                     feed_and_grazing,
                                     dog_expenses,
                                     cultivation_and_sowing,
                                     cash_crop,
                                     repairs_and_maintenance,
                                     irrigation_charges,
                                     cartage,
                                     administration,
                                     insurance_and_acc,
                                     rates,
                                     interest,
                                     rent,
                                     depreciation};
  for (const double line : lines) {
    if (line < 0.0) {
      return "an operating cost line cannot be negative";
    }
  }
  if (annual_per_hectare() <= 0.0) {
    return "a farm with no operating cost at all is not a farm this model can say anything about";
  }
  return {};
}

std::string Prices::invalid_reason() const {
  if (lamb_dollars_per_kg_carcass < 0.0 || wool_dollars_per_kg < 0.0 ||
      cull_ewe_dollars_per_head < 0.0) {
    return "a price cannot be negative";
  }
  return {};
}

std::string to_string(LedgerReason reason) {
  switch (reason) {
    case LedgerReason::OperatingCost:
      return "operating cost";
    case LedgerReason::BoughtFeed:
      return "bought feed";
    case LedgerReason::SoldStock:
      return "sold stock";
    case LedgerReason::SoldWool:
      return "sold wool";
  }
  return "unknown";
}

FarmAccount::FarmAccount(double opening_balance_dollars, OperatingCosts costs, Prices prices,
                         double hectares)
    : opening_balance_(opening_balance_dollars),
      balance_(opening_balance_dollars),
      hectares_(std::max(0.0, hectares)),
      lowest_(opening_balance_dollars),
      costs_(costs),
      prices_(prices) {
  daily_operating_cost_ = costs_.annual_per_hectare() * hectares_ / kDaysInYear;
}

void FarmAccount::charge_day(const Date& date) {
  record(date, LedgerReason::OperatingCost, -daily_operating_cost_, "one day of operating cost");
}

void FarmAccount::record(const Date& date, LedgerReason reason, double dollars,
                         std::string detail) {
  balance_ += dollars;

  CashEntry entry;
  entry.date = date;
  entry.reason = reason;
  entry.dollars = dollars;
  entry.detail = std::move(detail);
  entries_.push_back(std::move(entry));

  if (balance_ < lowest_) {
    lowest_ = balance_;
    lowest_on_ = date;
  }
}

double FarmAccount::total_for(LedgerReason reason) const {
  return std::accumulate(entries_.begin(), entries_.end(), 0.0,
                         [reason](double running, const CashEntry& entry) {
                           return entry.reason == reason ? running + entry.dollars : running;
                         });
}

}  // namespace paddock::core
