// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <paddock/core/FarmDecision.hpp>

namespace paddock::core {

namespace {

/// Wool off one ewe in a year. **PLACEHOLDER** - a real fleece weight varies
/// with breed and season, and no sourced figure is carried here.
constexpr double kFleeceKgPerHead = 5.0;

/// What a day of supplement costs a head, in kilograms of dry matter, when the
/// farmer is feeding out. **PLACEHOLDER**, and the feed price it is charged at
/// is the one the economics file carries.
constexpr double kSupplementKgPerHeadPerDay = 1.2;
constexpr double kSupplementDollarsPerKgDm = 0.42;

/// Whether the farm is close enough to the edge that selling beats spending.
bool short_of_cash(const FarmOutlook& outlook, const DecisionPolicy& policy) {
  if (outlook.daily_operating_cost_dollars <= 0.0) {
    return false;
  }
  const double days_left = outlook.balance_dollars / outlook.daily_operating_cost_dollars;
  return days_left < policy.days_of_cash_before_selling;
}

}  // namespace

std::string to_string(ActionKind kind) {
  switch (kind) {
    case ActionKind::SellFinishedStock:
      return "sold finished stock";
    case ActionKind::SellCullStock:
      return "sold cull stock";
    case ActionKind::SellWool:
      return "sold wool";
    case ActionKind::BuyFeed:
      return "bought feed";
    case ActionKind::Destock:
      return "destocked";
  }
  return "unknown";
}

std::string DecisionPolicy::invalid_reason() const {
  if (destock_after_days_short < 1) {
    return "destock_after_days_short must be at least one";
  }
  if (destock_fraction <= 0.0 || destock_fraction >= 1.0) {
    return "destock_fraction is a share of the mob and must lie between zero and one";
  }
  if (minimum_head < 0) {
    return "minimum_head cannot be negative";
  }
  if (days_of_cash_before_selling < 0.0) {
    return "days_of_cash_before_selling cannot be negative";
  }
  if (draft_liveweight_kg <= 0.0) {
    return "draft_liveweight_kg must be positive";
  }
  if (dressing_out_fraction <= 0.0 || dressing_out_fraction >= 1.0) {
    return "dressing_out_fraction is a share and must lie between zero and one";
  }
  return {};
}

std::vector<DecisionRule> standard_rules(const DecisionPolicy& policy) {
  std::vector<DecisionRule> rules;

  // **Shearing.** Once a year, and the farm's one income that does not cost it
  // an animal. Charged as revenue only; the shearing bill is already in the
  // operating costs.
  rules.emplace_back(
      [](const FarmOutlook& outlook, const Prices& prices) -> std::optional<Proposal> {
        if (outlook.today.month != 11 || outlook.today.day != 1 || outlook.head <= 0) {
          return std::nullopt;
        }
        Proposal shearing;
        shearing.kind = ActionKind::SellWool;
        shearing.head = outlook.head;
        shearing.dollars_in =
            static_cast<double>(outlook.head) * kFleeceKgPerHead * prices.wool_dollars_per_kg;
        shearing.because = "shore " + std::to_string(outlook.head) + " head";
        return shearing;
      });

  // **Drafting finished stock.** The farm's main income, and the reason a
  // liveweight model earns its keep: an animal is sold when it reaches a
  // weight, so anything that slows growth delays the cheque.
  rules.emplace_back([policy](const FarmOutlook& outlook,
                              const Prices& prices) -> std::optional<Proposal> {
    if (!outlook.is_finishing_class) {
      return std::nullopt;
    }
    if (outlook.liveweight_kg < policy.draft_liveweight_kg || outlook.head <= policy.minimum_head) {
      return std::nullopt;
    }
    // A tenth of the mob at a time, which is what a draft is.
    const int drafted = std::max(1, outlook.head / 10);
    const double carcass_kg = policy.draft_liveweight_kg * policy.dressing_out_fraction;

    Proposal draft;
    draft.kind = ActionKind::SellFinishedStock;
    draft.head = drafted;
    draft.dollars_in =
        static_cast<double>(drafted) * carcass_kg * prices.lamb_dollars_per_kg_carcass;
    draft.because = "drafted " + std::to_string(drafted) + " head at " +
                    std::to_string(static_cast<int>(policy.draft_liveweight_kg)) + " kg";
    return draft;
  });

  // **Buying feed.** Only while the farm can afford to. A farmer a fortnight
  // from an empty account does not buy baleage, and this is where that shows.
  rules.emplace_back(
      [policy](const FarmOutlook& outlook, const Prices& /*prices*/) -> std::optional<Proposal> {
        if (outlook.head <= 0 || outlook.cover_kg_dm_per_ha > outlook.minimum_cover_kg_dm_per_ha) {
          return std::nullopt;
        }
        if (short_of_cash(outlook, policy)) {
          return std::nullopt;
        }
        const double kg = static_cast<double>(outlook.head) * kSupplementKgPerHeadPerDay;

        Proposal feed;
        feed.kind = ActionKind::BuyFeed;
        feed.head = outlook.head;
        feed.dollars_out = kg * kSupplementDollarsPerKgDm;
        feed.because = "fed out to " + std::to_string(outlook.head) + " head below minimum cover";
        return feed;
      });

  // **Destocking.** Last, because it is what the rules above exist to avoid,
  // and it fires on either of two things going wrong: the feed running out for
  // long enough that rain will not fix it, or the money running out.
  rules.emplace_back([policy](const FarmOutlook& outlook,
                              const Prices& prices) -> std::optional<Proposal> {
    const bool feed_has_run_out = outlook.days_short >= policy.destock_after_days_short;
    const bool money_has_run_out = short_of_cash(outlook, policy) &&
                                   outlook.cover_kg_dm_per_ha <= outlook.minimum_cover_kg_dm_per_ha;
    if (!feed_has_run_out && !money_has_run_out) {
      return std::nullopt;
    }
    if (outlook.head <= policy.minimum_head) {
      return std::nullopt;
    }

    const int sellable = outlook.head - policy.minimum_head;
    const int sold = std::max(
        1, std::min(sellable, static_cast<int>(std::floor(static_cast<double>(outlook.head) *
                                                          policy.destock_fraction))));

    Proposal destock;
    destock.kind = ActionKind::Destock;
    destock.head = sold;
    destock.dollars_in = static_cast<double>(sold) * prices.cull_ewe_dollars_per_head;
    destock.because = feed_has_run_out
                          ? "sold " + std::to_string(sold) + " head after " +
                                std::to_string(outlook.days_short) + " days short of feed"
                          : "sold " + std::to_string(sold) + " head to stay solvent";
    return destock;
  });

  return rules;
}

FarmManager::FarmManager(DecisionPolicy policy, std::vector<DecisionRule> rules)
    : policy_(policy), rules_(std::move(rules)) {}

std::vector<Proposal> FarmManager::decide(const FarmOutlook& outlook, FarmAccount& account) {
  // **Collect first, act second.** Every rule sees the same farm - the one it
  // was given - so a rule cannot be made to answer differently by whichever
  // rule happened to run before it.
  std::vector<Proposal> proposals;
  for (const DecisionRule& rule : rules_) {
    if (std::optional<Proposal> proposed = rule(outlook, account.prices())) {
      proposals.push_back(*std::move(proposed));
    }
  }

  // **Money in before money out**, whatever order the rules are written in. A
  // farm that would be solvent after selling should not be refused a purchase
  // because the sale had not been banked yet.
  std::stable_sort(proposals.begin(), proposals.end(),
                   [](const Proposal& lhs, const Proposal& rhs) {
                     return lhs.net_dollars() > rhs.net_dollars();
                   });

  std::vector<Proposal> applied;
  for (const Proposal& proposal : proposals) {
    // **The solvency constraint, and the only place it lives.** A proposal that
    // would take the farm below zero is refused rather than applied and
    // regretted - which is the whole reason a rule proposes instead of acting.
    if (proposal.net_dollars() < 0.0 && account.balance() + proposal.net_dollars() < 0.0) {
      continue;
    }

    if (proposal.dollars_in > 0.0) {
      const LedgerReason reason =
          proposal.kind == ActionKind::SellWool ? LedgerReason::SoldWool : LedgerReason::SoldStock;
      account.record(outlook.today, reason, proposal.dollars_in, proposal.because);
    }
    if (proposal.dollars_out > 0.0) {
      account.record(outlook.today, LedgerReason::BoughtFeed, -proposal.dollars_out,
                     proposal.because);
    }
    applied.push_back(proposal);
  }
  return applied;
}

}  // namespace paddock::core
