// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/Terrain.hpp>

/// Turning paddock boundaries into the cells they own.
///
/// Pure geometry on core's own types, so it needs no GDAL: reading the
/// GeoPackage is gis/'s job, and deciding which cell belongs to which paddock
/// is the model's.
namespace paddock::core {

/// Which paddock owns each cell of a grid.
///
/// One index per cell rather than one mask per paddock: a farm of eighty
/// paddocks would otherwise carry eighty rasters, of which each cell uses one
/// bit. This also makes the partition explicit - a cell has exactly one owner,
/// or none - where separate masks would let two paddocks claim the same ground
/// and nothing would notice.
class PaddockMask {
 public:
  /// The value stored for a cell no paddock owns: a gap between boundaries, or
  /// ground outside the farm.
  static constexpr std::size_t kUnowned = std::numeric_limits<std::size_t>::max();

  /// Assigns each cell of `shape` to the first paddock whose boundary contains
  /// the cell's centre.
  ///
  /// **Cell centres, not areas.** A boundary cuts across the cells it passes
  /// through, and this gives each of them whole to one paddock or to none. The
  /// alternative - splitting a cell by the fraction each paddock covers - is
  /// more faithful to the survey and would stop the result being a partition,
  /// which is what everything downstream relies on. The error that buys is
  /// bounded by the boundary length times the cell size, so it shrinks as the
  /// grid gets finer and is reported by area_error_hectares().
  ///
  /// **First containing paddock wins.** A proper cadastral layer does not
  /// overlap, but real ones contain slivers, and a rule that is written down is
  /// better than one that emerges from iteration order. Overlaps are counted
  /// rather than hidden: see contested_cells().
  PaddockMask(const Raster<double>& shape, const std::vector<Paddock>& paddocks);

  /// Index into the paddock list, or kUnowned.
  [[nodiscard]] std::size_t owner(std::size_t col, std::size_t row) const;

  /// How many cells each paddock owns, in the order the paddocks were given.
  [[nodiscard]] const std::vector<std::size_t>& cell_counts() const noexcept {
    return cell_counts_;
  }

  /// Cells owned by nobody: gaps between paddocks, and ground outside them.
  [[nodiscard]] std::size_t unowned_cells() const noexcept { return unowned_cells_; }

  /// Cells whose centre fell inside more than one paddock. Zero for a clean
  /// cadastral layer; anything else is worth looking at before trusting the
  /// areas.
  [[nodiscard]] std::size_t contested_cells() const noexcept { return contested_cells_; }

  /// Area a paddock covers in the raster, which is its cell count times the
  /// cell area - not the area of its polygon.
  [[nodiscard]] double rasterised_hectares(std::size_t paddock) const;

  /// The largest disagreement between a paddock's polygon area and the area it
  /// was given on the grid, in hectares.
  ///
  /// This is the price of assigning whole cells, and it is worth printing
  /// rather than assuming: on a 2.5 ha paddock at 25 m cells it is a few per
  /// cent, and it falls roughly in proportion to the cell size.
  [[nodiscard]] double area_error_hectares(const std::vector<Paddock>& paddocks) const;

  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }

  [[nodiscard]] double cell_area_hectares() const noexcept { return cell_area_hectares_; }

 private:
  std::size_t cols_ = 0;
  std::size_t rows_ = 0;
  double cell_area_hectares_ = 0.0;
  std::vector<std::size_t> owners_;
  std::vector<std::size_t> cell_counts_;
  std::size_t unowned_cells_ = 0;
  std::size_t contested_cells_ = 0;
};

}  // namespace paddock::core
