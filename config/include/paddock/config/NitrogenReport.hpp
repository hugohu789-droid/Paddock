// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/config/Provenance.hpp>
#include <paddock/config/ScenarioRun.hpp>

/// What a farm leached, and what the rule says about it.
///
/// **A compliance line is a quotation, not a calculation.** New Zealand has no
/// national nitrogen loss limit: regional councils set them catchment by
/// catchment, so two farms a valley apart can be held to different figures. A
/// report that printed one number as "the threshold" would be inventing a
/// regulation. Everything here names the rule it is quoting and the zone that
/// rule applies to, and says plainly what the model did not count.
namespace paddock::config {

/// A nitrogen loss limit, loaded from `data/regulations/`.
struct NitrogenRegulation {
  std::string name;
  std::string authority;
  std::string plan;
  std::string zone;

  /// The loss above which a farm is subject to reductions against its own
  /// baseline. **A trigger, not a ceiling** - a farm above it is not in breach,
  /// it is required to reduce - and the verdict below is worded to match.
  SourcedValue leaching_trigger_kg_n_per_ha_per_year;
  std::string source_url;

  /// The national cap on synthetic nitrogen fertiliser, a different instrument:
  /// it limits what goes on rather than what comes out.
  SourcedValue fertiliser_cap_kg_n_per_ha_per_year;
  std::string cap_source;

  [[nodiscard]] std::string validation_error() const;
};

/// Loads a regulation file. Throws ConfigError with a line-precise message.
[[nodiscard]] NitrogenRegulation load_nitrogen_regulation(const std::string& path);

/// Where a year's leaching sits against the rule.
enum class NitrogenStanding : std::uint8_t {
  /// Below the trigger, with room to spare.
  Comfortable,
  /// Below the trigger but within a tenth of it - a year like this one would
  /// put the farm over.
  Close,
  /// Over the trigger, so the farm is in the group the rule asks to reduce.
  OverTheTrigger,
};

[[nodiscard]] std::string to_string(NitrogenStanding standing);

/// One year of nitrogen, as a farmer or a council would read it.
struct NitrogenYear {
  std::string label;
  core::DateRange range;

  double leached_kg_n_per_ha = 0.0;
  double drainage_mm = 0.0;
  double rainfall_mm = 0.0;

  /// Nitrogen the clover fixed, which is this farm's only nitrogen income
  /// besides what arrives in bought feed.
  double fixed_kg_n_per_ha = 0.0;

  /// Nitrogen the stock returned as dung and urine. On a grazed farm this, not
  /// fertiliser, is what leaches.
  double excreta_returned_kg_n_per_ha = 0.0;

  /// Synthetic nitrogen fertiliser applied. Zero here, and stated rather than
  /// omitted so a reader can see the cap is not the binding constraint.
  double fertiliser_kg_n_per_ha = 0.0;

  [[nodiscard]] NitrogenStanding standing(const NitrogenRegulation& rule) const;

  /// **Leaching per millimetre of drainage**, which is the diagnostic that
  /// separates a wet year from a leaky farm: a farm leaching twice as much in a
  /// year that drained three times as much is not leaking more, it is draining
  /// more.
  [[nodiscard]] double kg_n_per_mm_drainage() const {
    return drainage_mm > 0.0 ? leached_kg_n_per_ha / drainage_mm : 0.0;
  }
};

/// Reads one year out of a run.
[[nodiscard]] NitrogenYear nitrogen_year(const RunSummary& run, std::string label);

/// The compliance report: one year against the rule, in prose a farmer could
/// hand to a council.
[[nodiscard]] std::string nitrogen_compliance_report(const NitrogenYear& year,
                                                     const NitrogenRegulation& rule);

/// Several years side by side - **the comparison a single year cannot make.**
/// A farm's leaching moves with its drainage, so one year says almost nothing
/// about whether the farm or the weather is responsible.
[[nodiscard]] std::string nitrogen_years_report(const std::vector<NitrogenYear>& years,
                                                const NitrogenRegulation& rule);

}  // namespace paddock::config
