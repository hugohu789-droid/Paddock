// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <paddock/core/Sha256.hpp>
#include <paddock/core/SnapshotWeather.hpp>

namespace paddock::core {

namespace {

/// Thrown while parsing and turned into a stored error message: a snapshot
/// problem is something to report, not something to crash on.
class SnapshotParseError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

std::string trimmed(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

std::vector<std::string> split_fields(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t comma = line.find(',', start);
    if (comma == std::string::npos) {
      fields.push_back(trimmed(line.substr(start)));
      return fields;
    }
    fields.push_back(trimmed(line.substr(start, comma - start)));
    start = comma + 1;
  }
}

std::string where(const std::string& path, std::size_t line_number) {
  return (path.empty() ? std::string("<snapshot>") : path) + ':' + std::to_string(line_number) +
         ": ";
}

/// strtod rather than std::stod (which throws) or std::from_chars (whose
/// floating-point overloads are still missing from some standard libraries the
/// CI matrix builds against). The program never changes locale, so strtod reads
/// in the C locale and a decimal point means a decimal point on every platform.
double parse_number(const std::string& field, const std::string& column, const std::string& path,
                    std::size_t line_number) {
  if (field.empty()) {
    throw SnapshotParseError(where(path, line_number) + "column '" + column + "' is empty");
  }
  const char* begin = field.c_str();
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end != begin + field.size()) {
    throw SnapshotParseError(where(path, line_number) + "column '" + column +
                             "' is not a number: '" + field + "'");
  }
  return value;
}

Date parse_date(const std::string& field, const std::string& path, std::size_t line_number) {
  // Exactly YYYY-MM-DD. Accepting anything looser invites a snapshot whose
  // day and month are the wrong way round to run without complaint.
  if (field.size() != 10 || field[4] != '-' || field[7] != '-') {
    throw SnapshotParseError(where(path, line_number) + "date must be YYYY-MM-DD, got '" + field +
                             "'");
  }
  for (std::size_t i = 0; i < field.size(); ++i) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (field[i] < '0' || field[i] > '9') {
      throw SnapshotParseError(where(path, line_number) + "date must be YYYY-MM-DD, got '" + field +
                               "'");
    }
  }
  const Date date{std::stoi(field.substr(0, 4)), std::stoi(field.substr(5, 2)),
                  std::stoi(field.substr(8, 2))};
  if (!date.is_valid()) {
    throw SnapshotParseError(where(path, line_number) + "no such date: '" + field + "'");
  }
  return date;
}

std::vector<DailyWeather> parse_snapshot(const std::string& text, const std::string& path) {
  std::unordered_map<std::string, std::size_t> columns;
  std::vector<DailyWeather> records;
  std::istringstream input(text);
  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;
    const std::string cleaned = trimmed(line);
    if (cleaned.empty() || cleaned.front() == '#') {
      continue;
    }

    const std::vector<std::string> fields = split_fields(cleaned);

    if (columns.empty()) {
      for (std::size_t i = 0; i < fields.size(); ++i) {
        columns.emplace(fields[i], i);
      }
      for (const char* required :
           {"date", "rainfall_mm", "min_air_temperature_c", "max_air_temperature_c"}) {
        if (columns.count(required) == 0) {
          throw SnapshotParseError(where(path, line_number) + "header is missing the '" +
                                   std::string(required) + "' column");
        }
      }
      continue;
    }

    const auto field_at = [&](const char* column) -> const std::string& {
      const auto found = columns.find(column);
      if (found == columns.end() || found->second >= fields.size()) {
        throw SnapshotParseError(where(path, line_number) + "row is missing the '" +
                                 std::string(column) + "' column");
      }
      return fields[found->second];
    };
    const auto optional_number = [&](const char* column) -> double {
      if (columns.count(column) == 0) {
        return 0.0;
      }
      return parse_number(field_at(column), column, path, line_number);
    };

    DailyWeather record;
    record.date = parse_date(field_at("date"), path, line_number);
    record.rainfall_mm = parse_number(field_at("rainfall_mm"), "rainfall_mm", path, line_number);
    record.min_air_temperature_c =
        parse_number(field_at("min_air_temperature_c"), "min_air_temperature_c", path, line_number);
    record.max_air_temperature_c =
        parse_number(field_at("max_air_temperature_c"), "max_air_temperature_c", path, line_number);
    record.solar_radiation_mj_per_m2 = optional_number("solar_radiation_mj_per_m2");
    record.wind_speed_m_per_s = optional_number("wind_speed_m_per_s");

    if (!record.is_valid()) {
      throw SnapshotParseError(where(path, line_number) +
                               "row is not physically possible (negative rainfall, radiation or "
                               "wind, or a maximum temperature below the minimum)");
    }
    if (!records.empty()) {
      const std::int64_t previous = records.back().date.days_since_epoch();
      const std::int64_t current = record.date.days_since_epoch();
      if (current != previous + 1) {
        throw SnapshotParseError(where(path, line_number) + "expected " +
                                 Date::from_days_since_epoch(previous + 1).to_iso_string() +
                                 ", got " + record.date.to_iso_string() +
                                 "; snapshots must be one row per day with no gaps");
      }
    }
    records.push_back(record);
  }

  if (columns.empty()) {
    throw SnapshotParseError(where(path, 1) + "snapshot has no header row");
  }
  if (records.empty()) {
    throw SnapshotParseError(where(path, line_number) + "snapshot has a header but no rows");
  }
  return records;
}

}  // namespace

