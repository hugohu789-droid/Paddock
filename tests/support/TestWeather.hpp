#pragma once

#include <cstddef>

#include <paddock/core/SyntheticWeather.hpp>

namespace paddock::test_support {

/// A synthetic site used by several suites.
///
/// This is a fixture, not a calibration: the numbers are chosen to make
/// assertions legible - a warm month, a cold month, a fixed wet-day
/// probability - and carry no claim about any real place. Real site parameters
/// live in TOML under data/ and cite NIWA climate normals; see docs/verify.md.
inline core::SyntheticWeatherParameters test_site_parameters() {
  core::SyntheticWeatherParameters parameters;
  parameters.site_name = "test_site";
  parameters.licence = "test fixture";
  parameters.latitude_degrees = -43.5;  // Southern Hemisphere: January is summer
  parameters.daily_temperature_sd_c = 2.0;
  parameters.radiation_variation_fraction = 0.1;
  parameters.wet_day_radiation_fraction = 0.5;
  parameters.wind_variation_fraction = 0.2;

  for (std::size_t month = 0; month < 12; ++month) {
    core::MonthlyClimate& climate = parameters.months[month];
    // Warm around January and December, cold around June and July: the shape of
    // a Southern Hemisphere year, not the detail of any real one.
    const bool summer = month <= 1 || month == 11;
    const bool winter = month >= 5 && month <= 7;
    climate.mean_daily_max_c = 16.0;
    climate.mean_daily_min_c = 6.0;
    climate.mean_solar_radiation_mj = 14.0;
    if (summer) {
      climate.mean_daily_max_c = 22.0;
      climate.mean_daily_min_c = 12.0;
      climate.mean_solar_radiation_mj = 24.0;
    } else if (winter) {
      climate.mean_daily_max_c = 10.0;
      climate.mean_daily_min_c = 1.0;
      climate.mean_solar_radiation_mj = 6.0;
    }
    climate.wet_day_probability = 0.25;
    climate.mean_wet_day_rainfall_mm = 8.0;
    climate.rainfall_shape = 0.8;
    climate.mean_wind_speed_m_per_s = 3.0;
  }
  return parameters;
}

}  // namespace paddock::test_support
