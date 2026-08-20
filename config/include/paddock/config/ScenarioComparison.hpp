// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <paddock/config/ScenarioRun.hpp>

/// Putting several runs of the same model beside each other.
///
/// **This is the difference between a simulator and a tool somebody decides
/// with.** A simulator answers "what would this farm do"; a comparison answers
/// "which of these should I do", and the second question is the one a farm
/// adviser is actually asked. Nothing here models anything: every number is
/// read out of a run that has already happened, and the table's whole job is to
/// put them where two of them can be seen at once.
///
/// The numbers are pulled from the run's own budget ledger wherever the ledger
/// has them. That is deliberate: the ledger is what the conservation suite
/// balances to 1e-9, so a table built from it cannot disagree with the model it
/// is reporting. A total computed a second time here would be a second answer.
namespace paddock::config {

/// One scenario in a comparison: what it was called, what it came to, and what
/// was set up differently.
struct ComparedScenario {
  /// What the person running it called it. Theirs, not generated: "irrigated"
  /// and "as we farm it now" carry meaning that "Scenario 2" does not.
  std::string name;

  RunSummary summary;

  /// The farm's area, needed to turn millimetres of water into megalitres.
  double hectares = 0.0;

  /// How this scenario was set up, as label and value.
  ///
  /// Filled by the caller, because only the caller knows what the panel was
  /// showing. `compare` keeps the ones that actually differ between scenarios
  /// and drops the rest - a header listing thirty identical settings hides the
  /// two that matter.
  std::vector<std::pair<std::string, std::string>> settings;
};

/// One measured row of the table: the same quantity across every scenario.
struct MetricRow {
  std::string group;
  std::string name;
  std::string unit;
  /// Decimal places for display. Carried with the number because significance
  /// belongs to the quantity, not to whoever is printing it.
  int decimals = 0;
  std::vector<double> values;
};

/// One setting that differed between scenarios.
struct SettingRow {
  std::string name;
  std::vector<std::string> values;
};

/// Several runs, side by side.
struct ComparisonTable {
  std::vector<std::string> scenarios;

  /// Only the settings that are not the same everywhere. **A comparison whose
  /// header does not say what differs invites the reader to assume it was one
  /// thing when it was three.**
  std::vector<SettingRow> differences;

  std::vector<MetricRow> metrics;

  /// What the table cannot say, said out loud.
  ///
  /// A blank column reads as a zero, and a metric the model does not compute is
  /// not a zero - it is a question this tool has no opinion about. These go
  /// under the table wherever it is rendered.
  std::vector<std::string> caveats;
};

/// Builds the table. Scenarios keep the order they were given, so the first is
/// the one everything else is read against.
[[nodiscard]] ComparisonTable compare(const std::vector<ComparedScenario>& scenarios);

/// The table as Markdown, ready to paste into a report or a README.
[[nodiscard]] std::string as_markdown(const ComparisonTable& table);

/// The table as CSV, for a spreadsheet.
[[nodiscard]] std::string as_csv(const ComparisonTable& table);

/// A paragraph of what the table shows, against the first scenario.
///
/// **Facts and differences only; it recommends nothing.** A recommendation
/// needs a price for water and a price for what the stock produce, and this
/// project has a source for neither. Writing "B is the better choice" from
/// numbers that cannot weigh a megalitre against a kilogram of liveweight would
/// be the tool inventing an opinion and presenting it as a finding.
[[nodiscard]] std::string summarise(const ComparisonTable& table);

}  // namespace paddock::config
