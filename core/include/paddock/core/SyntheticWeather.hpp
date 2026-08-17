// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <paddock/core/Weather.hpp>

namespace paddock::core {

/// Climate normals for one calendar month at one site.
///
/// There is not a single number in this file: every value comes from a site's
/// TOML definition, which cites NIWA climate normals or is marked PLACEHOLDER.
/// A generator with defaults baked into the code is a generator that quietly
/// simulates the wrong country.
struct MonthlyClimate {
  double mean_daily_max_c = 0.0;
  double mean_daily_min_c = 0.0;
  double wet_day_probability = 0.0;       ///< Fraction of days with measurable rain
  double mean_wet_day_rainfall_mm = 0.0;  ///< Mean depth on days that are wet
  double rainfall_shape = 0.0;            ///< Gamma shape for wet-day depth
  double mean_solar_radiation_mj = 0.0;   ///< MJ per m2 per day
  double mean_wind_speed_m_per_s = 0.0;
};

struct SyntheticWeatherParameters {
  std::string site_name;
  std::string licence;
  double latitude_degrees = 0.0;  ///< Negative in New Zealand
  std::array<MonthlyClimate, 12> months{};

  double daily_temperature_sd_c = 0.0;        ///< Day-to-day scatter about the normal
  double radiation_variation_fraction = 0.0;  ///< Relative scatter in daily radiation
  double wet_day_radiation_fraction = 1.0;    ///< Cloud effect: radiation on wet days
  double wind_variation_fraction = 0.0;

  /// Empty when the parameters are usable, otherwise an error naming the field.
  [[nodiscard]] std::string validation_error() const;

  /// SHA-256 over a canonical rendering of every field. Recorded in provenance
  /// so a scenario bundle pins the parameter set, not just the seed.
  [[nodiscard]] std::string fingerprint() const;
};

/// The synthetic adapter of the weather port: a generator, not a model.
///
/// Each day's weather is drawn from an engine seeded by the master seed and
/// **the date itself**, never by position in a loop. Fetching January and
/// February separately gives exactly the same numbers as fetching the pair, and
/// a scenario that starts mid-year sees the same weather it would have seen in
/// a full-year run.
class SyntheticWeatherSource final : public WeatherSource {
 public:
  SyntheticWeatherSource(SyntheticWeatherParameters parameters, std::uint64_t master_seed);

  [[nodiscard]] SourceDescription describe() const override;
  [[nodiscard]] ConnectionStatus test_connection() const override;
  [[nodiscard]] WeatherSeries fetch(const DateRange& range) const override;

  /// One day, independent of any other. Public because it is the whole
  /// determinism argument: the series is a function of the dates, nothing else.
  [[nodiscard]] DailyWeather day(const Date& date) const;

  [[nodiscard]] const SyntheticWeatherParameters& parameters() const noexcept {
    return parameters_;
  }

  [[nodiscard]] std::uint64_t master_seed() const noexcept { return master_seed_; }

 private:
  SyntheticWeatherParameters parameters_;
  std::uint64_t master_seed_;
  std::string fingerprint_;
};

}  // namespace paddock::core
