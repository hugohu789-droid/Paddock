// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <paddock/core/PaddockMask.hpp>

namespace paddock::core {

PaddockMask::PaddockMask(const Raster<double>& shape, const std::vector<Paddock>& paddocks)
    : cols_(shape.cols()),
      rows_(shape.rows()),
      cell_area_hectares_(shape.transform().cell_size * shape.transform().cell_size /
                          kSquareMetresPerHectare),
      owners_(shape.size(), kUnowned),
      cell_counts_(paddocks.size(), 0) {
  if (shape.empty()) {
    throw std::invalid_argument("PaddockMask: the grid has no cells to assign");
  }

  const double cell = shape.transform().cell_size;
  const double origin_easting = shape.transform().origin_easting;
  const double origin_northing = shape.transform().origin_northing;

  // Each paddock's bounding box is tested before its ring, because
  // Polygon::contains walks every edge and a farm has far more cells than any
  // one paddock covers.
  std::vector<BoundingBox> bounds;
  bounds.reserve(paddocks.size());
  for (const Paddock& paddock : paddocks) {
    bounds.push_back(paddock.boundary.bounds());
  }

  for (std::size_t row = 0; row < rows_; ++row) {
    // Row 0 is the northernmost, so northing decreases as the row index grows.
    const double northing = origin_northing - ((static_cast<double>(row) + 0.5) * cell);

    for (std::size_t col = 0; col < cols_; ++col) {
      const double easting = origin_easting + ((static_cast<double>(col) + 0.5) * cell);
      const Point2D centre{easting, northing};

      std::size_t owner = kUnowned;
      std::size_t claims = 0;

      for (std::size_t i = 0; i < paddocks.size(); ++i) {
        if (!bounds[i].contains(centre)) {
          continue;
        }
        if (!paddocks[i].boundary.contains(centre)) {
          continue;
        }
        ++claims;
        if (owner == kUnowned) {
          owner = i;
        }
      }

      owners_[(row * cols_) + col] = owner;
      if (owner == kUnowned) {
        ++unowned_cells_;
      } else {
        ++cell_counts_[owner];
      }
      if (claims > 1) {
        ++contested_cells_;
      }
    }
  }
}

std::size_t PaddockMask::owner(std::size_t col, std::size_t row) const {
  if (col >= cols_ || row >= rows_) {
    throw std::out_of_range("PaddockMask::owner: cell is outside the grid");
  }
  return owners_[(row * cols_) + col];
}

double PaddockMask::rasterised_hectares(std::size_t paddock) const {
  if (paddock >= cell_counts_.size()) {
    throw std::out_of_range("PaddockMask::rasterised_hectares: no such paddock");
  }
  return static_cast<double>(cell_counts_[paddock]) * cell_area_hectares_;
}

double PaddockMask::area_error_hectares(const std::vector<Paddock>& paddocks) const {
  if (paddocks.size() != cell_counts_.size()) {
    throw std::invalid_argument(
        "PaddockMask::area_error_hectares: a different set of paddocks from the one rasterised");
  }

  double worst = 0.0;
  for (std::size_t i = 0; i < paddocks.size(); ++i) {
    worst = std::max(worst, std::abs(rasterised_hectares(i) - paddocks[i].area_hectares()));
  }
  return worst;
}

}  // namespace paddock::core
