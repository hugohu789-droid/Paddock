// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <string>
#include <string_view>

#include <paddock/config/SoilConfig.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

SoilDefinition read(const toml::table& root, const std::string& path) {
  detail::reject_unknown_keys(root, {"soil", "water"}, path, "the file");

  const toml::table& soil = detail::require_table(root, "soil", path);
  detail::reject_unknown_keys(soil, {"name"}, path, "[soil]");

  SoilDefinition definition;
  definition.name = detail::require_string(soil, "name", path);

  const toml::table& water = detail::require_table(root, "water", path);
  detail::reject_unknown_keys(
      water,
      {"total_available_water_mm", "field_capacity_fraction", "wilting_point_fraction",
       "rooting_depth_m", "depletion_fraction", "crop_coefficient", "runoff_fraction"},
      path, "[water]");

  const bool has_direct = detail::has(water, "total_available_water_mm");
  const bool has_measured = detail::has(water, "field_capacity_fraction") ||
                            detail::has(water, "wilting_point_fraction") ||
                            detail::has(water, "rooting_depth_m");

  if (has_direct && has_measured) {
    detail::throw_in(water, path,
                     "give either 'total_available_water_mm' or the measurements it is computed "
                     "from (field_capacity_fraction, wilting_point_fraction, rooting_depth_m), "
                     "not both");
  }
  if (!has_direct && !has_measured) {
    detail::throw_in(water, path,
                     "missing available water: give 'total_available_water_mm', or "
                     "'field_capacity_fraction', 'wilting_point_fraction' and 'rooting_depth_m'");
  }

  if (has_direct) {
    definition.water.total_available_water_mm =
        detail::require_double(water, "total_available_water_mm", path);
  } else {
    const double field_capacity = detail::require_double(water, "field_capacity_fraction", path);
    const double wilting_point = detail::require_double(water, "wilting_point_fraction", path);
    const double rooting_depth = detail::require_double(water, "rooting_depth_m", path);
    if (wilting_point >= field_capacity) {
      detail::throw_in(water, path,
                       "'wilting_point_fraction' must be below 'field_capacity_fraction'");
    }
    definition.water.total_available_water_mm = core::SoilWaterParameters::total_available_water(
        field_capacity, wilting_point, rooting_depth);
  }

  definition.water.depletion_fraction = detail::require_double(water, "depletion_fraction", path);
  definition.water.crop_coefficient = detail::require_double(water, "crop_coefficient", path);
  definition.water.runoff_fraction = detail::require_double(water, "runoff_fraction", path);

  detail::require_valid(definition.water.validation_error(), water, path);
  return definition;
}

}  // namespace

SoilDefinition parse_soil(std::string_view text, const std::string& path) {
  return read(detail::parse_text(text, path), path);
}

SoilDefinition load_soil(const std::string& path) {
  return read(detail::parse_file(path), path);
}

}  // namespace paddock::config
