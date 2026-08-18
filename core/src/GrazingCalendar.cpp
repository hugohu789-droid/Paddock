// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <paddock/core/GrazingCalendar.hpp>

namespace paddock::core {

namespace {

/// Smith and Dawson: "an initial 21 days" after weaning.
constexpr int kPostWeaningSpellDays = 21;

/// "increased from an initial 21 days to 35 days" as the drier summer
/// approaches, and the minimum spell they give for summer, autumn and winter.
constexpr int kDrySeasonSpellDays = 35;

/// "Do not graze a pasture for more than three days with the major grazing
/// mob."
constexpr int kMaximumGrazeDays = 3;

/// The paper puts the change from the post-weaning rotation to the longer dry
/// season one at the end of November: "This spelling of pastures before the end
/// of November is most beneficial in increasing clover content."
constexpr int kDrySeasonStartMonth = 12;
constexpr int kDrySeasonStartDay = 1;

Date add_days(const Date& date, std::int64_t days) noexcept {
  return Date::from_days_since_epoch(date.days_since_epoch() + days);
}

bool before_or_same(const Date& lhs, const Date& rhs) noexcept {
  return !(rhs < lhs);
}

/// The 1 December that starts the dry season rotation in the year `after`
/// falls in or after.
Date dry_season_start_on_or_after(const Date& after) noexcept {
  Date candidate{after.year, kDrySeasonStartMonth, kDrySeasonStartDay};
  if (candidate < after) {
    candidate = Date{after.year + 1, kDrySeasonStartMonth, kDrySeasonStartDay};
  }
  return candidate;
}

/// Clips a period to the run and appends it if anything is left.
void append_clipped(std::vector<GrazingPeriod>& into, const DateRange& run, std::string name,
                    const Date& first, const Date& last, const GrazingRule& rule) {
  const Date clipped_first = (first < run.first) ? run.first : first;
  const Date clipped_last = (run.last < last) ? run.last : last;
  if (clipped_last < clipped_first) {
    return;
  }
  into.push_back(GrazingPeriod{std::move(name), DateRange{clipped_first, clipped_last}, rule});
}

GrazingRule set_stocking() {
  GrazingRule rule;
  rule.system = GrazingSystem::SetStocking;
  rule.maximum_graze_days = 0;
  rule.minimum_spell_days = 0;
  return rule;
}

GrazingRule rotational(int spell_days) {
  GrazingRule rule;
  rule.system = GrazingSystem::Rotational;
  rule.maximum_graze_days = kMaximumGrazeDays;
  rule.minimum_spell_days = spell_days;
  return rule;
}

}  // namespace

std::string to_string(GrazingSystem system) {
  switch (system) {
    case GrazingSystem::SetStocking:
      return "set_stocking";
    case GrazingSystem::Rotational:
      return "rotational";
  }
  return "unknown";
}

std::string GrazingRule::validation_error() const {
  if (system == GrazingSystem::Rotational) {
    if (maximum_graze_days <= 0) {
      return "a rotational rule needs a positive maximum graze length";
    }
    if (minimum_spell_days <= 0) {
      return "a rotational rule needs a positive minimum spell";
    }
    if (minimum_spell_days <= maximum_graze_days) {
      return "a rotational rule spells for longer than it grazes; a spell of " +
             std::to_string(minimum_spell_days) + " days against a graze of " +
             std::to_string(maximum_graze_days) + " is set stocking by another name";
    }
  }
  return {};
}

Date FarmYearEvents::weaning_date() const noexcept {
  return add_days(lambing_start, weaning_age_days);
}

std::string FarmYearEvents::validation_error() const {
  if (!lambing_start.is_valid()) {
    return "the lambing date is not a valid date";
  }
  if (!tupping_start.is_valid()) {
    return "the tupping date is not a valid date";
  }
  if (weaning_age_days <= 0) {
    return "lambs cannot be weaned before they are born";
  }
  if (pre_lambing_shortening_days < 0) {
    return "the pre-lambing shortening cannot be negative";
  }
  return {};
}

GrazingCalendar::GrazingCalendar(std::vector<GrazingPeriod> periods)
    : periods_(std::move(periods)) {
  for (const GrazingPeriod& period : periods_) {
    if (!period.dates.is_valid()) {
      throw std::invalid_argument("GrazingCalendar: period '" + period.name +
                                  "' has an invalid or inverted date range");
    }
    const std::string rule_error = period.rule.validation_error();
    if (!rule_error.empty()) {
      throw std::invalid_argument("GrazingCalendar: period '" + period.name + "': " + rule_error);
    }
  }

  std::sort(periods_.begin(), periods_.end(),
            [](const GrazingPeriod& lhs, const GrazingPeriod& rhs) {
              return lhs.dates.first < rhs.dates.first;
            });
}

std::string GrazingCalendar::validation_error(const DateRange& run) const {
  if (!run.is_valid()) {
    throw std::invalid_argument("GrazingCalendar: the run has an invalid date range");
  }
  if (periods_.empty()) {
    return "the calendar has no periods, so no day of the run has a grazing rule";
  }

  // Overlaps first, because a doubly managed day is a different mistake from a
  // gap and saying which one it is saves the reader working it out.
  for (std::size_t i = 1; i < periods_.size(); ++i) {
    const Date& previous_last = periods_[i - 1].dates.last;
    const Date& next_first = periods_[i].dates.first;
    if (before_or_same(next_first, previous_last)) {
      return "'" + periods_[i - 1].name + "' and '" + periods_[i].name + "' both cover " +
             next_first.to_iso_string() + "; a day cannot be under two grazing rules";
    }
  }

  if (run.first < periods_.front().dates.first) {
    return "no grazing rule covers " + run.first.to_iso_string() +
           ", the first day of the run; the calendar starts on " +
           periods_.front().dates.first.to_iso_string();
  }
  if (periods_.back().dates.last < run.last) {
    return "no grazing rule covers " + run.last.to_iso_string() +
           ", the last day of the run; the calendar ends on " +
           periods_.back().dates.last.to_iso_string();
  }

  for (std::size_t i = 1; i < periods_.size(); ++i) {
    const Date expected = add_days(periods_[i - 1].dates.last, 1);
    if (expected < periods_[i].dates.first) {
      return "no grazing rule covers " + expected.to_iso_string() + ", between '" +
             periods_[i - 1].name + "' and '" + periods_[i].name + "'";
    }
  }

  return {};
}

const GrazingPeriod* GrazingCalendar::period_on(const Date& date) const noexcept {
  for (const GrazingPeriod& period : periods_) {
    if (period.dates.contains(date)) {
      return &period;
    }
  }
  return nullptr;
}

const GrazingRule& GrazingCalendar::rule_on(const Date& date) const {
  const GrazingPeriod* period = period_on(date);
  if (period == nullptr) {
    throw std::out_of_range("GrazingCalendar: no grazing rule covers " + date.to_iso_string());
  }
  return period->rule;
}

GrazingCalendar default_grazing_calendar(const FarmYearEvents& events, const DateRange& run) {
  const std::string events_error = events.validation_error();
  if (!events_error.empty()) {
    throw std::invalid_argument("default_grazing_calendar: " + events_error);
  }
  if (!run.is_valid()) {
    throw std::invalid_argument("default_grazing_calendar: the run has an invalid date range");
  }

  std::vector<GrazingPeriod> periods;

  // One cycle runs from a lambing to the day before the next, so the cycles
  // tile the years end to end and nothing has to be merged afterwards. The
  // years come from the run rather than from the event dates, so a calendar
  // written once still covers a run any distance away from it.
  for (int year = run.first.year - 1; year <= run.last.year + 1; ++year) {
    const Date lambing{year, events.lambing_start.month, events.lambing_start.day};
    const Date next_lambing{year + 1, events.lambing_start.month, events.lambing_start.day};
    if (!lambing.is_valid() || !next_lambing.is_valid()) {
      // A 29 February lambing date in a common year. Skipping the cycle leaves
      // a gap the caller will be told about, which is better than silently
      // moving lambing to a day the farmer did not choose.
      continue;
    }

    const Date weaning = add_days(lambing, events.weaning_age_days);
    const Date pre_lambing = add_days(next_lambing, -events.pre_lambing_shortening_days);

    Date tupping{year, events.tupping_start.month, events.tupping_start.day};
    if (!tupping.is_valid()) {
      continue;
    }
    // Tupping follows weaning within a farm year; a calendar date earlier in
    // the year belongs to the next one.
    if (tupping < weaning) {
      tupping = Date{year + 1, events.tupping_start.month, events.tupping_start.day};
      if (!tupping.is_valid()) {
        continue;
      }
    }

    // "The stock are set-stocked as close to lambing as is practical", through
    // to weaning at ten weeks.
    append_clipped(periods, run, "lambing", lambing, add_days(weaning, -1), set_stocking());

    // "This allows ewes to be shorn and begin on the rotation of the whole farm
    // as early as possible", at an initial 21 days; then "as the drier summer
    // approaches, the length of rotation is increased from an initial 21 days
    // to 35 days", the change falling at the end of November.
    const Date dry_season = dry_season_start_on_or_after(weaning);
    if (weaning < dry_season && dry_season < tupping) {
      append_clipped(periods, run, "post-weaning rotation", weaning, add_days(dry_season, -1),
                     rotational(kPostWeaningSpellDays));
      append_clipped(periods, run, "dry season rotation", dry_season, add_days(tupping, -1),
                     rotational(kDrySeasonSpellDays));
    } else {
      // A lambing date late enough that the end of November falls outside the
      // stretch between weaning and tupping. The paper's calendar assumes a
      // late-winter lambing and does not describe this farm, so the default
      // holds the initial rotation rather than inventing a second phase.
      append_clipped(periods, run, "post-weaning rotation", weaning, add_days(tupping, -1),
                     rotational(kPostWeaningSpellDays));
    }

    // "The length of rotation is increased from tupping to three weeks before
    // the start of lambing." The paper gives no figure for the increase beyond
    // the 35 day minimum it states for autumn and winter, so the default holds
    // at 35 rather than inventing a longer one. A colder district wanting the
    // "40 days plus" it mentions edits this period, which is exactly the kind
    // of judgement the paper leaves to the farmer.
    append_clipped(periods, run, "tupping to pre-lambing", tupping, add_days(pre_lambing, -1),
                   rotational(kDrySeasonSpellDays));

    // "Three weeks before lambing the length of rotation is gradually reduced
    // to even out the feed available on the lambing paddocks, and to increase
    // the feed intake of the ewes."
    append_clipped(periods, run, "pre-lambing shortening", pre_lambing, add_days(next_lambing, -1),
                   rotational(kPostWeaningSpellDays));
  }

  return GrazingCalendar(std::move(periods));
}

}  // namespace paddock::core
