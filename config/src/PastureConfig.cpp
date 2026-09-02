// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <string>
#include <string_view>

#include <paddock/config/PastureConfig.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

core::PastureSpeciesParameters read_species(const toml::table& table, const std::string& path,
                                            const std::string& context) {
  detail::reject_unknown_keys(
      table,
      {"species_id", "specific_leaf_area_m2_per_kg", "extinction_coefficient",
       "radiation_use_efficiency_g_per_mj", "base_temperature_c", "optimum_temperature_c",
       "maximum_temperature_c", "senescence_rate_per_day", "degree_days_per_leaf",
       "temperature_response_exponent", "leaves_per_tiller", "drought_turnover_threshold",
       "repro_season_max_allocation_increase", "repro_season_reference_latitude_degrees",
       "repro_season_timing_coefficient", "repro_season_duration_coefficient",
       "repro_season_shoulders_length_factor", "repro_season_onset_duration_factor",
       "repro_season_allocation_coefficient", "drought_turnover_effect_max",
       "drought_turnover_exponent", "residual_kg_dm_per_ha", "nitrogen_content_fraction",
       "nitrogen_fixation_kg_per_t_dm"},
      path, context);

  core::PastureSpeciesParameters species;
  species.species_id = detail::require_string(table, "species_id", path);
  species.specific_leaf_area_m2_per_kg =
      detail::require_double(table, "specific_leaf_area_m2_per_kg", path);
  species.extinction_coefficient = detail::require_double(table, "extinction_coefficient", path);
  species.radiation_use_efficiency_g_per_mj =
      detail::require_double(table, "radiation_use_efficiency_g_per_mj", path);
  species.base_temperature_c = detail::require_double(table, "base_temperature_c", path);
  species.optimum_temperature_c = detail::require_double(table, "optimum_temperature_c", path);
  species.maximum_temperature_c = detail::require_double(table, "maximum_temperature_c", path);
  species.senescence_rate_per_day = detail::require_double(table, "senescence_rate_per_day", path);

  // **Optional, and it changes the shape of the response rather than a number
  // in it.** Zero keeps the triangular response, so a sward file written before
  // this existed still loads and still runs the way it was written.
  species.temperature_response_exponent =
      detail::optional_double(table, "temperature_response_exponent", 0.0, path);

  // **Optional, and it should be stated.** With a leaf lifespan, senescence is
  // a season; without one it is a constant, which gets the annual total roughly
  // right and the season exactly wrong. Zero leaves the flat rate in charge, so
  // a sward file written before this existed still loads and still runs.
  species.degree_days_per_leaf = detail::optional_double(table, "degree_days_per_leaf", 0.0, path);
  species.leaves_per_tiller = detail::optional_double(table, "leaves_per_tiller", 3.0, path);

  // **How much faster leaf dies once the water runs out**, which is the half of
  // drought the leaf lifespan above does not carry. AgPasture's figures, the
  // same for its ryegrass and its white clover; a threshold of zero turns the
  // effect off for a sward file that wants the old behaviour.
  // **The spring flush.** Zero for the increase turns the season off entirely,
  // which is the default, so a sward written before this keeps its own seasons.
  // The rest are AgPasture's own defaults and are only read when it is on.
  species.repro_season_max_allocation_increase =
      detail::optional_double(table, "repro_season_max_allocation_increase", 0.0, path);
  species.repro_season_reference_latitude_degrees =
      detail::optional_double(table, "repro_season_reference_latitude_degrees", 41.0, path);
  species.repro_season_timing_coefficient =
      detail::optional_double(table, "repro_season_timing_coefficient", 0.14, path);
  species.repro_season_duration_coefficient =
      detail::optional_double(table, "repro_season_duration_coefficient", 2.0, path);
  species.repro_season_shoulders_length_factor =
      detail::optional_double(table, "repro_season_shoulders_length_factor", 1.0, path);
  species.repro_season_onset_duration_factor =
      detail::optional_double(table, "repro_season_onset_duration_factor", 0.60, path);
  species.repro_season_allocation_coefficient =
      detail::optional_double(table, "repro_season_allocation_coefficient", 0.10, path);

  species.drought_turnover_threshold =
      detail::optional_double(table, "drought_turnover_threshold", 0.6, path);
  species.drought_turnover_effect_max =
      detail::optional_double(table, "drought_turnover_effect_max", 1.0, path);
  species.drought_turnover_exponent =
      detail::optional_double(table, "drought_turnover_exponent", 2.0, path);
  species.residual_kg_dm_per_ha = detail::require_double(table, "residual_kg_dm_per_ha", path);
  species.nitrogen_content_fraction =
      detail::require_double(table, "nitrogen_content_fraction", path);
  // A grass fixes nothing, and saying so in every file would be noise.
  species.nitrogen_fixation_kg_per_t_dm =
      detail::optional_double(table, "nitrogen_fixation_kg_per_t_dm", 0.0, path);

  detail::require_valid(species.validation_error(), table, path);
  return species;
}

core::SwardParameters read(const toml::table& root, const std::string& path) {
  detail::reject_unknown_keys(root, {"sward", "grass", "legume"}, path, "the file");

  const toml::table& sward = detail::require_table(root, "sward", path);
  detail::reject_unknown_keys(sward, {"par_fraction", "decomposition_rate_per_day"}, path,
                              "[sward]");

  core::SwardParameters parameters;
  parameters.par_fraction = detail::require_double(sward, "par_fraction", path);
  parameters.decomposition_rate_per_day =
      detail::require_double(sward, "decomposition_rate_per_day", path);
  parameters.grass = read_species(detail::require_table(root, "grass", path), path, "[grass]");
  parameters.legume = read_species(detail::require_table(root, "legume", path), path, "[legume]");

  detail::require_valid(parameters.validation_error(), root, path);
  return parameters;
}

}  // namespace

core::SwardParameters parse_sward(std::string_view text, const std::string& path) {
  return read(detail::parse_text(text, path), path);
}

core::SwardParameters load_sward(const std::string& path) {
  return read(detail::parse_file(path), path);
}

}  // namespace paddock::config
