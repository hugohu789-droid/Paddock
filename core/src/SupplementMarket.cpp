// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <limits>
#include <utility>

#include <paddock/core/SupplementMarket.hpp>

namespace paddock::core {

std::string SupplementMarketPolicy::validation_error() const {
  if (available_kg_dm.has_value() && *available_kg_dm < 0.0) {
    return "a supplement market cannot hold a negative quantity";
  }
  if (window.has_value() && !window->is_valid()) {
    return "a supplement market's window must start on or before it ends";
  }
  return {};
}

SupplementMarket::SupplementMarket(SupplementMarketPolicy policy) : policy_(std::move(policy)) {}

double SupplementMarket::remaining_kg_dm() const noexcept {
  if (!policy_.is_finite()) {
    return std::numeric_limits<double>::infinity();
  }
  return std::max(0.0, *policy_.available_kg_dm - purchased_kg_dm_);
}

bool SupplementMarket::is_exhausted() const noexcept {
  return policy_.is_finite() && remaining_kg_dm() <= 0.0;
}

double SupplementMarket::total_unfilled_kg_dm() const noexcept {
  return std::max(0.0, requested_kg_dm_ - purchased_kg_dm_);
}

double SupplementMarket::buy(const Date& date, double requested_kg_dm) {
  const double wanted = std::max(0.0, requested_kg_dm);
  if (wanted <= 0.0) {
    return 0.0;
  }
  requested_kg_dm_ += wanted;

  // **Outside the window there is nothing to buy, and the request still
  // counts.** A shortage the farm never records is a shortage the report
  // cannot show, which is the whole failure this class exists to end.
  if (policy_.window.has_value() && !policy_.window->contains(date)) {
    ++short_days_;
    return 0.0;
  }

  const double supplied = std::min(wanted, remaining_kg_dm());
  purchased_kg_dm_ += supplied;

  // A tolerance rather than an equality, because the request is a sum of
  // per-mob shortfalls and the last kilogram of a market will not land on it.
  if (supplied < wanted - 1e-9) {
    ++short_days_;
  }
  return supplied;
}

}  // namespace paddock::core
