// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/FarmAccount.hpp>
#include <paddock/core/FarmDecision.hpp>
#include <paddock/core/Farmer.hpp>
#include <paddock/core/FeedStore.hpp>
#include <paddock/core/Flock.hpp>
#include <paddock/core/GrazingCalendar.hpp>
#include <paddock/core/Irrigation.hpp>
#include <paddock/core/Weather.hpp>

/// Running a scenario, and keeping enough of what happened to say something
/// about it afterwards.
///
/// The year-long test had this inline. It is here because comparing two grazing
/// systems means running the same farm twice, and because a report has to be
/// written from something.
namespace paddock::config {

/// What one run came to, day by day and in total.
struct RunSummary {
  /// Which system was being run, for a report or a chart legend. The caller
  /// names it, because a calendar can mix systems and only the caller knows
  /// what it was trying to demonstrate.
  std::string label;

  /// One entry per simulated day.
  std::vector<core::Date> dates;
  std::vector<double> cover_kg_dm_per_ha;

  /// The GREEN part of that cover, day by day - what an animal can actually
  /// eat. Cover includes the dead standing material above it, and on this farm
  /// the two part company badly by late summer.
  std::vector<double> green_kg_dm_per_ha;

  /// What grew each day, kg DM/ha.
  ///
  /// **Kept so the model can be checked against a season and not only a year.**
  /// Winchmore records production month by month, and a model that grows too
  /// much in spring and too little in summer sums to an annual figure that
  /// looks right. Without this the comparison could not be made.
  std::vector<double> growth_kg_dm_per_ha;

  /// Nitrate leached past the root zone each day, kg N/ha.
  ///
  /// **What a regional council asks a farm for.** Summed over a year it is the
  /// number a nitrogen limit is written against - Environment Canterbury's
  /// Selwyn Waihora zone puts that limit at 15 kg N/ha/yr, above which a farm
  /// must reduce below its own baseline.
  std::vector<double> nitrate_leached_kg_per_ha;
  std::vector<double> liveweight_kg;
  std::vector<int> paddock_of_first_mob;

  /// The weather each day was run on.
  ///
  /// Kept whole rather than reduced to the two or three numbers a chart wants.
  /// It is what the run was actually driven by, it is small - a year is 366
  /// records - and a view that wants to say what a day was like should read
  /// the day rather than somebody's summary of it. The alternative, a vector
  /// per variable, means editing this struct every time one more is wanted.
  std::vector<core::DailyWeather> weather;

  /// The water put on each day, as a mean over the farm in mm, and what the
  /// year came to.
  ///
  /// In the summary rather than worked out where it is printed, so that two
  /// ways of reporting one run cannot disagree about how much water it used -
  /// which is the roadmap's point about metrics belonging to the core.
  std::vector<double> irrigation_mm;
  core::IrrigationTally irrigation;

  /// FAO-56's water stress coefficient each day, averaged over the farm. One
  /// where the root zone still holds readily available water, falling to zero
  /// at wilting point.
  ///
  /// Kept because it is the number that turns a dry January into a feed
  /// deficit rather than into a lower soil moisture reading - and because it is
  /// what a comparison between a rain-fed farm and an irrigated one is
  /// actually about. Worked out here rather than where it is reported, so two
  /// ways of counting a dry year cannot disagree.
  std::vector<double> water_stress;

  /// Days the pasture's growth was held back by dry soil.
  [[nodiscard]] int days_water_stressed() const;

  double eaten_kg_dm = 0.0;

  /// Days on which any mob could not get what it needed, over the whole run.
  ///
  /// **Cumulative, and for reporting.** The farmer's destocking rule reads a
  /// consecutive count instead - see `FarmOutlook::consecutive_days_short` -
  /// and the two are deliberately separate fields now that feeding one to the
  /// other has been found to fire the rule permanently (E98).
  int days_short = 0;

  /// The longest run of short days in a row.
  ///
  /// Kept because it is the figure that says whether a year's short days were
  /// a drought or a scatter, which the total cannot: twenty-one in a row and
  /// twenty-one spread over a season are the same total and different farms.
  int longest_short_run_days = 0;

  int moves = 0;
  int short_spells = 0;
  int grazings_extended = 0;

