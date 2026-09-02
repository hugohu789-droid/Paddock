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

/// One `key = { value = ..., status = "...", source = "..." }` entry.
///
/// The status has no default. That is the whole point of the field: defaulting
/// it to `direct` would launder a guess into a published figure, and defaulting
/// it to `placeholder` would let a real citation go unrecorded. The author has
/// to say which it is.
SourcedValue read_sourced(const toml::table& parent, std::string_view key,
                          const std::string& path) {
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
  return sourced;
}

SpeciesDefinition read(const toml::table& root, const std::string& path) {
  detail::reject_unknown_keys(root, {"species", "energy", "intake", "reproduction", "typical"}, path,
                              "the file");

  const toml::table& species = detail::require_table(root, "species", path);
  detail::reject_unknown_keys(species, {"name", "display_name", "description", "kind"}, path,
                              "[species]");

  SpeciesDefinition definition;
  definition.name = detail::require_string(species, "name", path);
  definition.display_name = detail::optional_string(species, "display_name", definition.name);
  // Declared rather than read off the name. "sheep_ewe" begins "sheep_" today and a
  // convention that holds until somebody adds a species is not a fact about the animal.
  definition.energy.kind =
      core::animal_kind_from(detail::optional_string(species, "kind", "other"));
  definition.description = detail::optional_string(species, "description", "");

  const toml::table& energy = detail::require_table(root, "energy", path);
  detail::reject_unknown_keys(energy,
                              {"species_factor", "sex_factor", "standard_reference_weight_kg",
                               "grazing_coefficient", "gain_energy_ceiling_mj_per_kg"},
                              path, "[energy]");

  // class_id is the species name rather than a second field: two identifiers
  // for one thing is two things to keep in step.
  definition.energy.class_id = definition.name;

  definition.species_factor = read_sourced(energy, "species_factor", path);
  definition.sex_factor = read_sourced(energy, "sex_factor", path);
  definition.standard_reference_weight = read_sourced(energy, "standard_reference_weight_kg", path);
  definition.grazing_coefficient = read_sourced(energy, "grazing_coefficient", path);
  definition.gain_energy_ceiling = read_sourced(energy, "gain_energy_ceiling_mj_per_kg", path);

  definition.energy.species_factor = definition.species_factor.value;
  definition.energy.sex_factor = definition.sex_factor.value;
  definition.energy.standard_reference_weight_kg = definition.standard_reference_weight.value;
  definition.energy.grazing_coefficient = definition.grazing_coefficient.value;
  definition.energy.gain_energy_ceiling_mj_per_kg = definition.gain_energy_ceiling.value;

  // **[intake] is optional too, and its absence means "eats what it needs".**
  // Every animal file written before this one lacks the table, and a model that
  // demanded it would be asking somebody to invent three coefficients rather
  // than leave the old behaviour alone.
  if (const toml::table* intake = root["intake"].as_table()) {
    detail::reject_unknown_keys(*intake,
                                {"normal_weight_rate", "normal_weight_exponent",
                                 "normal_weight_blend", "condition_intake_limit",
                                 "lactation_peak_days", "lactation_curve_exponent",
                                 "lactation_peak_no_young", "lactation_peak_one_young",
                                 "lactation_peak_two_young", "lactation_peak_three_young",
                                 "appetite_scalar_per_day", "appetite_size_coefficient",
                                 "availability_rate_per_kg_dm", "grazing_time_increase",
                                 "grazing_time_rate_per_kg_dm"},
                                path, "[intake]");

    definition.normal_weight_rate = read_sourced(*intake, "normal_weight_rate", path);
    definition.normal_weight_exponent = read_sourced(*intake, "normal_weight_exponent", path);
    definition.normal_weight_blend = read_sourced(*intake, "normal_weight_blend", path);
    definition.condition_intake_limit = read_sourced(*intake, "condition_intake_limit", path);
    definition.lactation_peak_days = read_sourced(*intake, "lactation_peak_days", path);
    definition.lactation_curve_exponent =
        read_sourced(*intake, "lactation_curve_exponent", path);
    definition.lactation_peak_no_young = read_sourced(*intake, "lactation_peak_no_young", path);
    definition.lactation_peak_one_young = read_sourced(*intake, "lactation_peak_one_young", path);
    definition.lactation_peak_two_young = read_sourced(*intake, "lactation_peak_two_young", path);
    definition.lactation_peak_three_young =
        read_sourced(*intake, "lactation_peak_three_young", path);

    definition.energy.normal_weight_rate = definition.normal_weight_rate.value;
    definition.energy.normal_weight_exponent = definition.normal_weight_exponent.value;
    definition.energy.normal_weight_blend = definition.normal_weight_blend.value;
    definition.energy.condition_intake_limit = definition.condition_intake_limit.value;
    definition.energy.lactation_peak_days = definition.lactation_peak_days.value;
    definition.energy.lactation_curve_exponent = definition.lactation_curve_exponent.value;
    definition.energy.lactation_peak_no_young = definition.lactation_peak_no_young.value;
    definition.energy.lactation_peak_one_young = definition.lactation_peak_one_young.value;
    definition.energy.lactation_peak_two_young = definition.lactation_peak_two_young.value;
    definition.energy.lactation_peak_three_young = definition.lactation_peak_three_young.value;

    definition.appetite_scalar = read_sourced(*intake, "appetite_scalar_per_day", path);
    definition.appetite_size_coefficient =
        read_sourced(*intake, "appetite_size_coefficient", path);
    definition.energy.appetite_scalar_per_day = definition.appetite_scalar.value;
    definition.energy.appetite_size_coefficient = definition.appetite_size_coefficient.value;

    definition.intake_availability_rate = read_sourced(*intake, "availability_rate_per_kg_dm", path);
    definition.intake_grazing_time_increase = read_sourced(*intake, "grazing_time_increase", path);
    definition.intake_grazing_time_rate = read_sourced(*intake, "grazing_time_rate_per_kg_dm", path);

    definition.energy.intake_availability_rate_per_kg_dm =
        definition.intake_availability_rate.value;
    definition.energy.intake_grazing_time_increase = definition.intake_grazing_time_increase.value;
    definition.energy.intake_grazing_time_rate_per_kg_dm =
        definition.intake_grazing_time_rate.value;
  }

  // **[reproduction] is optional, and its absence means "does not breed".**
  // A wether has no gestation, and a file that had to state that would be
  // inviting somebody to state it wrongly.
  if (const toml::table* reproduction = root["reproduction"].as_table()) {
    detail::reject_unknown_keys(*reproduction,
                                {"gestation_length_days", "milk_fat_percent",
                                 "milk_protein_percent", "breed_effect", "suckling_weeks"},
                                path, "[reproduction]");

    definition.gestation_length_days = read_sourced(*reproduction, "gestation_length_days", path);
    definition.milk_fat_percent = read_sourced(*reproduction, "milk_fat_percent", path);
    definition.milk_protein_percent = read_sourced(*reproduction, "milk_protein_percent", path);
    definition.breed_effect = read_sourced(*reproduction, "breed_effect", path);

    definition.energy.gestation_length_days = definition.gestation_length_days.value;
    definition.energy.milk_fat_percent = definition.milk_fat_percent.value;
    definition.energy.milk_protein_percent = definition.milk_protein_percent.value;
    definition.energy.breed_effect = definition.breed_effect.value;

    // Optional inside an optional table: a class whose young never suckle - a
    // hogget, a wether - says nothing and gets zero.
    if (reproduction->contains("suckling_weeks")) {
      definition.suckling_weeks = read_sourced(*reproduction, "suckling_weeks", path);
      definition.energy.suckling_weeks = definition.suckling_weeks.value;
    }
  }

  const toml::table& typical = detail::require_table(root, "typical", path);
  detail::reject_unknown_keys(typical, {"liveweight_kg", "age_days", "stock_units"}, path,
                              "[typical]");
  definition.typical_liveweight_kg = detail::require_double(typical, "liveweight_kg", path);
  definition.typical_age_days = detail::require_double(typical, "age_days", path);

  // **Optional, and zero means "not rated" rather than "worth nothing".** A
  // class with no published conversion should make a farm decline to report a
  // stocking rate, not report one that is short by however many head of that
  // class it carries.
  definition.stock_units = detail::optional_double(typical, "stock_units", 0.0, path);

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

std::vector<const SourcedValue*> SpeciesDefinition::sourced_values() const {
  return {&species_factor, &sex_factor, &standard_reference_weight, &grazing_coefficient,
          &gain_energy_ceiling};
}

Provenance SpeciesDefinition::weakest_status() const {
  // The enumerators run from strongest to weakest, so the largest wins.
  Provenance weakest = Provenance::Direct;
  for (const SourcedValue* value : sourced_values()) {
    if (static_cast<int>(value->status) > static_cast<int>(weakest)) {
      weakest = value->status;
    }
  }
  return weakest;
}

bool SpeciesDefinition::fully_evidenced() const {
  const std::vector<const SourcedValue*> values = sourced_values();
  return std::all_of(values.begin(), values.end(),
                     [](const SourcedValue* value) { return value->is_evidence(); });
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
