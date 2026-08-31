// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <paddock/config/NitrogenReport.hpp>
#include <paddock/config/Provenance.hpp>
#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/config/ScenarioRun.hpp>

/// A year of farming, as a set of indicators somebody would act on.
///
/// **Every indicator carries how much it can be trusted, and that is the point
/// of building this at all.** A dashboard is the easiest place in a project like
/// this to undo its own discipline: a tile reading "27.3 kg N/ha" is read as a
/// measurement, and this one is a model output resting on a placeholder patch
/// uptake and excluding about 15% of the real loss. So `trust` is not decoration
/// beside the number - it is part of the number, it travels into every export,
/// and the last panel counts how many of the others are standing on evidence.
///
/// **This module knows nothing about Qt.** It assembles what a page or a
/// terminal or a CSV would show, so the window and the command line cannot
/// drift into showing different farms - which is what happened to `green` before
/// the GUI was taught to fill it.
namespace paddock::config {

/// Where a value sits against whatever it is measured against.
enum class Standing : std::uint8_t {
  /// No threshold exists for this one, so there is nothing to be good or bad
  /// against. Most indicators are this, and saying so is better than inventing
  /// a target.
  Unmeasured,
  /// Inside the band, with room.
  Good,
  /// Inside the band but near its edge - a year like this one would cross it.
  Watch,
  /// Outside the band.
  Over,
};

[[nodiscard]] std::string to_string(Standing standing);

/// One number a farmer or an auditor would look at.
struct Indicator {
  std::string name;
  double value = 0.0;
  std::string unit;

  Standing standing = Standing::Unmeasured;

  /// How far this number rests on something published. **Fitted counts as
  /// evidence and must say what it was fitted to**, which is why the note is
  /// not optional for those.
  Provenance trust = Provenance::Placeholder;

  /// What it is measured against, or - where the trust is weak - what would
  /// have to change before it could be quoted. One line, in a farmer's words.
  std::string note;

  /// The band, when there is one. Both ends may be absent.
  std::optional<double> low;
  std::optional<double> high;

  [[nodiscard]] bool rests_on_evidence() const;
};

/// A group of indicators that answer the same question.
struct DashboardPanel {
  std::string title;
  std::string question;  ///< What a reader is asking when they look here.
  std::vector<Indicator> indicators;
};

/// A daily series, for the trend charts.
struct DashboardSeries {
  std::string name;
  std::string unit;
  std::vector<double> values;

  /// A line drawn across the chart - the cover a farmer holds the farm to, the
  /// nitrogen trigger. Absent where there is nothing to draw.
  std::optional<double> reference;
};

/// Everything a dashboard page, a terminal or an export needs.
struct FarmDashboard {
  std::string farm;
  std::string label;
  core::DateRange range;

  std::vector<DashboardPanel> panels;

  std::vector<core::Date> dates;
  std::vector<DashboardSeries> series;

  /// Every indicator across every panel, flattened.
  [[nodiscard]] std::vector<Indicator> all_indicators() const;

  /// How many indicators rest on evidence, and how many do not. **The number
  /// that should be read first**, because it says how much of the rest means
  /// anything.
  [[nodiscard]] int indicators_on_evidence() const;
  [[nodiscard]] int indicators_total() const;
};

/// Builds the dashboard for one run.
///
/// `rule` is optional because a farm outside a zone with a nitrogen limit has
/// no threshold to be measured against, and inventing one would be inventing a
/// regulation. Without it the nitrogen panel still reports what leached; it just
/// has nothing to say about compliance.
[[nodiscard]] FarmDashboard build_dashboard(const ScenarioBundle& bundle, const RunSummary& run,
                                            std::string label,
                                            const std::optional<NitrogenRegulation>& rule = {});

/// The dashboard as a terminal page.
[[nodiscard]] std::string as_text(const FarmDashboard& dashboard);

/// The indicators as CSV - one row per indicator, with its standing, its trust
/// and its band. **The trust column is not optional in the export either**: a
/// spreadsheet that dropped it would be the same number with its caveat removed.
[[nodiscard]] std::string indicators_as_csv(const FarmDashboard& dashboard);

/// The daily series as CSV, one column per series, for plotting elsewhere.
[[nodiscard]] std::string series_as_csv(const FarmDashboard& dashboard);

/// Several years or several farms side by side, indicator by indicator.
///
/// **The comparison one dashboard cannot make.** A single year's leaching or
/// growth says almost nothing on a farm whose weather swings from 527 mm to
/// 1,036; what a reader needs is the same indicator across years, with the
/// standing beside each.
[[nodiscard]] std::string compare_dashboards_as_text(const std::vector<FarmDashboard>& boards);

[[nodiscard]] std::string compare_dashboards_as_csv(const std::vector<FarmDashboard>& boards);

}  // namespace paddock::config
