// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>

#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/Farmer.hpp>

/// Turning a run into something a person reads.
///
/// Markdown, from a template, filled from a RunSummary. It renders in a browser
/// and in a terminal, diffs cleanly, and can be handed to a language model later
/// without any of it having to be re-plumbed - which is the plan, and the reason
/// the numbers and the prose are assembled here rather than in a view.
namespace paddock::config {

/// What a report needs beyond the run itself.
struct ReportOptions {
  /// What to call the farm at the top of the page. The bundle's own name is
  /// used when this is empty.
  std::string farm_name;

  /// Included so a reader knows which management produced these numbers. Null
  /// for a run that followed a fixed calendar rather than a farmer's decisions.
  const core::ManagementPolicy* policy = nullptr;

  /// Whether to include the section saying what the report may be quoted for.
  ///
  /// On by default, and it should stay on. A report that presents a carrying
  /// capacity without saying the sheep figure is overstated by up to 17% is a
  /// more dangerous document than no report at all.
  bool include_evidence_notes = true;
};

/// Renders one run as a Markdown report.
///
/// Covers what was simulated, what happened to the pasture and to the stock,
/// every purchase of feed with its date and its reason, whether the budgets
/// closed, and what the whole thing may be relied on for.
[[nodiscard]] std::string render_report(const ScenarioBundle& bundle, const RunSummary& run,
                                        const ReportOptions& options = {});

/// Renders two runs side by side - the comparison a farmer actually wants,
/// which is not "what happened" but "what happened differently".
[[nodiscard]] std::string render_comparison_report(const ScenarioBundle& bundle,
                                                   const RunSummary& left, const RunSummary& right,
                                                   const ReportOptions& options = {});

}  // namespace paddock::config
