// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
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

}  // namespace paddock::core
