// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <paddock/core/Weather.hpp>

/// Which grazing system a farm is running, and when.
///
/// The systems and their parameters come from:
///
///   Smith, M E and Dawson, A D (1976). Hill country grazing management.
///   Proceedings of the New Zealand Grassland Association, volume 38.
///
/// That paper describes three systems and gives an action calendar for a hill
/// country breeding property. Two of the three are represented here. The third
/// - the "shuffle" - deliberately is not: the paper defines it as what
/// rotational intent becomes when there are too few paddocks for the mobs, so
/// it is an outcome of the farm's subdivision rather than a way of managing
/// one, and a farm that cannot hold its spell will produce it without being
/// told to. See docs/verify.md.
namespace paddock::core {

enum class GrazingSystem : std::uint8_t {
  /// "animals graze the pasture almost continuously" - Smith and Dawson.
  /// Spelling still happens, but uncontrolled and selective.
  SetStocking,
  /// "the pasture is grazed for a short time and spelled for a long time".
  Rotational,
};

[[nodiscard]] std::string to_string(GrazingSystem system);

/// How grazing is managed during one stretch of the year.
struct GrazingRule {
  GrazingSystem system = GrazingSystem::SetStocking;

  /// "Do not graze a pasture for more than three days with the major grazing
  /// mob." Ignored under set stocking, where there is no shift.
  int maximum_graze_days = 3;

  /// The rest a paddock gets between grazings. Smith and Dawson give minimum
  /// spells for the Hamilton region of 12 days in spring and 35 in summer,
  /// autumn and winter, reducible to 25 in summer "only if substantial rain
  /// falls", and 40 or more in winter in colder districts.
  ///
  /// Whether the farm can actually hold this is a different question, and the
  /// one that separates rotation from the shuffle: a farm with too few paddocks
  /// for its mobs cannot, and the model should show that rather than pretend.
  int minimum_spell_days = 21;

  [[nodiscard]] std::string validation_error() const;
};

/// One stretch of the run under one rule.
///
/// Inclusive at both ends, like DateRange, because a grazing period is a set of
/// days rather than a half-open interval and a farmer names the last day of it.
struct GrazingPeriod {
  std::string name;
  DateRange dates;
  GrazingRule rule;
};

/// The whole plan for a run: which system applies on each day.
///
/// **A partition of the run's time axis**, in the same sense and for the same
/// reason PaddockMask is a partition of its ground. Every simulated day has
/// exactly one rule or the calendar is rejected: a gap is a day with no
/// management, and an overlap is two managements at once, and both would show
/// up downstream as pasture behaving oddly rather than as a plan that does not
/// add up.
///
/// A system may appear as many times as the plan needs it, with different
/// parameters each time. That is not a convenience - Smith and Dawson's own
/// calendar rotates at 21 days after weaning, lengthens to 35 as summer dries,
/// and shortens again before lambing, so a calendar that allowed one entry per
/// system could not express the source it comes from.
class GrazingCalendar {
 public:
  GrazingCalendar() = default;

  /// Periods in any order; they are sorted on construction. Throws
  /// std::invalid_argument if any period is malformed.
  explicit GrazingCalendar(std::vector<GrazingPeriod> periods);

  /// Empty when this calendar is a partition of `run`; otherwise what is wrong,
  /// naming the first day that is uncovered or doubly covered.
  [[nodiscard]] std::string validation_error(const DateRange& run) const;

  /// The rule in force on a day. Throws std::out_of_range when no period covers
  /// it, rather than returning a default nobody chose.
  [[nodiscard]] const GrazingRule& rule_on(const Date& date) const;

  /// The period covering a day, or nullptr.
  [[nodiscard]] const GrazingPeriod* period_on(const Date& date) const noexcept;

  [[nodiscard]] const std::vector<GrazingPeriod>& periods() const noexcept { return periods_; }

  [[nodiscard]] bool empty() const noexcept { return periods_.empty(); }

 private:
  std::vector<GrazingPeriod> periods_;
};

/// The dates a breeding property's year turns on.
///
/// Smith and Dawson's calendar is written relative to these rather than to
/// calendar dates - "as close to lambing as is practical", "from tupping to
/// three weeks before the start of lambing" - so the model anchors it the same
/// way. Moving lambing moves the whole plan, which is what a farmer changing
/// their lambing date actually does.
struct FarmYearEvents {
  Date lambing_start;
  Date tupping_start;

  /// "Lambs should be weaned by an average age of ten weeks."
  int weaning_age_days = 70;

  /// "The length of rotation is increased from tupping to three weeks before
  /// the start of lambing", and shortened over those three weeks.
  int pre_lambing_shortening_days = 21;

  [[nodiscard]] Date weaning_date() const noexcept;

  [[nodiscard]] std::string validation_error() const;
};

/// The default plan, built from Smith and Dawson's action calendar.
///
/// It covers `run` exactly, repeating the annual cycle for a run of any length.
/// Where the paper gives a number, the number is used; where it leaves
/// something to judgement - "if feed is available, the length of rotation may
/// be reduced over the flushing/tupping period" - the default does not guess,
/// and that is precisely where a farmer's own edit belongs.
///
/// Throws std::invalid_argument when the events are inconsistent.
[[nodiscard]] GrazingCalendar default_grazing_calendar(const FarmYearEvents& events,
                                                       const DateRange& run);

}  // namespace paddock::core
