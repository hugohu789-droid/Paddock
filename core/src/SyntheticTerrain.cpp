// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <paddock/core/SyntheticTerrain.hpp>

namespace paddock::core {

namespace {

constexpr double kTwoPi = 6.283185307179586;

/// Below this a "cell size" is a division by something indistinguishable from
/// zero rather than a resolution.
constexpr double kSmallestCellSizeM = 1e-6;

}  // namespace

double SyntheticSurface::elevation_at(Point2D point) const noexcept {
  const double east = point.easting - reference_easting;
  const double north = point.northing - reference_northing;

  double elevation = base_elevation_m + (gradient_east * east) + (gradient_north * north);

  if (undulation_amplitude_m != 0.0 && undulation_wavelength_m > 0.0) {
    elevation += undulation_amplitude_m * std::sin(kTwoPi * east / undulation_wavelength_m) *
                 std::sin(kTwoPi * north / undulation_wavelength_m);
  }
  return elevation;
}

SyntheticElevationSource::SyntheticElevationSource(SyntheticSurface surface) : surface_(surface) {}

SourceDescription SyntheticElevationSource::describe() const {
  return SourceDescription{"Synthetic elevation",
                           "None - generated, not data. Never present this as a measurement.",
                           "Anywhere, at any resolution: it is a formula, not a survey.",
                           "Unchanging. The same area always returns the same ground."};
}

ConnectionStatus SyntheticElevationSource::test_connection() const {
  return ConnectionStatus::available("Synthetic surface: no file and no network to fail.");
}

Raster<double> SyntheticElevationSource::fetch(const BoundingBox& area, double cell_size_m) const {
  if (area.is_empty() || area.width() <= 0.0 || area.height() <= 0.0) {
    throw std::out_of_range("SyntheticElevationSource::fetch was given an empty area");
  }
  if (!(cell_size_m > kSmallestCellSizeM)) {
    throw std::out_of_range("SyntheticElevationSource::fetch needs a positive cell size");
  }

  // Round up, so the raster covers the whole area asked for rather than
  // stopping just inside it.
  const auto cols = static_cast<std::size_t>(std::ceil((area.width() / cell_size_m) - 1e-9));
  const auto rows = static_cast<std::size_t>(std::ceil((area.height() / cell_size_m) - 1e-9));

  GeoTransform transform;
  transform.origin_easting = area.min_easting;
  transform.origin_northing = area.min_northing + (static_cast<double>(rows) * cell_size_m);
  transform.cell_size = cell_size_m;

  Raster<double> elevation(cols, rows, transform);

  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      // Sample at cell centres. Sampling at corners would put the first sample
      // exactly on the boundary and bias every derivative computed from it.
      const double east = area.min_easting + ((static_cast<double>(col) + 0.5) * cell_size_m);
      const double north =
          transform.origin_northing - ((static_cast<double>(row) + 0.5) * cell_size_m);
      elevation(col, row) = surface_.elevation_at(Point2D{east, north});
    }
  }
  return elevation;
}

SyntheticParcelSource::SyntheticParcelSource(double target_paddock_hectares)
    : target_paddock_hectares_(target_paddock_hectares) {
  if (!(target_paddock_hectares_ > 0.0)) {
    throw std::out_of_range("SyntheticParcelSource needs a positive paddock size");
  }
}

SourceDescription SyntheticParcelSource::describe() const {
  return SourceDescription{"Synthetic paddocks",
                           "None - generated, not data. Not a substitute for the LINZ cadastre.",
                           "Anywhere: rectangles laid over whatever area is asked for.",
                           "Unchanging. The same area always returns the same paddocks."};
}

ConnectionStatus SyntheticParcelSource::test_connection() const {
  return ConnectionStatus::available("Synthetic paddocks: no file and no network to fail.");
}

std::vector<Paddock> SyntheticParcelSource::fetch(const BoundingBox& area) const {
  if (area.is_empty() || area.width() <= 0.0 || area.height() <= 0.0) {
    throw std::out_of_range("SyntheticParcelSource::fetch was given an empty area");
  }

  // Square paddocks of the target area, then as many whole ones as fit. At
  // least one row and column, so a small farm gets one paddock rather than
  // none.
  const double side_m = std::sqrt(target_paddock_hectares_ * kSquareMetresPerHectare);
  const auto columns =
      std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(area.width() / side_m)));
  const auto rows =
      std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(area.height() / side_m)));

  // Divide the area exactly, so the paddocks tile it with no gap and no
  // overlap. Task #21 rasterises these, and a gap there is a cell that belongs
  // to no paddock and is silently never grazed.
  const double width_m = area.width() / static_cast<double>(columns);
  const double height_m = area.height() / static_cast<double>(rows);

  std::vector<Paddock> paddocks;
  paddocks.reserve(columns * rows);
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < columns; ++col) {
      const Point2D corner{area.min_easting + (static_cast<double>(col) * width_m),
                           area.min_northing + (static_cast<double>(row) * height_m)};
      paddocks.push_back(Paddock{"Paddock " + std::to_string((row * columns) + col + 1),
                                 Polygon::rectangle(corner, width_m, height_m)});
    }
  }
  return paddocks;
}

}  // namespace paddock::core