  /// Every purchase the farmer made, with its date and its reason. Kept as a
  /// list rather than a total because "when did this farm need feed" and "how
  /// much did it need" are different questions and a report answers both.
  std::vector<core::FeedPurchase> purchases;

  /// Which system the farmer was running each day, when they were choosing.
  std::vector<core::GrazingSystem> system_each_day;

  [[nodiscard]] double bought_feed_kg_dm() const;
  [[nodiscard]] int days_feed_was_bought() const;

  /// The farm's cash, when the run was given an economics to keep it in.
  ///
  /// **Optional on purpose.** Every scenario that ran before money existed
  /// still runs, unchanged and unpriced: a run without economics reports what
  /// happened to the grass and says nothing about what it was worth. Asking for
  /// the second is a decision a scenario makes.
  std::optional<core::FarmAccount> account;

  /// What the flock did: born, died, culled, sold. Empty unless the run was
  /// given a flock.
  std::vector<core::FlockDay> flock_days;

  /// What the farm cut, kept and fed back. **The mechanism that turns a good
  /// year's growth into a bad year's feed**, and the reason utilisation used to
  /// read 46% in a dry year and 26% in a wet one for the same farm doing the
  /// same thing.
  core::FeedStore feed_store;

  /// Supplement that came off this farm rather than through the gate, kg DM.
  /// Separate from `bought_feed_kg_dm` because one is a cost and the other is
  /// a harvest.
  double conserved_fed_kg_dm = 0.0;

  /// Head at the close, when a flock was run.
  int closing_head = 0;

  /// Stock units carried, averaged over every day of the run.
  ///
  /// **A mean rather than a count, because a flock is not one number.** Lambing
  /// nearly doubles the head on the place and weaning takes it back, so "how
  /// many did this farm carry" is stock-unit-days over days - which is also the
  /// quantity that annual feed demand divides by, and therefore the one that
  /// compares with Beef + Lamb's per-hectare rates.
  ///
  /// Zero when no class in the flock has a published stock-unit rating. That is
  /// "not rated", not "no stock": a farm reporting a rate short by however many
  /// unrated head it carries would be worse than one reporting none.
  double mean_stock_units = 0.0;

  /// What a lamb weighed on the day the crop was drafted, kg.
  ///
  /// **The number the model was never able to state.** Lamb liveweight used to
  /// be whatever a lamb was born holding, because nothing drove it - which is
  /// why the drafting rule had to be switched off. Now lambs graze, so this is
  /// an outcome of the weather and the grass, and OVERSEER's own default of
  /// 20 kg (TMC Eq. 17) is something to check it against.
  double lamb_weaning_weight_kg = 0.0;

  core::BudgetLedger ledger;
  double closing_cover_kg_dm = 0.0;
  double closing_nitrogen_kg = 0.0;
  double closing_water_mm = 0.0;

  [[nodiscard]] double opening_liveweight_kg() const {
    return liveweight_kg.empty() ? 0.0 : liveweight_kg.front();
  }

  [[nodiscard]] double closing_liveweight_kg() const {
    return liveweight_kg.empty() ? 0.0 : liveweight_kg.back();
  }

  [[nodiscard]] double liveweight_change_kg() const {
    return closing_liveweight_kg() - opening_liveweight_kg();
  }

  /// The year's total nitrate leaching, kg N/ha.
  [[nodiscard]] double nitrate_leached_total_kg_per_ha() const;

  [[nodiscard]] double mean_cover_kg_dm_per_ha() const;
  [[nodiscard]] double lowest_cover_kg_dm_per_ha() const;
  [[nodiscard]] double highest_cover_kg_dm_per_ha() const;
};

/// The money side of a run, when a scenario asks for one.
///
/// **All four parts or none.** An account without prices cannot sell, a flock
/// without an account cannot be paid for, and decisions without either have
/// nothing to decide about - so this is one optional argument rather than four,
/// and a run either keeps books or does not.
struct FarmBusiness {
  core::OperatingCosts costs;
  core::Prices prices;
  double opening_balance_dollars = 0.0;

  core::Flock flock;
  core::FlockCalendar calendar;
  core::FlockRates rates;
  core::DecisionPolicy decisions;

