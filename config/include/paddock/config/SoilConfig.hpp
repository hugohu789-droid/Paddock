#pragma once

#include <string>
#include <string_view>

#include <paddock/core/SoilWater.hpp>

namespace paddock::config {

/// A named soil and the water parameters derived from it.
struct SoilDefinition {
  std::string name;
  core::SoilWaterParameters water;
};

/// Reads a soil definition.
///
/// ```toml
/// [soil]
/// name = "templeton_silt_loam_example"
///
/// [water]
/// # Either the profile's available water directly ...
/// total_available_water_mm = 120.0
/// # ... or the measurements it is computed from (FAO-56 Eq. 82), which is what
/// # S-map reports:
/// # field_capacity_fraction = 0.38
/// # wilting_point_fraction = 0.18
/// # rooting_depth_m = 0.6
/// depletion_fraction = 0.6
/// crop_coefficient = 0.95
/// runoff_fraction = 0.05
/// ```
///
/// Giving both forms, or neither, is an error: a file that silently prefers one
/// would let a corrected measurement have no effect.
[[nodiscard]] SoilDefinition load_soil(const std::string& path);

[[nodiscard]] SoilDefinition parse_soil(std::string_view text, const std::string& path);

}  // namespace paddock::config
