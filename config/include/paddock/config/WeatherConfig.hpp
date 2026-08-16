#pragma once

#include <string>
#include <string_view>

#include <paddock/core/SyntheticWeather.hpp>

namespace paddock::config {

/// Reads a synthetic weather site definition.
///
/// ```toml
/// [site]
/// name = "canterbury_plains_example"
/// licence = "climate normals from NIWA; see docs/verify.md"
/// latitude_degrees = -43.5
///
/// [variation]
/// daily_temperature_sd_c = 2.0
/// radiation_variation_fraction = 0.1
/// wet_day_radiation_fraction = 0.5
/// wind_variation_fraction = 0.2
///
/// [[month]]   # exactly twelve, January first
/// mean_daily_max_c = 22.5
/// mean_daily_min_c = 12.0
/// wet_day_probability = 0.25
/// mean_wet_day_rainfall_mm = 8.0
/// rainfall_shape = 0.8
/// mean_solar_radiation_mj = 24.0
/// mean_wind_speed_m_per_s = 3.0
/// ```
///
/// Throws ConfigError, whose message names the file, line and column.
[[nodiscard]] core::SyntheticWeatherParameters load_synthetic_weather(const std::string& path);

/// The same, from text already in memory. `path` is used only in messages.
[[nodiscard]] core::SyntheticWeatherParameters parse_synthetic_weather(std::string_view text,
                                                                       const std::string& path);

}  // namespace paddock::config
