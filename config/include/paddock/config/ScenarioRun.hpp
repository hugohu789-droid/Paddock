// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/GrazingCalendar.hpp>

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

  double eaten_kg_dm = 0.0;

  /// Days on which any mob could not get what it needed.
  int days_short = 0;

  int moves = 0;
  int short_spells = 0;
  int grazings_extended = 0;

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

/// A calendar that runs one system for the whole of `run`, for comparing a
/// system against another rather than against a mixed year.
[[nodiscard]] core::GrazingCalendar whole_run_calendar(const core::DateRange& run,
                                                       core::GrazingSystem system,
                                                       int maximum_graze_days,
                                                       int minimum_spell_days);

}  // namespace paddock::config
