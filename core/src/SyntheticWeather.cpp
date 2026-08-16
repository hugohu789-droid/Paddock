#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <paddock/core/Distributions.hpp>
#include <paddock/core/Rng.hpp>
#include <paddock/core/Sha256.hpp>
#include <paddock/core/SyntheticWeather.hpp>

namespace paddock::core {

namespace {

constexpr std::size_t kMonthsInYear = 12;

/// Which two months a date sits between, and how far along it is.
///
/// Monthly normals describe the middle of a month, so using them directly makes
/// the weather jump on the first of every month. Blending between the two
/// nearest month centres costs nothing and removes a discontinuity that would
/// otherwise show up in the growth curve.
struct MonthBlend {
  std::size_t before = 0;
  std::size_t after = 0;
  double weight = 0.0;  ///< 0 at the centre of `before`, 1 at the centre of `after`
};

MonthBlend blend_for(const Date& date) noexcept {
  const auto this_month = static_cast<std::size_t>(date.month - 1);
  const double length = days_in_month(date.year, date.month);
  const double centre = (length + 1.0) / 2.0;
  const double day_of_month = date.day;

  if (day_of_month >= centre) {
    const int next_month = date.month == 12 ? 1 : date.month + 1;
    const int next_year = date.month == 12 ? date.year + 1 : date.year;
    const double next_centre = (days_in_month(next_year, next_month) + 1.0) / 2.0;
    const double gap = (length - centre) + next_centre;
    return MonthBlend{this_month, (this_month + 1) % kMonthsInYear, (day_of_month - centre) / gap};
  }

  const int previous_month = date.month == 1 ? 12 : date.month - 1;
  const int previous_year = date.month == 1 ? date.year - 1 : date.year;
  const double previous_length = days_in_month(previous_year, previous_month);
  const double previous_centre = (previous_length + 1.0) / 2.0;
  const double gap = (previous_length - previous_centre) + centre;
  return MonthBlend{(this_month + kMonthsInYear - 1) % kMonthsInYear, this_month,
                    ((previous_length - previous_centre) + day_of_month) / gap};
}

double blended(const MonthBlend& blend, double before_value, double after_value) noexcept {
  return ((1.0 - blend.weight) * before_value) + (blend.weight * after_value);
}

void append(std::ostringstream& out, double value) {
  out << value << ';';
}

}  // namespace

std::string SyntheticWeatherParameters::validation_error() const {
  if (site_name.empty()) {
    return "site_name must not be empty";
  }
  if (latitude_degrees < -90.0 || latitude_degrees > 90.0) {
    return "latitude_degrees must be between -90 and 90";
  }
  if (daily_temperature_sd_c < 0.0) {
    return "daily_temperature_sd_c must not be negative";
  }
  if (radiation_variation_fraction < 0.0) {
    return "radiation_variation_fraction must not be negative";
  }
  if (wind_variation_fraction < 0.0) {
    return "wind_variation_fraction must not be negative";
  }
  if (wet_day_radiation_fraction < 0.0 || wet_day_radiation_fraction > 1.0) {
    return "wet_day_radiation_fraction must be between 0 and 1";
  }

  for (std::size_t month = 0; month < months.size(); ++month) {
    const MonthlyClimate& climate = months[month];
    const std::string where = " (month " + std::to_string(month + 1) + ")";
    if (climate.mean_daily_max_c < climate.mean_daily_min_c) {
      return "mean_daily_max_c is below mean_daily_min_c" + where;
    }
    if (climate.wet_day_probability < 0.0 || climate.wet_day_probability > 1.0) {
      return "wet_day_probability must be between 0 and 1" + where;
    }
    if (climate.mean_wet_day_rainfall_mm < 0.0) {
      return "mean_wet_day_rainfall_mm must not be negative" + where;
    }
    if (climate.wet_day_probability > 0.0 && climate.rainfall_shape <= 0.0) {
      return "rainfall_shape must be positive where rain is possible" + where;
    }
    if (climate.mean_solar_radiation_mj < 0.0) {
      return "mean_solar_radiation_mj must not be negative" + where;
    }
    if (climate.mean_wind_speed_m_per_s < 0.0) {
      return "mean_wind_speed_m_per_s must not be negative" + where;
    }
  }
  return {};
}

std::string SyntheticWeatherParameters::fingerprint() const {
  // Seventeen significant digits round-trips a double exactly, so the rendering
  // is a faithful and stable image of the parameter set.
  std::ostringstream canonical;
  canonical.precision(17);
  canonical << site_name << ';' << licence << ';';
  append(canonical, latitude_degrees);
  append(canonical, daily_temperature_sd_c);
  append(canonical, radiation_variation_fraction);
  append(canonical, wet_day_radiation_fraction);
  append(canonical, wind_variation_fraction);
  for (const MonthlyClimate& climate : months) {
    append(canonical, climate.mean_daily_max_c);
    append(canonical, climate.mean_daily_min_c);
    append(canonical, climate.wet_day_probability);
    append(canonical, climate.mean_wet_day_rainfall_mm);
    append(canonical, climate.rainfall_shape);
    append(canonical, climate.mean_solar_radiation_mj);
    append(canonical, climate.mean_wind_speed_m_per_s);
  }
  return Sha256::hex_of(canonical.str());
}

SyntheticWeatherSource::SyntheticWeatherSource(SyntheticWeatherParameters parameters,
                                               std::uint64_t master_seed)
    : parameters_(std::move(parameters)),
      master_seed_(master_seed),
      fingerprint_(parameters_.fingerprint()) {}

SourceDescription SyntheticWeatherSource::describe() const {
  return SourceDescription{
      "synthetic:" + parameters_.site_name,
      parameters_.licence.empty() ? "generated data, no licence applies" : parameters_.licence,
      "any date, generated from monthly climate normals for " + parameters_.site_name, "on demand"};
}

ConnectionStatus SyntheticWeatherSource::test_connection() const {
  const std::string error = parameters_.validation_error();
  if (!error.empty()) {
    return ConnectionStatus::unavailable("synthetic weather parameters are invalid: " + error);
  }
  return ConnectionStatus::available("generator ready for " + parameters_.site_name +
                                     ", parameter fingerprint " + fingerprint_.substr(0, 12));
}

DailyWeather SyntheticWeatherSource::day(const Date& date) const {
  // Keyed by the date, not by a position in a loop: this is what makes any
  // subrange of a run reproduce the numbers the whole run would have seen.
  std::mt19937_64 engine(derive_seed(master_seed_, Subsystem::Weather,
                                     static_cast<std::uint64_t>(date.days_since_epoch())));

  const MonthBlend blend = blend_for(date);
  const MonthlyClimate& before = parameters_.months[blend.before];
  const MonthlyClimate& after = parameters_.months[blend.after];

  DailyWeather weather;
  weather.date = date;

  // The draw order below is part of the reproducibility contract: changing it
  // changes every generated year.
  const double temperature_offset = normal(engine, 0.0, parameters_.daily_temperature_sd_c);
  weather.max_air_temperature_c =
      blended(blend, before.mean_daily_max_c, after.mean_daily_max_c) + temperature_offset;
  weather.min_air_temperature_c =
      blended(blend, before.mean_daily_min_c, after.mean_daily_min_c) + temperature_offset;

  const double wet_probability =
      blended(blend, before.wet_day_probability, after.wet_day_probability);
  const bool wet = bernoulli(engine, wet_probability);
  if (wet) {
    const double mean_depth =
        blended(blend, before.mean_wet_day_rainfall_mm, after.mean_wet_day_rainfall_mm);
    const double shape = blended(blend, before.rainfall_shape, after.rainfall_shape);
    if (shape > 0.0 && mean_depth > 0.0) {
      weather.rainfall_mm = gamma(engine, shape, mean_depth / shape);
    }
  }

  const double radiation_noise = normal(engine, 0.0, parameters_.radiation_variation_fraction);
  const double clear_sky =
      blended(blend, before.mean_solar_radiation_mj, after.mean_solar_radiation_mj);
  const double cloud_factor = wet ? parameters_.wet_day_radiation_fraction : 1.0;
  // Heavy cloud and a wide scatter can drive the draw below zero; a negative
  // radiation would put energy into the evapotranspiration term.
  weather.solar_radiation_mj_per_m2 =
      std::max(0.0, clear_sky * cloud_factor * (1.0 + radiation_noise));

  const double wind_noise = normal(engine, 0.0, parameters_.wind_variation_fraction);
  weather.wind_speed_m_per_s =
      std::max(0.0, blended(blend, before.mean_wind_speed_m_per_s, after.mean_wind_speed_m_per_s) *
                        (1.0 + wind_noise));

  return weather;
}

WeatherSeries SyntheticWeatherSource::fetch(const DateRange& range) const {
  if (!range.is_valid()) {
    throw std::invalid_argument("SyntheticWeatherSource: invalid date range " +
                                range.first.to_iso_string() + " to " + range.last.to_iso_string());
  }
  const std::string error = parameters_.validation_error();
  if (!error.empty()) {
    throw std::invalid_argument("SyntheticWeatherSource: " + error);
  }

  WeatherSeries series;
  series.provenance =
      Provenance{"synthetic", parameters_.site_name, fingerprint_, parameters_.licence};
  series.records.reserve(static_cast<std::size_t>(range.day_count()));
  for (std::int64_t day_index = range.first.days_since_epoch();
       day_index <= range.last.days_since_epoch(); ++day_index) {
    series.records.push_back(day(Date::from_days_since_epoch(day_index)));
  }
  return series;
}

}  // namespace paddock::core
