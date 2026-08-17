// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include <paddock/core/Topography.hpp>

namespace paddock::core {

namespace {

constexpr double kDegreesPerRadian = 57.29577951308232;
constexpr double kDegreesInATurn = 360.0;

/// Elevation at (col, row), with the outermost row and column repeated outward
/// so that every cell has a full 3x3 window. The same edge convention as
/// `gdaldem -compute_edges`.
double clamped(const Raster<double>& elevation, std::ptrdiff_t col, std::ptrdiff_t row) {
  const auto last_col = static_cast<std::ptrdiff_t>(elevation.cols()) - 1;
  const auto last_row = static_cast<std::ptrdiff_t>(elevation.rows()) - 1;
  return elevation(static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(col, 0, last_col)),
                   static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(row, 0, last_row)));
}

}  // namespace

Topography topography_of(const Raster<double>& elevation) {
  if (elevation.cols() < 2 || elevation.rows() < 2) {
    throw std::out_of_range(
        "topography_of needs at least a 2x2 elevation raster; there is no gradient in less");
  }
  // Cell size needs no check: Raster's constructor already refuses a
  // non-positive one, so a guard here would be unreachable.
  const double cell = elevation.transform().cell_size;

  Topography result{Raster<double>(elevation.cols(), elevation.rows(), elevation.transform()),
                    Raster<double>(elevation.cols(), elevation.rows(), elevation.transform())};

  for (std::size_t row = 0; row < elevation.rows(); ++row) {
    for (std::size_t col = 0; col < elevation.cols(); ++col) {
      const auto x = static_cast<std::ptrdiff_t>(col);
      const auto y = static_cast<std::ptrdiff_t>(row);

      // Horn's 3x3 window. Row 0 of a Paddock raster is the northernmost, so
      // y - 1 is the northern neighbour and y + 1 the southern one.
      const double north_west = clamped(elevation, x - 1, y - 1);
      const double north = clamped(elevation, x, y - 1);
      const double north_east = clamped(elevation, x + 1, y - 1);
      const double west = clamped(elevation, x - 1, y);
      const double east = clamped(elevation, x + 1, y);
      const double south_west = clamped(elevation, x - 1, y + 1);
      const double south = clamped(elevation, x, y + 1);
      const double south_east = clamped(elevation, x + 1, y + 1);

      // Metres of rise per metre travelled, in world directions rather than in
      // rows and columns - which is what keeps the aspect formula below free of
      // sign conventions that depend on how the raster happens to be stored.
      const double rise_east =
          ((north_east + (2.0 * east) + south_east) - (north_west + (2.0 * west) + south_west)) /
          (8.0 * cell);
      const double rise_north =
          ((north_west + (2.0 * north) + north_east) - (south_west + (2.0 * south) + south_east)) /
          (8.0 * cell);

      result.slope_degrees(col, row) =
          std::atan(std::hypot(rise_east, rise_north)) * kDegreesPerRadian;

      if (rise_east == 0.0 && rise_north == 0.0) {
        // Flat: no direction to report. See the note on Topography::aspect.
        result.aspect_degrees(col, row) = std::numeric_limits<double>::quiet_NaN();
        continue;
      }

      // Aspect is the compass bearing of steepest descent. The downhill
      // direction has components (-rise_east, -rise_north), and a bearing
      // clockwise from north is atan2(east component, north component).
      double bearing = std::atan2(-rise_east, -rise_north) * kDegreesPerRadian;
      if (bearing < 0.0) {
        bearing += kDegreesInATurn;
      }
      result.aspect_degrees(col, row) = bearing;
    }
  }
  return result;
}

}  // namespace paddock::core
