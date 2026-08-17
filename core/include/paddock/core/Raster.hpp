// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <paddock/core/Geometry.hpp>

namespace paddock::core {

/// North-up affine transform for a square-celled raster in NZTM2000.
///
/// Row 0 is the northernmost row: northing decreases as the row index grows,
/// matching the GeoTIFF convention the gis/ layer reads.
struct GeoTransform {
  double origin_easting = 0.0;   ///< North-west corner of cell (0, 0), metres.
  double origin_northing = 0.0;  ///< North-west corner of cell (0, 0), metres.
  double cell_size = 10.0;       ///< Metres. The working resolution is 5-10 m.
  int epsg = kNztm2000Epsg;
};

struct CellIndex {
  std::size_t col = 0;
  std::size_t row = 0;
};

[[nodiscard]] inline bool operator==(const CellIndex& lhs, const CellIndex& rhs) noexcept {
  return lhs.col == rhs.col && lhs.row == rhs.row;
}

[[nodiscard]] inline bool operator!=(const CellIndex& lhs, const CellIndex& rhs) noexcept {
  return !(lhs == rhs);
}

/// A georeferenced grid of values in NZTM2000, stored row-major.
///
/// The raster carries its own georeferencing so that no caller has to keep a
/// transform alongside it, and iteration is always row-major so that traversal
/// order is reproducible.
template <typename T>
class Raster {
 public:
  using value_type = T;

  Raster() = default;

  Raster(std::size_t cols, std::size_t rows, GeoTransform transform, T fill_value = T{})
      : cols_(cols), rows_(rows), transform_(transform), values_(cols * rows, fill_value) {
    if (transform.cell_size <= 0.0) {
      throw std::invalid_argument("Raster: cell_size must be positive");
    }
  }

  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }

  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

  [[nodiscard]] const GeoTransform& transform() const noexcept { return transform_; }

  /// Area of a single cell in square metres.
  [[nodiscard]] double cell_area() const noexcept {
    return transform_.cell_size * transform_.cell_size;
  }

  /// Total covered area in hectares - the unit every pastoral figure is quoted in.
  [[nodiscard]] double area_hectares() const noexcept {
    return (cell_area() * static_cast<double>(size())) / kSquareMetresPerHectare;
  }

  [[nodiscard]] bool contains(std::size_t col, std::size_t row) const noexcept {
    return col < cols_ && row < rows_;
  }

  /// Bounds-checked access.
  [[nodiscard]] T& at(std::size_t col, std::size_t row) {
    require_in_range(col, row);
    return values_[index_of(col, row)];
  }

  [[nodiscard]] const T& at(std::size_t col, std::size_t row) const {
    require_in_range(col, row);
    return values_[index_of(col, row)];
  }

  /// Unchecked access for hot loops that have already validated their indices.
  [[nodiscard]] T& operator()(std::size_t col, std::size_t row) noexcept {
    return values_[index_of(col, row)];
  }

  [[nodiscard]] const T& operator()(std::size_t col, std::size_t row) const noexcept {
    return values_[index_of(col, row)];
  }

  [[nodiscard]] const std::vector<T>& values() const noexcept { return values_; }

  [[nodiscard]] std::vector<T>& values() noexcept { return values_; }

  /// Centre of a cell in NZTM2000 metres.
  [[nodiscard]] Point2D cell_centre(std::size_t col, std::size_t row) const noexcept {
    const double half = transform_.cell_size / 2.0;
    return Point2D{
        transform_.origin_easting + (static_cast<double>(col) * transform_.cell_size) + half,
        transform_.origin_northing - (static_cast<double>(row) * transform_.cell_size) - half};
  }

  /// Cell covering a coordinate, or nothing when the point lies outside.
  [[nodiscard]] std::optional<CellIndex> cell_at(Point2D point) const noexcept {
    if (empty()) {
      return std::nullopt;
    }
    const double col_offset = (point.easting - transform_.origin_easting) / transform_.cell_size;
    const double row_offset = (transform_.origin_northing - point.northing) / transform_.cell_size;
    if (col_offset < 0.0 || row_offset < 0.0) {
      return std::nullopt;
    }
    const double col = std::floor(col_offset);
    const double row = std::floor(row_offset);
    if (col >= static_cast<double>(cols_) || row >= static_cast<double>(rows_)) {
      return std::nullopt;
    }
    return CellIndex{static_cast<std::size_t>(col), static_cast<std::size_t>(row)};
  }

  [[nodiscard]] BoundingBox bounds() const noexcept {
    BoundingBox box;
    box.min_easting = transform_.origin_easting;
    box.max_northing = transform_.origin_northing;
    box.max_easting =
        transform_.origin_easting + (static_cast<double>(cols_) * transform_.cell_size);
    box.min_northing =
        transform_.origin_northing - (static_cast<double>(rows_) * transform_.cell_size);
    return box;
  }

  void fill(const T& value) {
    for (T& cell : values_) {
      cell = value;
    }
  }

  /// Row-major traversal. Deterministic by construction: results must never
  /// depend on the order cells are visited in.
  template <typename Fn>
  void for_each(Fn&& fn) {
    for (std::size_t row = 0; row < rows_; ++row) {
      for (std::size_t col = 0; col < cols_; ++col) {
        fn(col, row, values_[index_of(col, row)]);
      }
    }
  }

  template <typename Fn>
  void for_each(Fn&& fn) const {
    for (std::size_t row = 0; row < rows_; ++row) {
      for (std::size_t col = 0; col < cols_; ++col) {
        fn(col, row, values_[index_of(col, row)]);
      }
    }
  }

 private:
  [[nodiscard]] std::size_t index_of(std::size_t col, std::size_t row) const noexcept {
    return (row * cols_) + col;
  }

  void require_in_range(std::size_t col, std::size_t row) const {
    if (!contains(col, row)) {
      throw std::out_of_range("Raster: cell (" + std::to_string(col) + ", " + std::to_string(row) +
                              ") is outside " + std::to_string(cols_) + "x" +
                              std::to_string(rows_));
    }
  }

  std::size_t cols_ = 0;
  std::size_t rows_ = 0;
  GeoTransform transform_{};
  std::vector<T> values_;
};

}  // namespace paddock::core
