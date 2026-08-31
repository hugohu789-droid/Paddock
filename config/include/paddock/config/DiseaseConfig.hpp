// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <paddock/config/Provenance.hpp>
#include <paddock/core/Mycotoxin.hpp>

namespace paddock::config {

/// A disease, as a data definition rather than a fixture.
///
/// CLAUDE.md's rule is that diseases are data: "Extending to a new species or
/// pest means adding a data file, not a class." **A TOML file nothing loads is
/// not a definition, it is a comment**, and that is what
/// `data/diseases/facial-eczema.toml` was until this loader existed - the
/// numbers lived twice, once in the file with their citations and once
/// hardcoded in the tests, and the two had already drifted apart before anyone
/// noticed. `DiseaseParametersMatchTheDataFile` is the test that stops it
/// happening again.
struct DiseaseDefinition {
  std::string name;
  std::string display_name;
  std::string description;

  /// The stock this disease is defined for. Not enforced by the model yet, and
  /// carried so that a run cannot silently apply a cattle disease to deer.
  std::vector<std::string> affects;

  /// The parameters, ready for core to use.
  core::MycotoxinParameters mycotoxin;

  /// Thresholds a report quotes but the model does not compute with. Kept
  /// because a spore count means nothing to a reader without them.
  double monitor_own_farm_spores_per_g = 0.0;
  double full_zinc_dose_spores_per_g = 0.0;
  double stand_down_spores_per_g = 0.0;
  double dangerous_spores_per_g = 0.0;

  /// What a count is worth knowing to about, from the same guide that gives the
  /// thresholds. A model that reports one clean number is claiming a precision
  /// the field test does not have.
  double variability_below_50k = 0.0;
  double variability_above_50k = 0.0;

  /// The dairy production response. **There is no sheep equivalent** - none
  /// that is quantified has been found - so a sheep run reports liver damage
  /// and claims nothing about production.
  double milksolids_kg_per_cow_per_day_per_100_ggt = 0.0;

  /// Where every one of those numbers came from, keyed by the name it has in
  /// the file.
  std::vector<std::pair<std::string, SourcedValue>> provenance;

  /// The `[sources]` table: identifier to full citation.
  std::vector<std::pair<std::string, std::string>> sources;

  /// Looks a parameter's provenance up by name. Absent means the file did not
  /// carry it.
  [[nodiscard]] const SourcedValue* provenance_for(const std::string& key) const;
};

/// Reads one disease definition.
///
/// Throws `ConfigError` with a `path:line:column:` prefix on anything wrong,
/// like every other loader here.
[[nodiscard]] DiseaseDefinition load_disease(const std::string& path);

/// Reads every `*.toml` in a directory, sorted by name so a run is reproducible.
[[nodiscard]] std::vector<DiseaseDefinition> load_disease_directory(const std::string& directory);

}  // namespace paddock::config
