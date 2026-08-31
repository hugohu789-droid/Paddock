// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/core/AnimalEnergy.hpp>
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

  /// Whether these animals are being kept or sold.
  ///
  /// A lamb cohort is finishing stock until the farmer picks replacements out
  /// of it at weaning; what is kept becomes breeding stock and what is not is
  /// sold. This is what `FarmOutlook::is_finishing_class` reads.
  bool is_finishing = false;

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
  /// **23 March, which is 150 days before lambing and not a round date.**
  ///
  /// It was 1 April, which is what a farmer would say and what a calendar would
  /// print - but 1 April to 20 August is 141 days against the 150 that OVERSEER
  /// (Table 28, from Freer et al. 2006) gives a ewe's gestation. The two dates
  /// described a pregnancy nine days shorter than a sheep has. Nothing read
  /// the mating date until pregnancy did, so nothing had caught it.
  ///
  /// A farmer sets both dates and the ram settles the interval; a scenario that
  /// wants 1 April should move lambing to 29 August rather than shorten the
  /// gestation. `invalid_reason` says so if the two drift apart again.
  int mating_month = 3;
  int mating_day = 23;

  int lambing_month = 8;
  int lambing_day = 20;

  int weaning_month = 12;
  int weaning_day = 1;

  /// 1 July, when the classes are renamed and the oldest draft goes.
  int year_turns_month = 7;
  int year_turns_day = 1;

  [[nodiscard]] std::string invalid_reason() const;

  /// Days from mating to lambing, which should be a ewe's gestation.
  [[nodiscard]] int gestation_days() const;

  /// Days from lambing to weaning. TMC (Characteristics of animals) Eq. 27
  /// defines a sheep's lactation length as exactly this, so the calendar
  /// already carries it and nothing else needs to state it.
  [[nodiscard]] int lactation_days() const;
};

/// What happened to the flock today.
struct FlockDay {
  int born = 0;
  int died = 0;
  int culled = 0;

  /// Lambs sold at weaning: the crop, less the replacements kept back. **The
  /// farm's main income**, and the reason a flock that keeps every lamb was
  /// growing without limit before this existed.
  int sold_store = 0;

  /// Lambs kept as replacements, which become next year's hoggets.
  int kept_as_replacements = 0;

  /// True on the day the year turned, so a caller knows the classes moved.
  bool year_turned = false;

  [[nodiscard]] bool anything_happened() const noexcept {
    return born > 0 || died > 0 || culled > 0 || sold_store > 0 || year_turned;
  }
};

/// The flock, oldest cohort first.
class Flock {
 public:
  Flock() = default;

  void add(AgeCohort cohort);

  /// A lamb cohort is built from this rather than from the ewes.
  ///
  /// **Without it a lamb is a ewe that happens to be young.** New cohorts used
  /// to copy the oldest ewe cohort's mob, so a newborn lamb inherited a 55 kg
  /// liveweight and a breeding ewe's sex factor - which is why drafting on lamb
  /// liveweight sold animals whose weight the model never earned. Set this from
  /// the scenario's lamb species and a lamb starts at its birth weight, with a
  /// lamb's parameters, and grows on what it eats.
  ///
  /// Left unset, cohorts are built from the ewes as before, so a flock that
  /// does not care about lambs needs no lamb data.
  void set_lamb_template(Mob lamb);

  /// Turns the year over: every cohort renames itself, and anything past the
  /// cull age leaves. Returns the head that left, for the account to sell.
  ///
  /// **Classes only: the animals' ages advance daily, in `step`.** This used to
  /// add a year to the representative animal's age as well, which was right for
  /// a flock stepped once a year and wrong for one stepped daily - a lamb born
  /// in August stayed nought days old until the following July, and the share
  /// of its diet that is milk is a function of exactly that number.
  ///
  /// **Ageing and culling are one operation on purpose.** A flock that aged
  /// without culling would grow old, and one that culled without ageing would
  /// never get there; splitting them would let a caller do one and forget the
  /// other, which is the kind of bug a year-long run hides until the tenth
  /// year.
  int age_one_year(const FlockRates& rates);

  [[nodiscard]] const std::vector<AgeCohort>& cohorts() const noexcept { return cohorts_; }

  /// Writable access, for the one thing the flock cannot work out for itself:
  /// what a mob's liveweight did on the paddock. The grazing model owns that
  /// number and the flock has to be told it.
  [[nodiscard]] std::vector<AgeCohort>& cohorts_for_update() noexcept { return cohorts_; }

  [[nodiscard]] int head() const noexcept;

  /// Head in cohorts old enough to breed. A hogget put to the ram is a real
  /// practice and a decision; until this model makes it, breeding starts at
  /// two.
  [[nodiscard]] int breeding_head() const noexcept;

  /// Removes `head` from the oldest cohorts first, which is what a farmer sells
  /// when they have to sell. Returns how many actually went.
  int sell_oldest(int head);

  /// Head in cohorts the farmer is finishing rather than keeping.
  [[nodiscard]] int finishing_head() const noexcept;

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

  /// Sets each breeding cohort's pregnancy and lactation for today.
  ///
  /// **The flock's calendar is what makes a ewe expensive in August.** Until
  /// this existed every ewe was fed maintenance every day of the year, which is
  /// about a third of a New Zealand stock unit - so a farm's whole feed demand
  /// was a third of what a farm's is. The dates were already here; nothing read
  /// them.
  ///
  /// Called from `step`, and separately testable.
  void set_reproductive_state(const Date& today, const FlockCalendar& calendar,
                              const FlockRates& rates);

 private:
  std::vector<AgeCohort> cohorts_;
  Mob lamb_template_;
  bool has_lamb_template_ = false;
};

}  // namespace paddock::core
