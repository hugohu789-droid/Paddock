// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/core/Grazing.hpp>
#include <paddock/core/SimulationClock.hpp>

/// A flock with an age structure.
///
/// **Why cohorts and not individual animals.** A farm does not manage
/// individuals and neither should a model of one. New Zealand sheep are run by
/// age class - ewe hoggets, two-tooths, mixed-age ewes, and the cull draft that
/// leaves at weaning - and a farmer who is asked how many ewes they have
/// answers by class, not by name. Tracking 417 animals separately would be
/// precise about something nobody measures: there is no source for how one ewe
/// differs from the next, so per-animal variation would be invented, and
/// inventing it at scale would look like detail.
///
/// It also keeps the determinism rule cheap. CLAUDE.md requires random draws
/// keyed by entity rather than by iteration order; a cohort has a stable
/// identity - the year it was born - where a position in a list does not.
///
/// **What a cohort is.** A `Mob` with an age. The existing mob already carries
/// one representative `AnimalState`, which is exactly a cohort's mean animal,
/// so this composes with what is here rather than replacing it.
///
/// **The year turns on 1 July**, with the farm year, because that is when a New
/// Zealand flock's classes are renamed: the lambs of the season past become
/// hoggets, hoggets become two-tooths, and the oldest draft goes.
namespace paddock::core {

/// One age class.
struct AgeCohort {
  /// The July this cohort's animals were born into. Its identity, and what a
  /// random draw is keyed by.
  int birth_year = 0;

  /// Whole years since that July. Zero for the season's lambs.
  int age_years = 0;

  Mob mob;

  /// A name a farmer would use, from the age. Hoggets at one, two-tooths at
  /// two, mixed-age after that.
  [[nodiscard]] std::string class_name() const;
};

/// Rates a flock turns over at.
///
/// **Two published sources disagree about replacement and both are carried.**
/// OVERSEER's *Characteristics of animals* gives sheep 20%; Ridler et al.
/// (2025), measuring 34 real New Zealand flocks, report a between-flock mean of
/// 29.2% (SD 5.0). The field study is more recent and directly measured, so it
/// is the default here - but a model that quietly picked one of two published
/// figures would be hiding a disagreement, so the other is beside it and
/// `docs/validation/verify.md` records both.
struct FlockRates {
  /// Share of the breeding flock replaced each year.
  double replacement_fraction = 0.292;

  /// Share of ewes presented for breeding that are culled or dead by
  /// mid-lactation. Ridler et al. (2025): 10.5% (SD 4.6).
  double loss_to_mid_lactation_fraction = 0.105;

  /// Share of the ewes present at weaning that are culled then - the main
  /// culling event of the year, for age, teeth and udders. Ridler et al.
  /// (2025): 16.5% (SD 8.3).
  double culled_at_weaning_fraction = 0.165;

  /// Share of the 10.5% loss that happens at lambing rather than spread from
  /// mating to mid-lactation. Ridler et al. (2025): "two-thirds of ewe
  /// mortalities between breeding and mid-lactation occurred during the lambing
  /// period."
  double share_of_loss_at_lambing = 2.0 / 3.0;

  /// Lambs tailed per hundred ewes put to the ram. Beef + Lamb New Zealand's
  /// national estimate for 2024-25 is a 132.3% ewe lambing percentage.
  double lambing_percentage = 132.3;

  /// The age at which a ewe is culled whatever else is true. **PLACEHOLDER**:
  /// the study names age as the commonest reason for culling without giving the
  /// age, and a replacement rate of 29.2% implies an average retention of about
  /// three and a half years rather than a hard limit.
  int cull_age_years = 6;

  [[nodiscard]] std::string invalid_reason() const;
};

/// When the year's events fall.
///
/// A New Zealand sheep calendar, and the dates a Canterbury flock runs to. They
/// are management choices rather than measurements - a farmer picks them - so
/// they are configurable and marked PLACEHOLDER where a scenario does not say.
struct FlockCalendar {
  int mating_month = 4;
  int mating_day = 1;

  int lambing_month = 8;
  int lambing_day = 20;

  int weaning_month = 12;
  int weaning_day = 1;

  /// 1 July, when the classes are renamed and the oldest draft goes.
  int year_turns_month = 7;
  int year_turns_day = 1;

  [[nodiscard]] std::string invalid_reason() const;
};

/// What happened to the flock today.
struct FlockDay {
  int born = 0;
  int died = 0;
  int culled = 0;

  /// True on the day the year turned, so a caller knows the classes moved.
  bool year_turned = false;

  [[nodiscard]] bool anything_happened() const noexcept {
    return born > 0 || died > 0 || culled > 0 || year_turned;
  }
};

/// The flock, oldest cohort first.
class Flock {
 public:
  Flock() = default;

  void add(AgeCohort cohort);

  /// Turns the year over: every cohort ages, and anything past the cull age
  /// leaves. Returns the head that left, for the account to sell.
  ///
  /// **Ageing and culling are one operation on purpose.** A flock that aged
  /// without culling would grow old, and one that culled without ageing would
  /// never get there; splitting them would let a caller do one and forget the
  /// other, which is the kind of bug a year-long run hides until the tenth
  /// year.
  int age_one_year(const FlockRates& rates);

  [[nodiscard]] const std::vector<AgeCohort>& cohorts() const noexcept { return cohorts_; }

  [[nodiscard]] int head() const noexcept;

  /// Head in cohorts old enough to breed. A hogget put to the ram is a real
  /// practice and a decision; until this model makes it, breeding starts at
  /// two.
  [[nodiscard]] int breeding_head() const noexcept;

  /// Removes `head` from the oldest cohorts first, which is what a farmer sells
  /// when they have to sell. Returns how many actually went.
  int sell_oldest(int head);

  /// Takes `head` out of the breeding cohorts, oldest first. What a cull draft
  /// and a lambing death both look like from the flock's side.
  int remove_from_breeding(int head);

  /// Runs one day of the flock's own year: mating, lambing, weaning and the
  /// turn of the year, each on its date and none of them on any other.
  ///
  /// **Deterministic, not stochastic.** A rate applied to a cohort gives a
  /// fraction of an animal, and this rounds rather than drawing. Two reasons:
  /// the published rates are between-flock means, so a draw would be inventing
  /// a within-flock distribution nobody measured; and a deterministic flock
  /// keeps the conservation assertions exact, which a per-animal draw would
  /// not. A scenario that wants a bad year should say so with weather rather
  /// than with a seed.
  FlockDay step(const Date& today, const FlockCalendar& calendar, const FlockRates& rates);

 private:
  std::vector<AgeCohort> cohorts_;
};

}  // namespace paddock::core
