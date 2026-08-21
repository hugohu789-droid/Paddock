// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>

#include <paddock/core/AnimalEnergy.hpp>
#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Pasture.hpp>

/// Animals eating pasture: the step that connects the two halves of the model.
///
/// Until this exists the sward grows and the animals compute a requirement, and
/// neither notices the other. The connection is what makes a grazing system
/// mean anything: a paddock that has been rested carries more feed than one
/// grazed yesterday, and a mob that cannot get what it needs loses weight.
namespace paddock::core {

/// Animals of one class, grazing together.
///
/// One representative animal and a head count rather than individuals. A mob is
/// what a farmer moves and what a paddock carries, and modelling six hundred
/// ewes separately would buy variance this model has no data to calibrate.
struct Mob {
  std::string name;
  AnimalClassParameters animal;
  AnimalState state;
  int head = 0;

  /// What the manager is feeding this mob for, in kg/head/day.
  ///
  /// **Intent, not observation.** `state.liveweight_change_kg_per_day` is what
  /// the mob actually did yesterday; this is what somebody is trying to make it
  /// do. They were the same field once, and demand was computed from it, which
  /// made the whole thing circular: a well fed mob ate to maintenance, changed
  /// by nothing, and so was offered maintenance again the next day. A year of
  /// that leaves a ewe on exactly her opening weight while the farmer's target
  /// gain sits in the panel doing nothing - it only ever reached the decision
  /// about how much feed to buy, which on a farm with grass to spare is no
  /// decision at all.
  ///
  /// Zero means hold weight, which is what an unmanaged mob gets and what the
  /// model did before this existed. Negative is not honoured: deliberately
  /// feeding an animal below maintenance is a decision this model does not
  /// represent, and the sign would silently shrink demand.
  double target_gain_kg_per_day = 0.0;

  [[nodiscard]] std::string validation_error() const;
};

/// What one day's grazing did to one paddock.
struct GrazingDay {
  /// What the mob needed, from its energy requirement (TMC Eq. 19).
  double demand_kg_dm = 0.0;

  /// What stood above the residual and could therefore be eaten.
  double offered_kg_dm = 0.0;

  double eaten_kg_dm = 0.0;
  double grass_eaten_kg_dm = 0.0;
  double legume_eaten_kg_dm = 0.0;
  double nitrogen_removed_kg = 0.0;

  /// Per head, which is what a stockman would look at.
  double intake_per_head_kg_dm = 0.0;

  /// True when the paddock could not meet the demand. This is not an error: it
  /// is the signal a farm is short of feed, and it is what a grazing system is
  /// judged by. A caller that ignores it is modelling animals that eat what
  /// they like regardless of what is there.
  bool feed_limited = false;

  /// Eaten over demanded, 1.0 when the mob was satisfied and 0 when there was
  /// nothing to eat.
  [[nodiscard]] double satisfaction() const noexcept;
};

/// Grazes one paddock for one day.
///
/// `sward` is one hectare of the paddock and `paddock_hectares` scales it: the
/// mob's demand is spread over the whole area, so the same mob on a bigger
/// paddock takes less from each hectare.
///
/// **Dry matter and its nitrogen leave the modelled system.** Both are recorded
/// as outflows so the budget still closes, but a real paddock gets most of that
/// nitrogen back within days as dung and urine, and this model does not return
/// it. A long run will therefore strip nitrogen from the farm in a way a real
/// one does not. That is a known and recorded gap, not an oversight; see
/// docs/validation/verify.md.
///
/// Throws std::invalid_argument for a malformed mob, a non-positive paddock
/// area, or a diet the animal model rejects.
GrazingDay graze(PastureSward& sward, double paddock_hectares, const Mob& mob,
                 const DietQuality& diet, const GrazingConditions& ground,
                 BudgetLedger* ledger = nullptr);

/// Advances a mob by one day on what it actually ate, and by one day of age.
///
/// This is the step that gives a short paddock a consequence. Without it a mob
/// grazes, comes back marked feed-limited, and is exactly the same animal
/// tomorrow - so a farm that ran out of feed would look identical to one that
/// did not, and no grazing system could be judged against another.
///
/// Returns what the day did, so a caller can report it.
LiveweightResponse advance_one_day(Mob& mob, const GrazingDay& day, const DietQuality& diet,
                                   const GrazingConditions& ground);

}  // namespace paddock::core
