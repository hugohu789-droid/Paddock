// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <paddock/config/DiseaseConfig.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

/// One `key = { value = ..., status = "...", source = "..." }` entry, recorded
/// as it is read so that a report can say where every number came from.
double read_sourced(const toml::table& parent, std::string_view key, const std::string& path,
                    DiseaseDefinition& into) {
  const toml::table& entry = detail::require_table(parent, key, path);
  detail::reject_unknown_keys(entry, {"value", "status", "source"}, path,
                              "'" + std::string(key) + "'");

  SourcedValue sourced;
  sourced.value = detail::require_double(entry, "value", path);

  const std::string status = detail::require_string(entry, "status", path);
  if (!provenance_from_string(status, sourced.status)) {
    detail::throw_in(entry, path,
                     "'" + std::string(key) + "' has status '" + status +
                         "'. It must be one of: direct, derived, verify, fitted, placeholder");
  }
  sourced.source_id = detail::optional_string(entry, "source", "");

  const std::string error = sourced.validation_error(std::string(key));
  if (!error.empty()) {
    detail::throw_in(entry, path, error);
  }

  into.provenance.emplace_back(std::string(key), sourced);
  return sourced.value;
}

DiseaseDefinition read(const toml::table& root, const std::string& path) {
  detail::reject_unknown_keys(root,
                              {"disease", "sporulation", "counting", "thresholds", "toxin", "liver",
                               "production", "sources"},
                              path, "the file");

  DiseaseDefinition definition;

  const toml::table& disease = detail::require_table(root, "disease", path);
  detail::reject_unknown_keys(disease, {"name", "display_name", "description", "affects"}, path,
                              "[disease]");
  definition.name = detail::require_string(disease, "name", path);
  definition.display_name = detail::optional_string(disease, "display_name", definition.name);
  definition.description = detail::optional_string(disease, "description", "");

  if (const toml::array* affects = disease["affects"].as_array()) {
    for (const toml::node& entry : *affects) {
      if (const auto kind = entry.value<std::string>()) {
        definition.affects.push_back(*kind);
      }
    }
  }

  core::MycotoxinParameters& toxin = definition.mycotoxin;

  const toml::table& sporulation = detail::require_table(root, "sporulation", path);
  toxin.grass_minimum_temperature_c =
      read_sourced(sporulation, "grass_minimum_temperature_c", path, definition);
  toxin.consecutive_nights =
      static_cast<int>(read_sourced(sporulation, "consecutive_nights", path, definition));
  toxin.rainfall_mm_per_48h = read_sourced(sporulation, "rainfall_mm_per_48h", path, definition);
  toxin.rise_per_favourable_day =
      read_sourced(sporulation, "rise_per_favourable_day", path, definition);
  toxin.decay_per_unfavourable_day =
      read_sourced(sporulation, "decay_per_unfavourable_day", path, definition);
  toxin.background_spores_per_g =
      read_sourced(sporulation, "background_spores_per_g", path, definition);

  const toml::table& counting = detail::require_table(root, "counting", path);
  definition.variability_below_50k =
      read_sourced(counting, "variability_below_50k", path, definition);
  definition.variability_above_50k =
      read_sourced(counting, "variability_above_50k", path, definition);
  read_sourced(counting, "between_paddock_spread", path, definition);

  const toml::table& thresholds = detail::require_table(root, "thresholds", path);
  definition.monitor_own_farm_spores_per_g =
      read_sourced(thresholds, "monitor_own_farm", path, definition);
  definition.full_zinc_dose_spores_per_g =
      read_sourced(thresholds, "full_zinc_dose", path, definition);
  definition.stand_down_spores_per_g = read_sourced(thresholds, "stand_down", path, definition);
  read_sourced(thresholds, "stand_down_weeks", path, definition);
  definition.dangerous_spores_per_g = read_sourced(thresholds, "dangerous", path, definition);

  const toml::table& toxin_table = detail::require_table(root, "toxin", path);
  toxin.picograms_per_spore = read_sourced(toxin_table, "picograms_per_spore", path, definition);
  read_sourced(toxin_table, "picograms_per_spore_low", path, definition);
  read_sourced(toxin_table, "acute_severe_mg_per_kg", path, definition);

  const toml::table& liver = detail::require_table(root, "liver", path);
  toxin.reactor_spore_days = read_sourced(liver, "reactor_spore_days", path, definition);
  toxin.background_spore_days_per_year =
      read_sourced(liver, "background_spore_days_per_year", path, definition);
  toxin.clearance_per_day = read_sourced(liver, "clearance_per_day", path, definition);
  toxin.reactor_ggt_iu_per_l = read_sourced(liver, "reactor_ggt_iu_per_l", path, definition);
  toxin.liver_injury_intercept = read_sourced(liver, "liver_injury_intercept", path, definition);
  toxin.liver_injury_ln_ggt_slope =
      read_sourced(liver, "liver_injury_ln_ggt_slope", path, definition);
  read_sourced(liver, "moderate_damage_ggt_iu_per_l", path, definition);
  toxin.clinical_fraction_of_affected =
      read_sourced(liver, "clinical_fraction_of_affected", path, definition);

  const toml::table& production = detail::require_table(root, "production", path);
  definition.milksolids_kg_per_cow_per_day_per_100_ggt =
      read_sourced(production, "milksolids_kg_per_cow_per_day_per_100_ggt", path, definition);
  read_sourced(production, "milksolids_ci_low", path, definition);
  read_sourced(production, "milksolids_ci_high", path, definition);

  if (const toml::table* sources = root["sources"].as_table()) {
    for (const auto& [key, node] : *sources) {
      if (const auto text = node.value<std::string>()) {
        definition.sources.emplace_back(std::string(key.str()), *text);
      }
    }
    std::sort(definition.sources.begin(), definition.sources.end());
  }

  // **Every citation a parameter names has to exist.** A source identifier with
  // no entry in [sources] is a number that looks cited and is not.
  for (const auto& [key, sourced] : definition.provenance) {
    if (sourced.source_id.empty()) {
      continue;
    }
    const bool known =
        std::any_of(definition.sources.begin(), definition.sources.end(),
                    [&](const auto& entry) { return entry.first == sourced.source_id; });
    if (!known) {
      throw ConfigError(path, 1, 1,
                        "'" + key + "' cites source '" + sourced.source_id +
                            "', which [sources] does not define");
    }
  }

  const std::string invalid = definition.mycotoxin.invalid_reason();
  if (!invalid.empty()) {
    throw ConfigError(path, 1, 1, invalid);
  }
  return definition;
}

}  // namespace

const SourcedValue* DiseaseDefinition::provenance_for(const std::string& key) const {
  const auto found = std::find_if(provenance.begin(), provenance.end(),
                                  [&](const auto& entry) { return entry.first == key; });
  return found == provenance.end() ? nullptr : &found->second;
}

DiseaseDefinition load_disease(const std::string& path) {
  return read(detail::parse_file(path), path);
}

std::vector<DiseaseDefinition> load_disease_directory(const std::string& directory) {
  namespace fs = std::filesystem;

  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    throw ConfigError(directory, 1, 1, "not a directory of disease definitions");
  }

  // Sorted before loading, for the same reason the species loader sorts: a
  // directory iterator's order is the filesystem's, and a report that printed
  // them would stop being reproducible between machines.
  std::vector<std::string> files;
  for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".toml") {
      files.push_back(entry.path().string());
    }
  }
  std::sort(files.begin(), files.end());

  std::vector<DiseaseDefinition> diseases;
  diseases.reserve(files.size());
  for (const std::string& file : files) {
    diseases.push_back(load_disease(file));
  }
  return diseases;
}

}  // namespace paddock::config
