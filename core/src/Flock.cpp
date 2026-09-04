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

/// Days since the most recent occurrence of a month and day, on or before
/// today. Comparing three of these is how the year's phases are told apart
/// without any of them having to know where a year begins.
int days_since_last(const Date& today, int month, int day) {
  Date candidate{today.year, month, day};
  if (!candidate.is_valid()) {
    return -1;
  }
  if (candidate.days_since_epoch() > today.days_since_epoch()) {
    candidate = Date{today.year - 1, month, day};
  }
  return static_cast<int>(today.days_since_epoch() - candidate.days_since_epoch());
}

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

void Flock::set_lamb_template(Mob lamb) {
  lamb_template_ = std::move(lamb);
  has_lamb_template_ = true;
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

int Flock::sell_finishing(int head) {
  int remaining = std::max(0, head);
  int sold = 0;

  for (AgeCohort& cohort : cohorts_) {
    if (remaining <= 0) {
      break;
    }
    if (!cohort.is_finishing) {
      continue;
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
  if (!valid(store_sale_month, store_sale_day)) {
    return "the date the tail is sold is not a date";
  }
  if (!valid(year_turns_month, year_turns_day)) {
    return "the date the year turns is not a date";
  }

  // **The check that catches the nine days**, and the band is published rather
  // than picked. Sheep gestation runs 142 to 152 days across breeds, averaging
  // 147, with wool breeds such as Romney at 147 to 150; OVERSEER uses 150 (TMC
  // Table 28, from Freer et al. 2006). The shipped calendar used to say 141,
  // which is outside the range for a sheep of any breed - which is the point.
  //
  // A first band of 136 to 164 was written here and would have let those 141
  // days through, so it is recorded: a validator that admits the bug it was
  // written for is worse than none, because it certifies the mistake.
  const int gestation = gestation_days();
  if (gestation < 142 || gestation > 152) {
    return "mating to lambing is " + std::to_string(gestation) +
           " days, which is outside a sheep's gestation of 142 to 152";
  }

  const int lactation = lactation_days();
  if (lactation < 1) {
    return "weaning must fall after lambing";
  }
  return {};
}

int FlockCalendar::gestation_days() const {
  const Date mating{2024, mating_month, mating_day};
  Date lambing{2024, lambing_month, lambing_day};
  if (!mating.is_valid() || !lambing.is_valid()) {
    return 0;
  }
  if (lambing.days_since_epoch() < mating.days_since_epoch()) {
    lambing = Date{2025, lambing_month, lambing_day};
  }
  return static_cast<int>(lambing.days_since_epoch() - mating.days_since_epoch());
}

int FlockCalendar::lactation_days() const {
  const Date lambing{2024, lambing_month, lambing_day};
  Date weaning{2024, weaning_month, weaning_day};
  if (!lambing.is_valid() || !weaning.is_valid()) {
    return 0;
  }
  if (weaning.days_since_epoch() < lambing.days_since_epoch()) {
    weaning = Date{2025, weaning_month, weaning_day};
  }
  return static_cast<int>(weaning.days_since_epoch() - lambing.days_since_epoch());
}

void Flock::set_reproductive_state(const Date& today, const FlockCalendar& calendar,
                                   const FlockRates& rates) {
  const int since_mating = days_since_last(today, calendar.mating_month, calendar.mating_day);
  const int since_lambing = days_since_last(today, calendar.lambing_month, calendar.lambing_day);
  const int since_weaning = days_since_last(today, calendar.weaning_month, calendar.weaning_day);
  if (since_mating < 0 || since_lambing < 0 || since_weaning < 0) {
    return;
  }

  // Mating more recent than lambing means she is carrying; lambing more recent
  // than weaning means she is milking. Comparing recencies rather than testing
  // date ranges is what lets a phase cross the new year without a special case.
  const bool carrying = since_mating < since_lambing;
  const bool milking = since_lambing < since_weaning;

  const double litter = rates.lambing_percentage / 100.0;

  for (AgeCohort& cohort : cohorts_) {
    // A lamb or a hogget is neither, and neither is a cohort being finished.
    const bool breeds = cohort.age_years >= 2 && !cohort.is_finishing;

    cohort.mob.state.days_pregnant = breeds && carrying ? since_mating : 0;
    cohort.mob.state.days_lactating = breeds && milking ? since_lambing : 0;
    cohort.mob.state.young = breeds && (carrying || milking) ? litter : 0.0;

    // **A lamb is on its mother while its mother is milking**, which is the
    // same span from the other side. What it drinks is charged to her, so the
    // paddock only answers for the rest.
    cohort.mob.state.on_the_mother = cohort.age_years == 0 && milking;
  }
}

FlockDay Flock::step(const Date& today, const FlockCalendar& calendar, const FlockRates& rates) {
  FlockDay day;

  // **Older by the days that passed, not by the number of calls.** The milk
  // share of a lamb's diet is a function of its age in days (TMC Eq. 15), so
  // this has to move - an age that only advanced once a year kept every lamb
  // newborn until the following July.
  //
  // Advancing by one per call would have been enough for the run loop, which
  // steps every day, and wrong for everything else: a test stepping five
  // scattered dates aged its flock five days across ten months, and a caller
  // that stepped the same date twice aged it twice. The date is the authority.
  if (has_stepped_) {
    const auto elapsed =
        static_cast<double>(today.days_since_epoch() - last_stepped_.days_since_epoch());
    if (elapsed > 0.0) {
      for (AgeCohort& cohort : cohorts_) {
        cohort.mob.state.age_days += elapsed;
      }
    }
  }
  last_stepped_ = today;
  has_stepped_ = true;

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
        if (has_lamb_template_) {
          born.mob = lamb_template_;
        } else {
          born.mob = cohorts_.empty() ? Mob{} : cohorts_.front().mob;
        }
        born.mob.name = "lambs " + std::to_string(today.year);
        born.mob.head = lambs;
        born.mob.state.age_days = 0.0;

        // **Born at a birth weight, not at its mother's.** TMC Eq. 11-14 make
        // that a share of the dam's reference weight that falls as the litter
        // grows, so a flock at 1.32 lambs gets a lamb between a single's and a
        // twin's.
        born.mob.state.liveweight_kg =
            birth_weight_kg(born.mob.animal, rates.lambing_percentage / 100.0);
        born.mob.state.liveweight_change_kg_per_day = 0.0;
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

    // **The crop splits three ways, not two.** Replacements first, because a
    // farm that cannot replace its ewes has no next year. Then what is kept to
    // finish - grown on this farm's grass and drafted as it reaches weight -
    // and the tail goes as stores.
    //
    // Splitting it two ways made this a store farm, which is not what its own
    // cost survey describes: Beef + Lamb's Class 6 is "S.I. Finishing
    // Breeding", and the whole crop was leaving at weaning at 17 kg.
    for (AgeCohort& cohort : cohorts_) {
      if (!cohort.is_finishing || cohort.mob.head <= 0) {
        continue;
      }
      const int kept = std::min(cohort.mob.head, std::max(0, wanted - day.kept_as_replacements));
      const int rest = cohort.mob.head - kept;
      const int finished = static_cast<int>(
          std::llround(static_cast<double>(rest) * std::clamp(rates.finished_fraction, 0.0, 1.0)));
      const int sold = rest - finished;

      day.kept_as_replacements += kept;
      day.kept_to_finish += finished;
      day.sold_store += sold;

      if (finished > 0) {
        // The finishers stay a cohort of their own and stay finishing stock, so
        // the drafting rule can take them as they come to weight.
        AgeCohort finishing = cohort;
        finishing.mob.name = "finishing " + std::to_string(today.year);
        finishing.mob.head = finished;
        finishing.is_finishing = true;
        cohorts_.push_back(std::move(finishing));
      }

      cohort.mob.head = kept;
      // What is kept as a replacement stops being finishing stock: it is next
      // year's flock.
      cohort.is_finishing = false;
    }

    // Sorted again, because the finishers were appended rather than added.
    std::stable_sort(
        cohorts_.begin(), cohorts_.end(),
        [](const AgeCohort& lhs, const AgeCohort& rhs) { return lhs.birth_year < rhs.birth_year; });

    cohorts_.erase(std::remove_if(cohorts_.begin(), cohorts_.end(),
                                  [](const AgeCohort& cohort) { return cohort.mob.head <= 0; }),
                   cohorts_.end());
  }

  // **The tail.** What has not made weight by autumn goes as a store, because a
  // farmer does not carry stock through a winter to find out. Without this the
  // finishing cohort simply accumulated: 364 lambs kept, none drafted - they
  // reach the 38 kg draft weight on the last day of the farm year and not
  // before - and a farm that had sold its crop for $46,000 sold nothing at all.
  if (is(calendar.store_sale_month, calendar.store_sale_day)) {
    int tail = 0;
    for (AgeCohort& cohort : cohorts_) {
      if (cohort.is_finishing) {
        tail += cohort.mob.head;
        cohort.mob.head = 0;
      }
    }
    cohorts_.erase(std::remove_if(cohorts_.begin(), cohorts_.end(),
                                  [](const AgeCohort& cohort) { return cohort.mob.head <= 0; }),
                   cohorts_.end());
    day.sold_store += tail;
  }

  // Last, so that a cohort born or weaned today is already in the flock when it
  // is asked whether it is carrying anything.
  set_reproductive_state(today, calendar, rates);

  // What the day left on the place. Taken here rather than by the caller so a
  // record and the flock it describes cannot disagree.
  day.head = head();
  day.breeding_head = breeding_head();

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