  /// When this farm cuts a surplus and what it costs to keep it. A farm that
  /// does not conserve is the same farm with `conserves` off.
  core::ConservationPolicy conservation;
  core::ConservationLosses conservation_losses;
};

/// Runs a bundle for its own date range under a calendar the caller supplies.
///
/// The calendar is a parameter rather than being taken from the bundle so that
/// the same farm, the same weather and the same stock can be put through two
/// managements and the difference attributed to the management. That is the
/// only way a comparison between grazing systems means anything: change one
/// thing.
///
/// Throws if the bundle has no grid, no paddocks, or a calendar that does not
/// cover the run.
[[nodiscard]] RunSummary run_scenario(const ScenarioBundle& bundle,
                                      const core::GrazingCalendar& calendar,
                                      const core::DietQuality& diet, std::string label);

/// Runs the bundle under its own calendar.
[[nodiscard]] RunSummary run_scenario(const ScenarioBundle& bundle, const core::DietQuality& diet,
                                      std::string label);

/// Called once per simulated day, after the day has been stepped, with the farm
/// as it then stands.
///
/// It exists because a RunSummary keeps means and a map needs cells. The map
/// view used to grow its own pasture with no stock on it, so what it drew was a
/// farm nobody was grazing - the one picture guaranteed not to show the thing
/// the model is for. Rather than have the view run its own loop and drift from
/// this one, the run offers each day up as it happens.
///
/// Taking the Farm by reference and keeping nothing means an observer that
/// wants a day must copy what it wants there and then.
/// The schedule comes too, because what it decided this morning cannot be read
/// off the farm this evening. An observer that wants to explain a dry day needs
/// the soil the decision was made on and the reason water was held back, and
/// both are gone by the time the day is over.
using DayObserver =
    std::function<void(const core::Farm&, const core::FarmDay&, const core::IrrigationSchedule&)>;

/// Runs a bundle under a farmer who decides rather than follows.
///
/// The farmer picks the system from the state of the farm, moves stock, and
/// buys feed when the pasture cannot both carry the stock and stay above the
/// cover it is being held to. What they bought and when is in the summary.
[[nodiscard]] RunSummary run_managed_scenario(const ScenarioBundle& bundle,
                                              const core::ManagementPolicy& policy,
                                              const core::DietQuality& diet, std::string label);

/// The same run, keeping books.
///
/// The pasture, the stock and the weather behave exactly as they do without
/// `business` - money observes and decides, it does not feed the grass - so a
/// priced run and an unpriced one of the same scenario grow the same pasture.
[[nodiscard]] RunSummary run_managed_scenario(const ScenarioBundle& bundle,
                                              const core::ManagementPolicy& policy,
                                              const core::DietQuality& diet, std::string label,
                                              FarmBusiness business);

/// Runs a bundle under the farmer its own manifest describes.
///
/// This is what `[management]` is for. Until it existed the calendar was in the
/// bundle and the farmer's judgement was in whatever code started the run, so a
/// managed result could be reproduced only by somebody who also had that code -
/// which is not what a bundle is supposed to be.
///
/// Throws when the bundle names no policy. The alternative is to invent one,
/// and inventing the rules a farm was run under is exactly what this section
/// exists to stop.
[[nodiscard]] RunSummary run_managed_scenario(const ScenarioBundle& bundle,
                                              const core::DietQuality& diet, std::string label);

/// As above, reporting each day to `each_day` as it is stepped.
[[nodiscard]] RunSummary run_managed_scenario(const ScenarioBundle& bundle,
                                              const core::ManagementPolicy& policy,
                                              const core::DietQuality& diet, std::string label,
                                              const DayObserver& each_day,
                                              const core::IrrigationPolicy& irrigation = {},
                                              const core::IrrigationSystem& system = {},
                                              FarmBusiness* business = nullptr);

/// A calendar that runs one system for the whole of `run`, for comparing a
/// system against another rather than against a mixed year.
[[nodiscard]] core::GrazingCalendar whole_run_calendar(const core::DateRange& run,
                                                       core::GrazingSystem system,
                                                       int maximum_graze_days,
                                                       int minimum_spell_days);

}  // namespace paddock::config
