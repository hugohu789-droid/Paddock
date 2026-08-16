#include <cstddef>
#include <string>
#include <string_view>

#include <paddock/config/WeatherConfig.hpp>

#include "TomlSupport.hpp"

namespace paddock::config {

namespace {

constexpr std::size_t kMonthsInYear = 12;

core::MonthlyClimate read_month(const toml::table& month, const std::string& path,
                                std::size_t index) {
  const std::string context = "month " + std::to_string(index + 1);
  detail::reject_unknown_keys(
      month,
      {"mean_daily_max_c", "mean_daily_min_c", "wet_day_probability", "mean_wet_day_rainfall_mm",
       "rainfall_shape", "mean_solar_radiation_mj", "mean_wind_speed_m_per_s"},
      path, context);

  core::MonthlyClimate climate;
  climate.mean_daily_max_c = detail::require_double(month, "mean_daily_max_c", path);
  climate.mean_daily_min_c = detail::require_double(month, "mean_daily_min_c", path);
  climate.wet_day_probability = detail::require_double(month, "wet_day_probability", path);
  climate.mean_wet_day_rainfall_mm =
      detail::require_double(month, "mean_wet_day_rainfall_mm", path);
  climate.rainfall_shape = detail::require_double(month, "rainfall_shape", path);
  climate.mean_solar_radiation_mj = detail::require_double(month, "mean_solar_radiation_mj", path);
  climate.mean_wind_speed_m_per_s = detail::require_double(month, "mean_wind_speed_m_per_s", path);
  return climate;
}

core::SyntheticWeatherParameters read(const toml::table& root, const std::string& path) {
  detail::reject_unknown_keys(root, {"site", "variation", "month"}, path, "the file");

  const toml::table& site = detail::require_table(root, "site", path);
  detail::reject_unknown_keys(site, {"name", "licence", "latitude_degrees"}, path, "[site]");

  core::SyntheticWeatherParameters parameters;
  parameters.site_name = detail::require_string(site, "name", path);
  parameters.licence = detail::optional_string(site, "licence", "");
  parameters.latitude_degrees = detail::require_double(site, "latitude_degrees", path);

  const toml::table& variation = detail::require_table(root, "variation", path);
  detail::reject_unknown_keys(variation,
                              {"daily_temperature_sd_c", "radiation_variation_fraction",
                               "wet_day_radiation_fraction", "wind_variation_fraction"},
                              path, "[variation]");
  parameters.daily_temperature_sd_c =
      detail::require_double(variation, "daily_temperature_sd_c", path);
  parameters.radiation_variation_fraction =
      detail::require_double(variation, "radiation_variation_fraction", path);
  parameters.wet_day_radiation_fraction =
      detail::require_double(variation, "wet_day_radiation_fraction", path);
  parameters.wind_variation_fraction =
      detail::require_double(variation, "wind_variation_fraction", path);

  const toml::node* months = root.get("month");
  if (months == nullptr || !months->is_array_of_tables()) {
    detail::throw_in(root, path,
                     "a site needs twelve [[month]] tables, January first; none were found");
  }
  const toml::array& month_array = *months->as_array();
  if (month_array.size() != kMonthsInYear) {
    detail::throw_at(month_array, path,
                     "a site needs exactly twelve [[month]] tables, January first; found " +
                         std::to_string(month_array.size()));
  }
  for (std::size_t index = 0; index < kMonthsInYear; ++index) {
    parameters.months[index] = read_month(*month_array[index].as_table(), path, index);
  }

  // The same validation the core type applies at run time, reported here with a
  // place in the file rather than as an exception from the middle of a run.
  detail::require_valid(parameters.validation_error(), root, path);
  return parameters;
}

}  // namespace

core::SyntheticWeatherParameters parse_synthetic_weather(std::string_view text,
                                                         const std::string& path) {
  return read(detail::parse_text(text, path), path);
}

core::SyntheticWeatherParameters load_synthetic_weather(const std::string& path) {
  return read(detail::parse_file(path), path);
}

}  // namespace paddock::config
