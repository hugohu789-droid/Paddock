// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <cstddef>
#include <utility>
#include <vector>

#include <paddock/core/Farmer.hpp>

namespace paddock::core {

Farmer::Farmer(GrazingCalendar calendar) : calendar_(std::move(calendar)) {}

Farmer::Day Farmer::decide(Farm& farm, const Date& date) {
  Day day;

  const GrazingRule& rule = calendar_.rule_on(date);
  day.system = rule.system;

  // Under set stocking the stock get the run of the whole farm. That is what
  // the system is - Smith and Dawson (1976) say of lambing that "the whole of
  // the farm area should be used for grazing" - and it is *not* the same as
  // leaving a mob where it stands. A mob confined to the paddock it happened to
  // be on would starve there while the rest of the farm grew, which is exactly
  // what the first run of the year-long scenario did.
  if (rule.system == GrazingSystem::SetStocking) {
    for (std::size_t index = 0; index < farm.mobs().size(); ++index) {
      farm.spread_mob(index);
    }
    return day;
  }

  // Coming out of set stocking, a mob has the run of everything. It has to be
  // put somewhere before a rotation can start.
  for (std::size_t index = 0; index < farm.mobs().size(); ++index) {
    if (farm.mobs()[index].paddocks.size() > 1) {
      farm.move_mob(index, farm.mobs()[index].paddocks.front());
    }
  }

  const std::vector<FarmMob>& mobs = farm.mobs();
  const std::vector<int>& rest = farm.days_since_grazed();

  for (std::size_t index = 0; index < mobs.size(); ++index) {
    if (mobs[index].days_on_paddock < rule.maximum_graze_days) {
      continue;
    }

    // The best free paddock is the one that has rested longest. Occupied ones
    // are out: a paddock carries at most one mob here, because two mobs sharing
    // ground needs a rule for how they divide the feed and there is no source
    // for one.
    std::size_t best = Farm::kNobody;
    int best_rest = -1;
    for (std::size_t paddock = 0; paddock < rest.size(); ++paddock) {
      if (paddock == mobs[index].paddock()) {
        continue;
      }
      if (farm.mob_on(paddock) != Farm::kNobody) {
        continue;
      }
      if (rest[paddock] > best_rest) {
        best_rest = rest[paddock];
        best = paddock;
      }
    }

    if (best == Farm::kNobody) {
      // Nowhere to go: every other paddock is occupied. The mob stays and the
      // grazing lengthens, which is the first of the two things Smith and
      // Dawson say happens when a farm has too few paddocks for its mobs.
      ++day.grazings_extended;
      continue;
    }

    MobMove move;
    move.mob = index;
    move.from = mobs[index].paddock();
    move.to = best;
    move.days_grazed = mobs[index].days_on_paddock;
    move.rest_days = best_rest;

    // The mob moves whether or not the paddock has had its spell, because the
    // stock have to eat. Going anyway with the shortfall recorded is what makes
    // the shuffle measurable rather than hidden.
    move.spell_was_short = best_rest < rule.minimum_spell_days;
    if (move.spell_was_short) {
      ++day.short_spells;
    }

    farm.move_mob(index, best);
    day.moves.push_back(move);
  }

  return day;
}

}  // namespace paddock::core
