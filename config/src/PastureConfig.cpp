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
       "maximum_temperature_c", "senescence_rate_per_day", "residual_kg_dm_per_ha",
       "nitrogen_content_fraction", "nitrogen_fixation_kg_per_t_dm"},
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
