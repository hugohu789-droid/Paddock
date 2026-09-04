// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <paddock/core/FarmAccount.hpp>
#include <paddock/core/SimulationClock.hpp>

/// How a farmer decides when the decisions cost money.
///
/// **The design problem, and why the obvious shape does not solve it.** The
/// farmer already has rules - move the mob at this cover, buy feed at that one -
/// and each is a threshold that acts directly on the farm. That works while
/// actions are free. It stops working the moment they are not, for two reasons
/// a threshold cannot express:
///
///   * **An economic decision is a comparison, not a level.** A farmer sells
///     stock because carrying them costs more than they are worth, which
///     depends on the feed price, the schedule and how long the pinch will
///     last - not on cover crossing a number.
///   * **Actions compete for one bank balance.** Buying feed and keeping stock
///     are the same decision seen twice, and a rule that acts on its own cannot
///     know whether the farm can afford what it just did.
///
/// **So a rule proposes and never acts.** Each rule reads the farm and returns
/// a `Proposal` carrying its own arithmetic: what it would cost, what it would
/// earn, and why. The manager collects every proposal, orders them by what they
/// do to the balance, and applies the ones the farm can afford. Nothing mutates
/// a farm until the money has been checked.
///
/// That separation is the whole of the pattern, and it buys three things that
/// direct-acting rules cannot: **a solvency constraint expressible at all**,
/// because the cost is known before the act; **a decision that can be
/// explained**, because the proposal carries its reason into the ledger; and
/// **rules testable without a farm**, because a proposal is a value.
///
/// **What this deliberately is not.** Not a hierarchy of rule classes. CLAUDE.md
/// forbids inheritance trees for exactly the reason they would fail here - a
/// destocking rule is not a kind of feed-buying rule - and a rule is a function
/// of farm state, which is what `std::function` says and a base class would
/// obscure. Not a rules engine either: the order is explicit and short, because
/// a farmer's priorities are and because an order nobody can read is an order
/// nobody can audit.
namespace paddock::core {

/// The things a farmer can do that move money.
enum class ActionKind : std::uint8_t {
  SellFinishedStock,  ///< Draft lambs at weight, the farm's main income.
  SellCullStock,      ///< Older ewes, sold because they are done rather than fat.
  SellWool,           ///< Shearing.
  BuyFeed,            ///< Supplement, when the pasture cannot carry the stock.
  Destock,            ///< Sell stock the farm cannot feed. The drought answer.
};

[[nodiscard]] std::string to_string(ActionKind kind);

/// Something the farm could do today, with the arithmetic that justifies it.
///
/// **Cost and revenue are both stated, not netted.** A proposal that earns
/// $4,000 and costs $500 in cartage is a different thing from one that nets
/// $3,500, and a farmer refusing the second because the cartage is due first is
/// a real situation.
struct Proposal {
  ActionKind kind = ActionKind::SellFinishedStock;

  /// How many head, or zero for an action measured in something else.
  int head = 0;

  double dollars_in = 0.0;
  double dollars_out = 0.0;

  /// Why, in a farmer's words. Carried into the ledger, so an account can say
  /// what a payment was for rather than only how much.
  std::string because;

  /// What the balance would do if this were applied.
  [[nodiscard]] double net_dollars() const noexcept { return dollars_in - dollars_out; }
};

/// What a rule is allowed to see.
///
/// **Deliberately narrow.** A rule that could see the whole farm would be able
/// to reach past the questions it is meant to answer, and every field here had
/// to be argued for. Adding one is a decision, not a convenience.
struct FarmOutlook {
  Date today{};

  int head = 0;
  double liveweight_kg = 0.0;

  /// Whether these animals are being finished for slaughter.
  ///
  /// **A breeding ewe at 55 kg is not a draft lamb**, and without this the
  /// drafting rule sells the flock every day of the year - which is what it did
  /// until a test fixture and a rule disagreed about what 55 kg meant. Set from
  /// the mob's species class.
  ///
  /// It is false for every mob this project currently ships. Lambs are not a
  /// stock class here yet: lambing is not modelled, so there is nothing for a
  /// finishing rule to sell. The rule is kept because the day lambs exist it is
  /// what turns a liveweight model into an income, and a rule that fires on
  /// nothing is visible where a missing rule is not.
  bool is_finishing_class = false;

  /// Mean cover across the farm, kg DM/ha, and the floor the farmer holds it to.
  double cover_kg_dm_per_ha = 0.0;
  double minimum_cover_kg_dm_per_ha = 0.0;

  /// **Consecutive** days the mob has not been able to eat what it needed, up
  /// to and including today. The signal a drought shows up in before anything
  /// else does, and the one the destocking rule reads.
  ///
  /// **Zero on the first day the stock get what they asked for.** A run of
  /// short days is a drought; the same number of short days scattered over a
  /// year is a farm that had some hard weeks, and the two ask for different
  /// decisions. This field used to be fed the year-to-date total, which meant
  /// the twenty-first short day of the year fired the rule whenever it fell -
  /// and, because that total cannot fall, kept it fired for the rest of the
  /// run. See E98 and E99.
  int consecutive_days_short = 0;

