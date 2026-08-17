// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace paddock::test_support {

/// A measured monthly series from data/calibration/, January first.
struct CalibrationSeries {
  std::string site;
  double latitude_degrees = 0.0;
  double nitrogen_applied_kg_per_ha = 0.0;
  std::array<double, 12> monthly_kg_dm_per_ha_per_day{};
  double annual_t_dm_per_ha = 0.0;

  /// Each month's share of the year's total, which is the shape the model is
  /// asked to reproduce even when the magnitude cannot match.
  [[nodiscard]] std::array<double, 12> monthly_shares() const {
    double total = 0.0;
    for (const double value : monthly_kg_dm_per_ha_per_day) {
      total += value;
    }
    std::array<double, 12> shares{};
    if (total <= 0.0) {
      return shares;
    }
    for (std::size_t month = 0; month < shares.size(); ++month) {
      shares[month] = monthly_kg_dm_per_ha_per_day[month] / total;
    }
    return shares;
  }
};

/// Pearson correlation between two monthly series.
[[nodiscard]] inline double correlation(const std::array<double, 12>& left,
                                        const std::array<double, 12>& right) {
  double sum_left = 0.0;
  double sum_right = 0.0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    sum_left += left[i];
    sum_right += right[i];
  }
  const double mean_left = sum_left / static_cast<double>(left.size());
  const double mean_right = sum_right / static_cast<double>(right.size());

  double covariance = 0.0;
  double variance_left = 0.0;
  double variance_right = 0.0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    const double delta_left = left[i] - mean_left;
    const double delta_right = right[i] - mean_right;
    covariance += delta_left * delta_right;
    variance_left += delta_left * delta_left;
    variance_right += delta_right * delta_right;
  }
  if (variance_left <= 0.0 || variance_right <= 0.0) {
    return 0.0;
  }
  return covariance / std::sqrt(variance_left * variance_right);
}

/// Reads one site from a calibration CSV. Lines starting with '#' are comments.
[[nodiscard]] inline CalibrationSeries load_calibration_series(const std::string& path,
                                                               const std::string& site) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("cannot open calibration data '" + path + "'");
  }

  std::string line;
  std::vector<std::string> header;
  while (std::getline(file, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    std::vector<std::string> fields;
    std::istringstream row(line);
    std::string field;
    while (std::getline(row, field, ',')) {
      if (!field.empty() && field.back() == '\r') {
        field.pop_back();
      }
      fields.push_back(field);
    }
    if (header.empty()) {
      header = fields;
      continue;
    }
    if (fields.empty() || fields.front() != site) {
      continue;
    }

    CalibrationSeries series;
    series.site = fields.at(0);
    series.latitude_degrees = std::stod(fields.at(1));
    series.nitrogen_applied_kg_per_ha = std::stod(fields.at(2));
    for (std::size_t month = 0; month < 12; ++month) {
      series.monthly_kg_dm_per_ha_per_day[month] = std::stod(fields.at(3 + month));
    }
    series.annual_t_dm_per_ha = std::stod(fields.at(15));
    return series;
  }

  throw std::runtime_error("calibration data '" + path + "' has no site '" + site + "'");
}

}  // namespace paddock::test_support