SnapshotWeatherSource::SnapshotWeatherSource(std::vector<DailyWeather> records,
                                             std::string content_hash, Options options)
    : records_(std::move(records)),
      content_hash_(std::move(content_hash)),
      options_(std::move(options)) {}

SnapshotWeatherSource::SnapshotWeatherSource(Options options) : options_(std::move(options)) {
  const std::ifstream file(options_.path, std::ios::binary);
  if (!file) {
    load_error_ = "cannot open weather snapshot '" + options_.path +
                  "'. Create it with scripts/cliflo-snapshot.py, or point the scenario at a "
                  "synthetic weather source.";
    return;
  }

  std::ostringstream contents;
  contents << file.rdbuf();
  const std::string text = contents.str();
  content_hash_ = Sha256::hex_of(text);

  if (!options_.expected_content_hash.empty() && options_.expected_content_hash != content_hash_) {
    load_error_ = "weather snapshot '" + options_.path +
                  "' does not match the hash the scenario "
                  "was built on. Expected " +
                  options_.expected_content_hash + ", found " + content_hash_ + ".";
    return;
  }

  try {
    records_ = parse_snapshot(text, options_.path);
  } catch (const SnapshotParseError& error) {
    load_error_ = error.what();
  }
}

SnapshotWeatherSource SnapshotWeatherSource::from_text(const std::string& csv_text,
                                                       Options options) {
  const std::string hash = Sha256::hex_of(csv_text);
  const std::string path = options.path;
  try {
    return {parse_snapshot(csv_text, path), hash, std::move(options)};
  } catch (const SnapshotParseError& error) {
    SnapshotWeatherSource source({}, hash, std::move(options));
    source.load_error_ = error.what();
    return source;
  }
}

SourceDescription SnapshotWeatherSource::describe() const {
  const DateRange covered = coverage();
  const std::string span =
      records_.empty() ? "no data loaded"
                       : covered.first.to_iso_string() + " to " + covered.last.to_iso_string();
  return SourceDescription{
      "cliflo_snapshot:" + (options_.dataset.empty() ? options_.path : options_.dataset),
      options_.licence.empty() ? "unrecorded - check the source's terms before redistributing"
                               : options_.licence,
      span, "static snapshot; refresh by re-running the fetch script"};
}

ConnectionStatus SnapshotWeatherSource::test_connection() const {
  if (!load_error_.empty()) {
    return ConnectionStatus::unavailable(load_error_);
  }
  const DateRange covered = coverage();
  return ConnectionStatus::available(
      std::to_string(records_.size()) + " days, " + covered.first.to_iso_string() + " to " +
      covered.last.to_iso_string() + ", sha256 " + content_hash_.substr(0, 12));
}

DateRange SnapshotWeatherSource::coverage() const noexcept {
  if (records_.empty()) {
    return DateRange{};
  }
  return DateRange{records_.front().date, records_.back().date};
}

WeatherSeries SnapshotWeatherSource::fetch(const DateRange& range) const {
  if (!load_error_.empty()) {
    throw std::runtime_error("SnapshotWeatherSource: " + load_error_);
  }
  if (!range.is_valid()) {
    throw std::invalid_argument("SnapshotWeatherSource: invalid date range " +
                                range.first.to_iso_string() + " to " + range.last.to_iso_string());
  }

  const DateRange covered = coverage();
  if (!covered.contains(range.first) || !covered.contains(range.last)) {
    // A short series would look like a dry spell to every process downstream,
    // so this is an error rather than a truncated answer.
    throw std::out_of_range("SnapshotWeatherSource: '" + options_.path + "' covers " +
                            covered.first.to_iso_string() + " to " + covered.last.to_iso_string() +
                            ", which does not include " + range.first.to_iso_string() + " to " +
                            range.last.to_iso_string());
  }

  const auto offset =
      static_cast<std::size_t>(range.first.days_since_epoch() - covered.first.days_since_epoch());
  const auto count = static_cast<std::size_t>(range.day_count());

  WeatherSeries series;
  series.provenance =
      Provenance{"cliflo_snapshot", options_.dataset, content_hash_, options_.licence};
  series.records.assign(records_.begin() + static_cast<std::ptrdiff_t>(offset),
                        records_.begin() + static_cast<std::ptrdiff_t>(offset + count));
  return series;
}

}  // namespace paddock::core
