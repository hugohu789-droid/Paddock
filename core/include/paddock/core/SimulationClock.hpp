// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace paddock::core {

[[nodiscard]] bool is_leap_year(int year) noexcept;
[[nodiscard]] int days_in_month(int year, int month) noexcept;

/// A calendar date in the proleptic Gregorian calendar.
///
/// The core keeps its own date type rather than reaching for <chrono>'s clocks:
/// simulated time must never be able to pick up the wall clock.
struct Date {
  int year = 1970;
  int month = 1;
  int day = 1;

  [[nodiscard]] static Date from_days_since_epoch(std::int64_t days) noexcept;

  /// Days since 1970-01-01, negative before it.
  [[nodiscard]] std::int64_t days_since_epoch() const noexcept;
  [[nodiscard]] int day_of_year() const noexcept;
  [[nodiscard]] bool is_valid() const noexcept;

  /// ISO 8601, e.g. "2024-07-01".
  [[nodiscard]] std::string to_iso_string() const;
};

[[nodiscard]] bool operator==(const Date& lhs, const Date& rhs) noexcept;
[[nodiscard]] bool operator!=(const Date& lhs, const Date& rhs) noexcept;
[[nodiscard]] bool operator<(const Date& lhs, const Date& rhs) noexcept;

/// Southern Hemisphere seasons: summer is December to February.
enum class Season : std::uint8_t { Summer, Autumn, Winter, Spring };

[[nodiscard]] Season season_of(const Date& date) noexcept;
[[nodiscard]] std::string_view season_name(Season season) noexcept;

/// Daily simulation time. Stepping is by whole days - the pastoral processes
/// this engine models (growth, intake, drainage) are all daily.
class SimulationClock {
 public:
  SimulationClock() = default;
  explicit SimulationClock(Date start);

  void advance(int days = 1);
  void reset();

  [[nodiscard]] Date date() const noexcept;
  [[nodiscard]] Date start_date() const noexcept;

  /// Days elapsed since the start date; 0 on the first simulated day.
  [[nodiscard]] std::int64_t day_index() const noexcept { return day_index_; }

 private:
  std::int64_t start_epoch_day_ = 0;
  std::int64_t day_index_ = 0;
};

}  // namespace paddock::core