  /// Short days so far this run, whether or not they were in a row.
  ///
  /// **Reporting only.** Nothing decides on this. It is here so that a rule
  /// which wanted the seasonal total could have it without reaching for the
  /// field above, which is how the two came to be confused in the first place.
  int total_days_short = 0;

  double hectares = 0.0;

  /// What the farm has in the bank, and what a day of it costs.
  double balance_dollars = 0.0;
  double daily_operating_cost_dollars = 0.0;

  /// Feed the farm has in its own stack, kg DM.
  ///
  /// **A farm with silage does not ring a merchant.** The buying rule fires on
  /// cover, and cover says nothing about whether the farmer already has the
  /// answer in a pit. Without this a farm that had cut a surplus in spring
  /// bought hay against it in February.
  double stored_feed_kg_dm = 0.0;
};

/// A rule: reads the outlook, proposes or declines. Never mutates anything.
using DecisionRule = std::function<std::optional<Proposal>(const FarmOutlook&, const Prices&)>;

/// How far the farmer will go, and when.
///
/// Every number here is a management choice rather than a measurement, and is
/// marked PLACEHOLDER in the data file that carries it - the same standing as
/// the cover thresholds beside it in ManagementPolicy.
struct DecisionPolicy {
  /// Consecutive short days before the farmer accepts the mob is too big for
  /// the feed, rather than waiting for rain.
  int destock_after_days_short = 21;

  /// Share of the mob sold when that happens. Selling a fifth is a real
  /// decision; selling all of it is giving up, and a farmer who has to do that
  /// is past what this model is describing.
  double destock_fraction = 0.20;

  /// The farm keeps at least this many head, so a destocking cascade cannot
  /// empty it one fifth at a time.
  int minimum_head = 50;

  /// Cash below which the farmer stops buying feed and starts selling stock,
  /// as a number of days of operating cost. **This is the solvency rule**: a
  /// farm with a fortnight of cash left does not buy baleage.
  double days_of_cash_before_selling = 30.0;

  /// Liveweight a finished lamb is drafted at, and the killing-out share that
  /// turns it into a carcass.
  double draft_liveweight_kg = 38.0;
  double dressing_out_fraction = 0.43;

  /// What a farmer feeds a finishing lamb to gain, kg a day.
  ///
  /// **DERIVED from the farmer's own two dates and one weight.** A lamb weaned
  /// at about 17 kg has to reach `draft_liveweight_kg` before the tail is sold:
  /// weaning on 1 December to the autumn store sale on 1 May is 151 days, and
  /// 21 kg over that is 139 grams a day, which sits inside the 100 to 200 a
  /// finishing lamb is grown at.
  ///
  /// **The first version of this used 210 days** - weaning to the end of the
  /// farm year - and asked for 100 grams. The lambs delivered exactly that and
  /// reached 36 kg on the day they were sold, two short of drafting, because
  /// they had been fed to arrive two months after the farmer wanted them. A
  /// target is a date as much as a weight.
  ///
  /// It is what the mob is fed for rather than what it achieves; whether the
  /// grass delivers is the model's answer, not this number's.
  ///
  /// Zero feeds a finishing mob for maintenance, which is what happened before
  /// this existed: the lambs held their weaning weight and were never drafted.
  double finishing_gain_kg_per_day = 0.139;

  /// What a farmer feeds a lamb to gain while it is still on its mother.
  ///
  /// **A different phase and a different number.** A suckling lamb and a
  /// finishing one were fed the same target, which is what a single lamb mob
  /// with a single target does - and it weaned them at 23 kg where Beef + Lamb
  /// New Zealand say a top flock weans 40 kg of lamb per ewe mated. At this
  /// flock's 132.3% that is 30 kg a lamb, and from a 6 kg birth weight over the
  /// 103 days to weaning it is 235 grams a day. **DERIVED**, from Beef + Lamb's
  /// own figure and this flock's own dates.
  ///
  /// It sits under the 300 grams a day Beef + Lamb name as what a ewe has to be
  /// fed to put on her lambs, which is a peak rather than an average - so a
  /// derived mean below their peak is the consistent reading of the two.
  double suckling_gain_kg_per_day = 0.235;

  [[nodiscard]] std::string invalid_reason() const;
};

/// The rules this project ships, in the order the manager runs them.
///
/// Order is priority: selling what is finished comes before selling what is
/// not, and both come before buying feed, because a farmer short of cash sells
/// before they spend. Destocking is last because it is the decision the others
/// exist to avoid.
[[nodiscard]] std::vector<DecisionRule> standard_rules(const DecisionPolicy& policy);

/// Collects proposals, applies what the farm can afford, and records why.
///
/// **The manager is what makes a proposal a decision.** It is also the only
/// thing that touches the account, so there is one place to look when asking
/// how a farm ran out of money.
class FarmManager {
 public:
  FarmManager(DecisionPolicy policy, std::vector<DecisionRule> rules);

  /// What was actually done today, in the order it was done.
  [[nodiscard]] std::vector<Proposal> decide(const FarmOutlook& outlook, FarmAccount& account);

  [[nodiscard]] const DecisionPolicy& policy() const noexcept { return policy_; }

 private:
  DecisionPolicy policy_;
  std::vector<DecisionRule> rules_;
};

}  // namespace paddock::core
