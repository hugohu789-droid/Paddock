// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <paddock/config/Provenance.hpp>
#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/FarmAccount.hpp>
#include <paddock/core/FarmDecision.hpp>

/// What a farm spends and what it sells, loaded from data.
///
/// **The file existed for a while and nothing read it.** Every cost in
/// `data/economics/canterbury-sheep.toml` came from Beef + Lamb New Zealand's
/// Class 6 survey, was cited line by line, and was then typed by hand into the
/// tests that needed it - so the twenty-two figures a farm's year rests on lived
/// in two places, and only one of them had a source. That is the eighth time
/// this project has found a quantity implemented carefully and read by nothing;
/// ADR 0014 names the pattern and `docs/validation/verify.md` counts them.
namespace paddock::config {

/// One farm's operating costs and prices, with the provenance of each.
struct FarmEconomics {
  std::string name;
  std::string display_name;
  std::string description;
  std::string region;
  std::string survey_class;
  std::string survey_year;

  core::OperatingCosts costs;
  core::Prices prices;

  /// Cash on day one. Not a survey figure - "how much should a farm start a
  /// season with" is a question about the farmer - so it is per hectare and
  /// marked placeholder.
  double opening_balance_per_hectare = 0.0;

  /// Every value with its status, in the order the file lists them, so a report
  /// can say which of a farm's costs rest on the survey and which do not.
  std::vector<std::pair<std::string, SourcedValue>> provenance;

  /// The weakest status across every value here. **What a whole-farm margin
  /// should be quoted with**, because a total is only as good as its worst
  /// line.
  [[nodiscard]] Provenance weakest_status() const;

  [[nodiscard]] std::string validation_error() const;
};

/// Loads an economics file. Throws ConfigError with a line-precise message.
[[nodiscard]] FarmEconomics load_economics(const std::string& path);

/// Every economics file in a directory, discovered rather than enumerated.
[[nodiscard]] std::vector<FarmEconomics> load_economics_directory(const std::string& directory);

/// A running business from a bundle and an economics file.
///
/// **One place, so the command line, the window and the tests describe the same
/// farm.** Before this, every caller that wanted a priced run built its own
/// flock and typed the twenty-two Beef + Lamb cost lines by hand - so a change
/// to the survey meant finding all of them, and a test could quietly disagree
/// with the data file it claimed to be testing.
///
/// The flock is split out of the bundle's first mob across the breeding ages,
/// because a bundle states a head count and not an age structure. That is an
/// assumption and it is stated: a real flock is not five equal classes, and a
/// scenario that cares should say so once flocks have a file of their own.
[[nodiscard]] FarmBusiness business_from(const ScenarioBundle& bundle,
                                         const FarmEconomics& economics);

}  // namespace paddock::config
