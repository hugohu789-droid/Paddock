// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <optional>
#include <string>

#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/Weather.hpp>

/// **What a farm can actually buy, as against what it would like to.**
///
/// Until this existed, a farm that could pay could farm without grass: the
/// farmer worked out a shortfall, called it a purchase, and the feed appeared.
/// Stocking had no upper brake but solvency, so profit rose monotonically to
/// 11.9 SU/ha on a Canterbury dryland block and `days short of feed` read zero
/// at every rung of the ladder (verify.md, E55).
///
/// This is the smallest thing that fixes that, and deliberately not a commodity
/// market: **one feed, one price, one finite quantity, one optional window.** No
/// suppliers, no substitution, no price response to demand, no contracts. The
/// question it exists to answer is whether a farm stocked past its feed supply
/// meets a constraint, not what the feed would have cost in a tight year.
///
/// **It constrains management, never physiology.** The market decides how much
/// feed the farmer can get; how much of it an animal can eat is
/// `potential_intake_kg_dm`, and nothing here touches it. The chain is:
/// management requests, the market supplies all or part or none, the farmer
/// offers what arrived, the animal eats what its appetite allows, and whatever
/// is still missing stays visible as a feed-supply shortfall that the existing
/// destocking policy may act on.
namespace paddock::core {

/// The market a scenario declares.
struct SupplementMarketPolicy {
  /// Total dry matter this market can supply across the whole window, kg DM.
  ///
  /// **Empty means unlimited, and that is an assumption rather than a finding.**
  /// No source in this repository states how much baleage a Canterbury farm can
  /// actually buy in a dry February, so inventing a tonnage would be worse than
  /// leaving the old behaviour in place and naming it. Empty is therefore the
  /// default, every run reports which of the two it was, and a scenario that
  /// wants the constraint states a number and marks it for what it is.
  std::optional<double> available_kg_dm;

  /// When the feed can be bought. Empty means all year.
  ///
  /// Also unsourced, and for the same reason: the seasonality of the New
  /// Zealand supplement trade is real and this project has read nothing that
  /// quantifies it. A scenario may state a window; none is invented here.
  std::optional<DateRange> window;

  /// True when this market can run out.
  [[nodiscard]] bool is_finite() const noexcept { return available_kg_dm.has_value(); }

  /// Empty when the policy is self-consistent, otherwise what is wrong.
  [[nodiscard]] std::string validation_error() const;
};

/// One supplementary feed, in finite supply.
///
/// Stateful across a run: what is sold is gone. Deterministic by construction -
/// there is no random draw here, and the caller is expected to buy **once per
/// day for the whole farm** rather than once per mob, so that nothing depends
/// on the order mobs happen to sit in.
class SupplementMarket {
 public:
  SupplementMarket() = default;
  explicit SupplementMarket(SupplementMarketPolicy policy);

  /// What the market supplies today against a request.
  ///
  /// Never negative, never more than was asked for, and never more than is
  /// left. Outside the window it supplies nothing. A request it cannot fill in
  /// full is a **partial fulfilment**, not a failure: the farmer takes what
  /// there is, and the rest stays missing.
  [[nodiscard]] double buy(const Date& date, double requested_kg_dm);

  /// What is left to sell. Infinity when the market is unlimited.
  [[nodiscard]] double remaining_kg_dm() const noexcept;

  [[nodiscard]] bool is_finite() const noexcept { return policy_.is_finite(); }

  /// Running totals, which are what a report needs to show that a shortage
  /// happened rather than that feed quietly stopped being bought.
  [[nodiscard]] double total_requested_kg_dm() const noexcept { return requested_kg_dm_; }

  [[nodiscard]] double total_purchased_kg_dm() const noexcept { return purchased_kg_dm_; }

  /// Requested and not supplied, cumulative. Zero on an unlimited market.
  [[nodiscard]] double total_unfilled_kg_dm() const noexcept;

  /// Days on which the market could not fill the day's request in full.
  [[nodiscard]] int short_days() const noexcept { return short_days_; }

  /// True once a finite market has nothing left.
  [[nodiscard]] bool is_exhausted() const noexcept;

  [[nodiscard]] const SupplementMarketPolicy& policy() const noexcept { return policy_; }

 private:
  SupplementMarketPolicy policy_;
  double purchased_kg_dm_ = 0.0;
  double requested_kg_dm_ = 0.0;
  int short_days_ = 0;
};

}  // namespace paddock::core
