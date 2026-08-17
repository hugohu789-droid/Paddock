// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <vector>

#include <paddock/core/Raster.hpp>
#include <paddock/core/Topography.hpp>

namespace paddock::core {

/// Per-cell radiation ratios for a whole year, sampled and interpolated.
///
/// slope_radiation_ratio() integrates a day of solar geometry, which costs
/// about 11 microseconds. A 1536-cell farm stepped through a year needs 562 176
/// of them - 6.1 seconds measured, against a whole-year run that otherwise
/// takes well under one. So the ratios are computed once and looked up.
///
/// The ratio moves smoothly through the year, so it is sampled every nine days
/// and interpolated between samples. Nine days was chosen by measuring the
/// error that interpolation introduces, at Canterbury's latitude:
///
///     samples  interval   north 20   south 20   south 30   east 25
///          12       30d     0.0433     0.0214     0.0474    0.0036
///          24       15d     0.0114     0.0055     0.0168    0.0009
///          37        9d     0.0040     0.0020     0.0077    0.0003
///          73        5d     0.0012     0.0006     0.0032    0.0001
///
/// The worst case is a steep shaded slope, whose ratio collapses towards zero
/// in midwinter. At nine days that costs 0.008 of a ratio - against measured
/// solar radiation that carries twenty to thirty per cent, which is where the
/// real uncertainty is. Denser sampling would buy precision the inputs cannot
/// support.
///
/// **This is linear in the number of cells**: 0.6 s for 1536 cells, and about
/// 8 s for the 20 000 a 200 ha farm reaches at 10 m. If that becomes a problem
/// the answer is to key the table on quantised slope and aspect instead of on
/// cells, which stops depending on grid size altogether. It is not done yet
/// because it trades a measured error for an unmeasured one.
class SlopeRadiationTable {
 public:
  /// Builds the table for `ground` at `latitude_degrees`.
  ///
  /// Cells whose aspect is NaN - level ground, which has no aspect - get a
  /// ratio of exactly one on every day, without integrating anything.
  SlopeRadiationTable(const Topography& ground, double latitude_degrees);

  /// The ratio for one cell on one day of the year, one meaning level ground.
  [[nodiscard]] double ratio(std::size_t col, std::size_t row, int day_of_year) const;

  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }

  /// How many days apart the samples are.
  [[nodiscard]] static constexpr int sample_interval_days() noexcept { return 9; }

 private:
  std::size_t cols_ = 0;
  std::size_t rows_ = 0;
  /// Day of year for each sample, ascending, first 1 and last at least 365.
  std::vector<int> sample_days_;
  /// cols_ * rows_ * sample_days_.size(), cell-major.
  std::vector<double> samples_;
};

}  // namespace paddock::core
