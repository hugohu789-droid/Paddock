#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <paddock/core/SimulationClock.hpp>

namespace paddock::core {

/// One day of weather at one site. These are the drivers every process
/// downstream reads: rainfall feeds the soil water bucket, temperature drives
/// pasture growth, radiation and the temperature range drive evapotranspiration.
struct DailyWeather {
  Date date;
  double rainfall_mm = 0.0;
  double min_air_temperature_c = 0.0;
  double max_air_temperature_c = 0.0;
  double solar_radiation_mj_per_m2 = 0.0;
  double wind_speed_m_per_s = 0.0;

  /// The mean of the daily extremes, which is what growth models expect.
  [[nodiscard]] double mean_air_temperature_c() const noexcept;

  /// Physically possible, not necessarily plausible: no negative rainfall,
  /// radiation or wind, and a maximum that is not below the minimum.
  [[nodiscard]] bool is_valid() const noexcept;
};

/// An inclusive span of dates.
struct DateRange {
  Date first;
  Date last;

  [[nodiscard]] static DateRange calendar_year(int year) noexcept;

  /// Number of days in the span; zero when the range is inverted.
  [[nodiscard]] std::int64_t day_count() const noexcept;
  [[nodiscard]] bool contains(const Date& date) const noexcept;
  [[nodiscard]] bool is_valid() const noexcept;
};

/// Where a series came from, carried with the data so that a scenario bundle
/// can record it and a reviewer can check it.
struct Provenance {
  std::string source_name;   ///< "synthetic" or "cliflo_snapshot"
  std::string dataset;       ///< Site or agent identifier
  std::string content_hash;  ///< SHA-256 of the snapshot, or of the generator's parameters
  std::string licence;
};

struct WeatherSeries {
  std::vector<DailyWeather> records;  ///< Ascending by date, one per day, no gaps
  Provenance provenance;

  [[nodiscard]] bool empty() const noexcept { return records.empty(); }

  [[nodiscard]] std::size_t size() const noexcept { return records.size(); }

  /// True when the records are ascending, contiguous and individually valid.
  [[nodiscard]] bool is_well_formed() const noexcept;

  [[nodiscard]] double total_rainfall_mm() const noexcept;
};

/// What a data source is, in the terms a user needs before trusting it.
struct SourceDescription {
  std::string name;
  std::string licence;
  std::string coverage;  ///< Spatial and temporal extent, in plain words
  std::string cadence;   ///< How often new data appears
};

/// The result of `test_connection`: either usable, or an error a user can act
/// on. "Failed" is never an acceptable message on its own - say what is missing
/// and what to do about it.
struct ConnectionStatus {
  bool ok = false;
  std::string message;

  [[nodiscard]] static ConnectionStatus available(std::string detail);
  [[nodiscard]] static ConnectionStatus unavailable(std::string actionable_error);
};

/// The common port every weather source implements.
///
/// Three adapters are planned for each data source: synthetic (a generator),
/// file (a local snapshot) and live (the real service). For weather, the
/// synthetic and snapshot adapters live in core because they need nothing but
/// the standard library; fetching from CliFlo is done by a script that writes a
/// snapshot and records its hash, so that no live call can ever happen in the
/// middle of a simulation run. See docs/adr/0008-weather-sources.md.
class WeatherSource {
 public:
  WeatherSource() = default;
  WeatherSource(const WeatherSource&) = default;
  WeatherSource& operator=(const WeatherSource&) = default;
  WeatherSource(WeatherSource&&) = default;
  WeatherSource& operator=(WeatherSource&&) = default;
  virtual ~WeatherSource() = default;

  [[nodiscard]] virtual SourceDescription describe() const = 0;

  /// Cheap check that the source can deliver data, suitable for a CLI command.
  [[nodiscard]] virtual ConnectionStatus test_connection() const = 0;

  /// Every day in `range`, ascending. Throws std::out_of_range when the source
  /// cannot cover the whole range: a silently short series would look like a
  /// dry spell to every process downstream.
  [[nodiscard]] virtual WeatherSeries fetch(const DateRange& range) const = 0;
};

}  // namespace paddock::core
