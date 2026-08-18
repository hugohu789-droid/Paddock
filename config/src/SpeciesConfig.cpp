// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <paddock/config/SpeciesConfig.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

SpeciesDefinition read(const toml::table& root, const std::string& path) {
  detail::reject_unknown_keys(root, {"species", "energy", "typical"}, path, "the file");

  const toml::table& species = detail::require_table(root, "species", path);
  detail::reject_unknown_keys(species, {"name", "display_name", "description"}, path, "[species]");

  SpeciesDefinition definition;
  definition.name = detail::require_string(species, "name", path);
  definition.display_name = detail::optional_string(species, "display_name", definition.name);
  definition.description = detail::optional_string(species, "description", "");

  const toml::table& energy = detail::require_table(root, "energy", path);
  detail::reject_unknown_keys(
      energy,
      {"species_factor", "sex_factor", "standard_reference_weight_kg", "reference_weight_verified",
       "grazing_coefficient", "gain_energy_ceiling_mj_per_kg"},
      path, "[energy]");

  // class_id is the species name rather than a second field: two identifiers
  // for one thing is two things to keep in step.
  definition.energy.class_id = definition.name;
  definition.energy.species_factor = detail::require_double(energy, "species_factor", path);
  definition.energy.sex_factor = detail::require_double(energy, "sex_factor", path);
  definition.energy.standard_reference_weight_kg =
      detail::require_double(energy, "standard_reference_weight_kg", path);
  definition.energy.grazing_coefficient =
      detail::require_double(energy, "grazing_coefficient", path);
  definition.energy.gain_energy_ceiling_mj_per_kg =
      detail::require_double(energy, "gain_energy_ceiling_mj_per_kg", path);

  // Required rather than defaulted, because the honest answer is the one the
  // author had to type. A definition that silently defaulted to "verified"
  // would launder a guess into a published figure, and one that silently
  // defaulted to "not verified" would let a real citation go unrecorded.
  definition.reference_weight_verified =
      detail::require_bool(energy, "reference_weight_verified", path);

  const toml::table& typical = detail::require_table(root, "typical", path);
  detail::reject_unknown_keys(typical, {"liveweight_kg", "age_days"}, path, "[typical]");
  definition.typical_liveweight_kg = detail::require_double(typical, "liveweight_kg", path);
  definition.typical_age_days = detail::require_double(typical, "age_days", path);

  detail::require_valid(definition.validation_error(), species, path);
  return definition;
}

}  // namespace

std::string SpeciesDefinition::validation_error() const {
  if (name.empty()) {
    return "a species needs a 'name'";
  }

  // The energy parameters carry their own rules; borrow them rather than
  // restate them, so the two layers cannot drift apart. Not const: a const
  // local cannot be moved from on return.
  std::string energy_error = energy.validation_error();
  if (!energy_error.empty()) {
    return energy_error;
  }

  if (typical_liveweight_kg <= 0.0) {
    return name + ": a typical liveweight must be positive";
  }
  if (typical_age_days < 0.0) {
    return name + ": a typical age cannot be negative";
  }
  // An animal heavier than the mature weight of its own breed is a data
  // mistake, and it would run: the energy value of gain (TMC Eq. 44) simply
  // saturates, so the growth rate would look plausible and be wrong.
  if (typical_liveweight_kg > energy.standard_reference_weight_kg) {
    return name + ": a typical liveweight of " + std::to_string(typical_liveweight_kg) +
           " kg is above the standard reference weight of " +
           std::to_string(energy.standard_reference_weight_kg) +
           " kg, which is the mature weight of the breed";
  }
  return {};
}

SpeciesDefinition parse_species(std::string_view text, const std::string& path) {
  return read(detail::parse_text(text, path), path);
}

SpeciesDefinition load_species(const std::string& path) {
  return read(detail::parse_file(path), path);
}

std::vector<SpeciesDefinition> load_species_directory(const std::string& directory) {
  namespace fs = std::filesystem;

  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    throw ConfigError(directory, 1, 1, "not a directory of species definitions");
  }

  // Sorted before loading: a directory iterator's order is the filesystem's,
  // and a species list that changed order between machines would make every
  // report that prints it non-reproducible.
  std::vector<std::string> files;
  for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".toml") {
      files.push_back(entry.path().string());
    }
  }
  std::sort(files.begin(), files.end());

  std::vector<SpeciesDefinition> species;
  species.reserve(files.size());
  std::map<std::string, std::string> seen;
  for (const std::string& file : files) {
    SpeciesDefinition definition = load_species(file);
    const auto [existing, inserted] = seen.emplace(definition.name, file);
    if (!inserted) {
      throw ConfigError(file, 1, 1,
                        "species name '" + definition.name + "' is already used by " +
                            existing->second +
                            ". Names identify a class to a mob, so they have to be unique");
    }
    species.push_back(std::move(definition));
  }

  std::sort(species.begin(), species.end(),
            [](const SpeciesDefinition& lhs, const SpeciesDefinition& rhs) {
              return lhs.name < rhs.name;
            });
  return species;
}

}  // namespace paddock::config
