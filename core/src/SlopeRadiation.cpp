// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <paddock/core/SlopeRadiation.hpp>
#include <paddock/core/Solar.hpp>

namespace paddock::core {

namespace {

constexpr int kDaysInYear = 366;

/// Sample days: every ninth, starting at 1, with the last day of a leap year
/// added so that no lookup ever has to extrapolate past the final sample.
std::vector<int> sample_days() {
  std::vector<int> days;
  for (int day = 1; day <= kDaysInYear; day += SlopeRadiationTable::sample_interval_days()) {
    days.push_back(day);
  }
  if (days.back() < kDaysInYear) {
    days.push_back(kDaysInYear);
  }
  return days;
}

}  // namespace

SlopeRadiationTable::SlopeRadiationTable(const Topography& ground, double latitude_degrees)
    : cols_(ground.slope_degrees.cols()),
      rows_(ground.slope_degrees.rows()),
      sample_days_(sample_days()) {
  if (ground.aspect_degrees.cols() != cols_ || ground.aspect_degrees.rows() != rows_) {
    throw std::invalid_argument(
        "SlopeRadiationTable: the slope and aspect rasters must be the same size");
  }

  samples_.resize(cols_ * rows_ * sample_days_.size());

  for (std::size_t row = 0; row < rows_; ++row) {
    for (std::size_t col = 0; col < cols_; ++col) {
      const double slope = ground.slope_degrees(col, row);
      const double aspect = ground.aspect_degrees(col, row);
      const std::size_t base = ((row * cols_) + col) * sample_days_.size();

      // Level ground has no aspect and needs no integrating: it is the thing
      // every ratio is measured against, so its ratio is one by definition.
      // Checking here also keeps the NaN aspect that flat ground carries out of
      // the arithmetic entirely.
      if (!(slope > 0.0)) {
        std::fill_n(samples_.begin() + static_cast<std::ptrdiff_t>(base), sample_days_.size(), 1.0);
        continue;
      }

      for (std::size_t i = 0; i < sample_days_.size(); ++i) {
        samples_[base + i] =
            slope_radiation_ratio(latitude_degrees, sample_days_[i], slope, aspect);
      }
    }
  }
}

double SlopeRadiationTable::ratio(std::size_t col, std::size_t row, int day_of_year) const {
  if (col >= cols_ || row >= rows_) {
    throw std::out_of_range("SlopeRadiationTable::ratio: cell is outside the grid");
  }
  const int day = std::clamp(day_of_year, 1, kDaysInYear);
  const std::size_t base = ((row * cols_) + col) * sample_days_.size();

  // The first sample at or after `day`. Samples are every ninth day from 1, so
  // this is arithmetic rather than a search - but the search is written out
  // because the last interval is short and the arithmetic would not be.
  const auto upper = std::lower_bound(sample_days_.begin(), sample_days_.end(), day);
  if (upper == sample_days_.begin()) {
    return samples_[base];
  }
  if (upper == sample_days_.end()) {
    return samples_[base + sample_days_.size() - 1];
  }

  const auto after = static_cast<std::size_t>(upper - sample_days_.begin());
  const int day_after = *upper;
  if (day_after == day) {
    return samples_[base + after];
  }

  const std::size_t before = after - 1;
  const int day_before = sample_days_[before];
  const double weight =
      static_cast<double>(day - day_before) / static_cast<double>(day_after - day_before);

  return samples_[base + before] + (weight * (samples_[base + after] - samples_[base + before]));
}

}  // namespace paddock::core
