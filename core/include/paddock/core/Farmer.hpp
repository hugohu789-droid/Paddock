// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <vector>

#include <paddock/core/Farm.hpp>
#include <paddock/core/GrazingCalendar.hpp>

/// Who decides where the stock go.
///
/// The farm knows how to move a mob; the calendar knows which system is meant
/// to be running today. Neither knows when to act, and this is the piece that
/// does - the last of the two gaps in the simulation loop.
namespace paddock::core {

/// One mob moved, or not moved when it should have been.
struct MobMove {
  std::size_t mob = 0;
  std::size_t from = 0;
  std::size_t to = 0;

  /// How long the mob had been on the paddock it left.
  int days_grazed = 0;

  /// How long the paddock it arrived on had been resting.
  int rest_days = 0;

  /// **The shuffle, made visible.** True when the best paddock available had
  /// not had the spell the rule asks for, and the mob went on anyway.
  ///
  /// Smith and Dawson (1976) define the shuffle as what rotational intent
  /// becomes when there are too few paddocks for the mobs: "the effect must be
  /// to lengthen the grazing period or shorten the spelling period". This flag
  /// is the second of those, and `Farmer::Day::grazings_extended` is the first.
  /// Neither is implemented as a mode - both fall out of a farm that cannot
  /// keep its own rules.
  bool spell_was_short = false;
};

/// Moves mobs according to a grazing calendar.
///
/// Deliberately simple, and the simplicity is the point: the farmer moves a mob
/// when it has been somewhere long enough, and sends it to whichever free
/// paddock has rested longest. A cleverer rule - picking on cover, or looking
/// ahead - would be a management model, and this project has no source for one
/// yet. What this rule can do is show what happens when the rules cannot be
/// kept, which is the thing worth measuring.
class Farmer {
 public:
  explicit Farmer(GrazingCalendar calendar);

  /// What one day of decisions came to.
  struct Day {
    GrazingSystem system = GrazingSystem::SetStocking;
    std::vector<MobMove> moves;

    /// Moves that had to break the spell rule because nothing was rested
    /// enough.
    int short_spells = 0;

    /// Mobs that were due to move and could not, because every other paddock
    /// was occupied. The other half of the shuffle: the grazing lengthens
    /// instead.
    int grazings_extended = 0;
  };

  /// Decides and applies today's moves. Call before Farm::step, so a mob that
  /// moves eats on the paddock it moved to.
  Day decide(Farm& farm, const Date& date);

  [[nodiscard]] const GrazingCalendar& calendar() const noexcept { return calendar_; }

 private:
  GrazingCalendar calendar_;
};

}  // namespace paddock::core
