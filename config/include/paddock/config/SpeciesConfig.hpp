// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <paddock/config/Provenance.hpp>
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

  /// What one head of this class counts as in stock units.
  ///
  /// **The unit New Zealand farms are compared in, and the one this project
  /// nearly got wrong.** A stock unit is Parker's (1998) base ewe: 55 kg,
  /// weaning one lamb, eating 550 kg DM a year. Every published conversion in
  /// his Table 1 puts a breeding ewe at 1.0 - the Meat and Wool Board's
  /// Economic Service, whose survey became Beef + Lamb's, and MAF both do.
  ///
  /// It lives in the species file rather than in code because it is exactly the
  /// kind of number that gets asserted to make a comparison come out: this
  /// repository's economics file claimed 1.35 a ewe, with no source, so that a
  /// 5.2 SU/ha farm would read as the 7.74 of the class it was being priced
  /// against. See docs/validation/verify.md, E53.
  ///
  /// Zero means the class has no rating stated, and a farm carrying it reports
  /// no stocking rate rather than a wrong one.
  double stock_units = 0.0;

  /// Where each of those numbers came from.
  ///
  /// `energy` above holds the plain values because that is what the model
  /// consumes; these hold the account. Keeping them apart is the point: an
  /// equation is the same equation whether its inputs are published or guessed,
  /// so the difference has to live somewhere a report can read.
  SourcedValue species_factor;
  SourcedValue sex_factor;
  SourcedValue standard_reference_weight;
  SourcedValue grazing_coefficient;

  /// GrazPlan's three availability coefficients, from an optional [intake]
  /// table. Absent means this class eats its requirement whatever is standing,
  /// which is what every animal in this model did before they existed.
  SourcedValue normal_weight_rate;
  SourcedValue normal_weight_exponent;
  SourcedValue normal_weight_blend;
  SourcedValue condition_intake_limit;
  SourcedValue lactation_peak_days;
  SourcedValue lactation_curve_exponent;
  SourcedValue lactation_peak_no_young;
  SourcedValue lactation_peak_one_young;
  SourcedValue lactation_peak_two_young;
  SourcedValue lactation_peak_three_young;
  SourcedValue appetite_scalar;
  SourcedValue appetite_size_coefficient;
  SourcedValue intake_availability_rate;
  SourcedValue intake_grazing_time_increase;
  SourcedValue intake_grazing_time_rate;
  SourcedValue gain_energy_ceiling;

  /// Reproduction. Optional: a class that does not breed - a wether, a store
  /// lamb - simply omits the whole `[reproduction]` table, and a gestation
  /// length of zero switches both the pregnancy and lactation terms off.
  SourcedValue gestation_length_days;
  SourcedValue milk_fat_percent;
  SourcedValue milk_protein_percent;
  SourcedValue breed_effect;
  SourcedValue suckling_weeks;

  /// Every sourced value in this definition, for a caller that wants to report
  /// on the lot rather than name them one at a time.
  [[nodiscard]] std::vector<const SourcedValue*> sourced_values() const;

  /// The weakest status any of these numbers carries. A species is only as
  /// quotable as its least supported parameter, so a result depending on the
  /// whole definition should be reported against this rather than against
  /// whichever value the reader happened to look at.
  [[nodiscard]] Provenance weakest_status() const;

  /// True when every number rests on something published.
  [[nodiscard]] bool fully_evidenced() const;

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
/// # Every value is an inline table: the number, where it sits on the evidence
/// # scale, and what it cites. `source` is an identifier into
/// # data/calibration/livestock/sources.toml.
/// species_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
/// sex_factor = { value = 1.0, status = "direct", source = "tmc_animal_me" }
/// standard_reference_weight_kg = { value = 65.0, status = "verify" }
/// grazing_coefficient = { value = 0.0025, status = "direct", source = "tmc_animal_me" }
/// gain_energy_ceiling_mj_per_kg = { value = 20.3, status = "direct", source = "tmc_animal_me" }
///
/// [typical]
/// liveweight_kg = 60.0
/// age_days = 1200.0
/// ```
///
/// The four statuses are `direct`, `derived`, `verify` and `placeholder`; see
/// Provenance.hpp. A `direct` or `derived` value must cite a source, because a
/// citation with nothing to cite is decoration. `verify` and `placeholder` need
/// not, being by definition not yet attached to one.
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
