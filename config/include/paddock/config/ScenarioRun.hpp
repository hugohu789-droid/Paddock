// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Farmer.hpp>
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

  /// Days on which any mob could not get what it needed.
  int days_short = 0;

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

  [[nodiscard]] double mean_cover_kg_dm_per_ha() const;
  [[nodiscard]] double lowest_cover_kg_dm_per_ha() const;
  [[nodiscard]] double highest_cover_kg_dm_per_ha() const;
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
using DayObserver = std::function<void(const core::Farm&, const core::FarmDay&)>;

/// Runs a bundle under a farmer who decides rather than follows.
///
/// The farmer picks the system from the state of the farm, moves stock, and
/// buys feed when the pasture cannot both carry the stock and stay above the
/// cover it is being held to. What they bought and when is in the summary.
[[nodiscard]] RunSummary run_managed_scenario(const ScenarioBundle& bundle,
                                              const core::ManagementPolicy& policy,
                                              const core::DietQuality& diet, std::string label);

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
                                              const core::IrrigationSystem& system = {});

/// A calendar that runs one system for the whole of `run`, for comparing a
/// system against another rather than against a mixed year.
[[nodiscard]] core::GrazingCalendar whole_run_calendar(const core::DateRange& run,
                                                       core::GrazingSystem system,
                                                       int maximum_graze_days,
                                                       int minimum_spell_days);

}  // namespace paddock::config
