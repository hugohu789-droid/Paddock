// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/core/Weather.hpp>

namespace paddock::core {

/// The file adapter of the weather port: replay of a local snapshot.
///
/// Real-year replay reads a snapshot on disk, never the network. Fetching from
/// NIWA CliFlo is done by `scripts/cliflo-snapshot.py`, which writes this format
/// and records the file's SHA-256; a scenario bundle pins that hash, so a run
/// either replays exactly the data it was built on or fails loudly.
///
/// The format is deliberately dull, one row per day, header-driven so column
/// order does not matter:
///
/// ```
/// # station: Lincoln, Canterbury (NIWA CliFlo agent 4881)
/// date,rainfall_mm,min_air_temperature_c,max_air_temperature_c,solar_radiation_mj_per_m2,wind_speed_m_per_s
/// 2023-01-01,0.0,11.2,24.6,25.1,3.4
/// ```
///
/// `date`, `rainfall_mm`, `min_air_temperature_c` and `max_air_temperature_c`
/// are required; radiation and wind default to zero when the station does not
/// report them. Lines starting with `#` and blank lines are ignored.
class SnapshotWeatherSource final : public WeatherSource {
 public:
  struct Options {
    std::string path;                   ///< Snapshot location, used in error messages
    std::string dataset;                ///< Station or agent identifier, recorded in provenance
    std::string licence;                ///< Licence the data was obtained under
    std::string expected_content_hash;  ///< Optional SHA-256 the file must match
  };

  /// Reads and parses the file.
  ///
  /// A missing file, a malformed row or a hash mismatch does not throw here:
  /// the problem is reported by `test_connection()` with a `path:line:` prefix,
  /// so `paddock source test` can print something a user can act on. `fetch()`
  /// throws the same message if a caller ignores that and asks for data anyway.
  explicit SnapshotWeatherSource(Options options);

  /// Parses text that is already in memory. Used by the tests and by any caller
  /// that has the snapshot from somewhere other than a file.
  static SnapshotWeatherSource from_text(const std::string& csv_text, Options options);

  [[nodiscard]] SourceDescription describe() const override;
  [[nodiscard]] ConnectionStatus test_connection() const override;
  [[nodiscard]] WeatherSeries fetch(const DateRange& range) const override;

  [[nodiscard]] const std::vector<DailyWeather>& records() const noexcept { return records_; }

  [[nodiscard]] const std::string& content_hash() const noexcept { return content_hash_; }

  /// The span the snapshot actually covers. Empty when it holds no rows.
  [[nodiscard]] DateRange coverage() const noexcept;

 private:
  SnapshotWeatherSource(std::vector<DailyWeather> records, std::string content_hash,
                        Options options);

  std::vector<DailyWeather> records_;
  std::string content_hash_;
  Options options_;
  std::string load_error_;
};

}  // namespace paddock::core
