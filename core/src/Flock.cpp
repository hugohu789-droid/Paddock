// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <utility>

#include <paddock/core/Flock.hpp>

namespace paddock::core {

namespace {

/// A year of ageing, in the days the animal state counts in.
constexpr double kDaysPerYear = 365.25;

}  // namespace

std::string AgeCohort::class_name() const {
  switch (age_years) {
    case 0:
      return "lambs";
    case 1:
      return "hoggets";
    case 2:
      return "two-tooths";
    default:
      return "mixed-age";
  }
}

std::string FlockRates::invalid_reason() const {
  if (replacement_fraction <= 0.0 || replacement_fraction >= 1.0) {
    return "replacement_fraction is a share and must lie between zero and one";
  }
  if (loss_to_mid_lactation_fraction < 0.0 || loss_to_mid_lactation_fraction >= 1.0) {
    return "loss_to_mid_lactation_fraction is a share and must lie in [0, 1)";
  }
  if (culled_at_weaning_fraction < 0.0 || culled_at_weaning_fraction >= 1.0) {
    return "culled_at_weaning_fraction is a share and must lie in [0, 1)";
  }
  if (cull_age_years < 2) {
    return "cull_age_years below two would sell the flock before it had bred";
  }
  return {};
}

void Flock::add(AgeCohort cohort) {
  cohorts_.push_back(std::move(cohort));
  // Oldest first, and by birth year rather than by age so the order is stable
  // across an ageing step rather than depending on when a cohort was added.
  std::stable_sort(
      cohorts_.begin(), cohorts_.end(),
      [](const AgeCohort& lhs, const AgeCohort& rhs) { return lhs.birth_year < rhs.birth_year; });
}

int Flock::age_one_year(const FlockRates& rates) {
  int culled = 0;

  for (AgeCohort& cohort : cohorts_) {
    ++cohort.age_years;
    cohort.mob.state.age_days += kDaysPerYear;
  }

  // Anything past the cull age goes. Counted before erasing, because a farmer
  // sells them rather than losing them.
  for (const AgeCohort& cohort : cohorts_) {
    if (cohort.age_years > rates.cull_age_years) {
      culled += cohort.mob.head;
    }
  }
  cohorts_.erase(std::remove_if(cohorts_.begin(), cohorts_.end(),
                                [&rates](const AgeCohort& cohort) {
                                  return cohort.age_years > rates.cull_age_years;
                                }),
                 cohorts_.end());
  return culled;
}

int Flock::head() const noexcept {
  return std::accumulate(
      cohorts_.begin(), cohorts_.end(), 0,
      [](int running, const AgeCohort& cohort) { return running + cohort.mob.head; });
}

int Flock::breeding_head() const noexcept {
  return std::accumulate(cohorts_.begin(), cohorts_.end(), 0,
                         [](int running, const AgeCohort& cohort) {
                           return cohort.age_years >= 2 ? running + cohort.mob.head : running;
                         });
}

int Flock::sell_oldest(int head) {
  int remaining = std::max(0, head);
  int sold = 0;

  for (AgeCohort& cohort : cohorts_) {
    if (remaining <= 0) {
      break;
    }
    const int from_this = std::min(cohort.mob.head, remaining);
    cohort.mob.head -= from_this;
    remaining -= from_this;
    sold += from_this;
  }

  cohorts_.erase(std::remove_if(cohorts_.begin(), cohorts_.end(),
                                [](const AgeCohort& cohort) { return cohort.mob.head <= 0; }),
                 cohorts_.end());
  return sold;
}

std::string FlockCalendar::invalid_reason() const {
  const auto valid = [](int month, int day) { return Date{2024, month, day}.is_valid(); };
  if (!valid(mating_month, mating_day)) {
    return "the mating date is not a date";
  }
  if (!valid(lambing_month, lambing_day)) {
    return "the lambing date is not a date";
  }
  if (!valid(weaning_month, weaning_day)) {
    return "the weaning date is not a date";
  }
  if (!valid(year_turns_month, year_turns_day)) {
    return "the date the year turns is not a date";
  }
  return {};
}

FlockDay Flock::step(const Date& today, const FlockCalendar& calendar, const FlockRates& rates) {
  FlockDay day;

  const auto is = [&today](int month, int date) {
    return today.month == month && today.day == date;
  };

  // **The year turns first.** Classes are renamed and the oldest draft leaves
  // before anything else happens, because a ewe culled for age on 1 July is not
  // available to be mated in the season that follows.
  if (is(calendar.year_turns_month, calendar.year_turns_day)) {
    day.culled += age_one_year(rates);
    day.year_turned = true;
  }

  // **Lambing.** The lambs of the season arrive as a cohort of their own, and
  // the bulk of the year's ewe deaths happen here.
  if (is(calendar.lambing_month, calendar.lambing_day)) {
    const int ewes = breeding_head();
    if (ewes > 0) {
      const int lambs = static_cast<int>(
          std::llround(static_cast<double>(ewes) * rates.lambing_percentage / 100.0));
      if (lambs > 0) {
        AgeCohort born;
        born.birth_year = today.year;
        born.age_years = 0;
        born.mob = cohorts_.empty() ? Mob{} : cohorts_.front().mob;
        born.mob.name = "lambs " + std::to_string(today.year);
        born.mob.head = lambs;
        born.mob.state.age_days = 0.0;
        born.is_finishing = true;
        add(std::move(born));
        day.born = lambs;
      }

      const double lost = static_cast<double>(ewes) * rates.loss_to_mid_lactation_fraction *
                          rates.share_of_loss_at_lambing;
      day.died = remove_from_breeding(static_cast<int>(std::llround(lost)));
    }
  }

  // **Weaning**: the main culling event, and the day the lamb crop is split.
  if (is(calendar.weaning_month, calendar.weaning_day)) {
    const int ewes = breeding_head();
    const double culled = static_cast<double>(ewes) * rates.culled_at_weaning_fraction;
    day.culled += remove_from_breeding(static_cast<int>(std::llround(culled)));

    // **The replacements come out of the crop, and the rest are sold.** How
    // many is the replacement rate applied to the breeding flock that will
    // remain - a farmer keeps enough ewe lambs to replace the ewes leaving, not
    // a share of the lambs born. Ridler et al. (2025) put that at 29.2%.
    const int remaining_ewes = breeding_head();
    const int wanted = static_cast<int>(
        std::llround(static_cast<double>(remaining_ewes) * rates.replacement_fraction));

    for (AgeCohort& cohort : cohorts_) {
      if (!cohort.is_finishing || cohort.mob.head <= 0) {
        continue;
      }
      const int kept = std::min(cohort.mob.head, std::max(0, wanted - day.kept_as_replacements));
      const int sold = cohort.mob.head - kept;

      day.kept_as_replacements += kept;
      day.sold_store += sold;
      cohort.mob.head = kept;
      // What is kept stops being finishing stock: it is next year's flock.
      cohort.is_finishing = false;
    }

    cohorts_.erase(std::remove_if(cohorts_.begin(), cohorts_.end(),
                                  [](const AgeCohort& cohort) { return cohort.mob.head <= 0; }),
                   cohorts_.end());
  }

  return day;
}

int Flock::finishing_head() const noexcept {
  return std::accumulate(cohorts_.begin(), cohorts_.end(), 0,
                         [](int running, const AgeCohort& cohort) {
                           return cohort.is_finishing ? running + cohort.mob.head : running;
                         });
}

int Flock::remove_from_breeding(int head) {
  int remaining = std::max(0, head);
  int removed = 0;

  // Oldest breeding cohorts first: a farmer culling for age and teeth takes the
  // old ewes, and a death at lambing falls hardest on them too.
  for (AgeCohort& cohort : cohorts_) {
    if (remaining <= 0) {
      break;
    }
    if (cohort.age_years < 2) {
      continue;
    }
    const int from_this = std::min(cohort.mob.head, remaining);
    cohort.mob.head -= from_this;
    remaining -= from_this;
    removed += from_this;
  }

  cohorts_.erase(std::remove_if(cohorts_.begin(), cohorts_.end(),
                                [](const AgeCohort& cohort) { return cohort.mob.head <= 0; }),
                 cohorts_.end());
  return removed;
}

}  // namespace paddock::core
