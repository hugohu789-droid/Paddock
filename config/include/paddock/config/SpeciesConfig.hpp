// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <paddock/core/AnimalEnergy.hpp>

namespace paddock::config {

/// One class of animal, as a data definition rather than a fixture.
///
/// CLAUDE.md's M3 asks for livestock "fully driven by species TOML", and this
/// is what that means in practice: a ewe and a dairy cow differ only in the
/// numbers a file gives them, so adding a deer or a dairy goat is a file rather
/// than a change to the model.
struct SpeciesDefinition {
  /// Stable identifier, snake_case, unique within a directory. This is what a
  /// mob or a scenario refers to; the display name may change freely.
  std::string name;
  std::string display_name;
  std::string description;

  /// The energy parameters, ready for core to use. `class_id` is set from
  /// `name`, so nothing downstream has to carry both.
  core::AnimalClassParameters energy;

  /// A starting weight for an animal of this class, and its age. Not part of
  /// the energy model - an animal's state is its own - but a farm has to start
  /// its stock somewhere, and putting it here keeps that out of code.
  double typical_liveweight_kg = 0.0;
  double typical_age_days = 0.0;

  /// False when the standard reference weight is a placeholder rather than a
  /// published figure. SRW drives the energy value of gain (TMC Eq. 45), so a
  /// definition that guesses it produces a plausible-looking growth rate with
  /// nothing behind it. Every definition shipped today is false; see
  /// docs/verify.md.
  bool reference_weight_verified = false;

  [[nodiscard]] std::string validation_error() const;
};

/// Reads one species definition.
///
/// ```toml
/// [species]
/// name = "sheep_ewe_romney"
/// display_name = "Romney ewe"
/// description = "A mature breeding ewe."
///
/// [energy]
/// # K, TMC Eq. 13. CSIRO (2007) and Nicol and Brookes (2007) both give 1.0
/// # for sheep - the one species factor the two sources agree on.
/// species_factor = 1.0
/// # S, TMC Eq. 14: 1.0 for females and castrates, 1.075 mixed, 1.15 entire
/// # males.
/// sex_factor = 1.0
/// # PLACEHOLDER - verify against a published mature weight for the breed.
/// standard_reference_weight_kg = 65.0
/// reference_weight_verified = false
/// # SpGraze, TMC Eq. 20.
/// grazing_coefficient = 0.0025
/// # k1, TMC Eq. 44: 16.5 for large lean cattle breeds, 20.3 otherwise.
/// gain_energy_ceiling_mj_per_kg = 20.3
///
/// [typical]
/// liveweight_kg = 60.0
/// age_days = 1200.0
/// ```
[[nodiscard]] SpeciesDefinition load_species(const std::string& path);

[[nodiscard]] SpeciesDefinition parse_species(std::string_view text, const std::string& path);

/// Every species definition in a directory, sorted by name.
///
/// Discovered rather than enumerated, like the farms: dropping a `.toml` into
/// `data/species/` adds an animal class and no code changes. Throws
/// ConfigError naming the file if any fails to load, or if two declare the same
/// `name`.
[[nodiscard]] std::vector<SpeciesDefinition> load_species_directory(const std::string& directory);

}  // namespace paddock::config
