// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <vector>

namespace paddock::core {

/// EPSG code for NZTM2000. Every coordinate inside core is metres on this grid;
/// WGS84 conversion happens at the gis/ boundary and nowhere else.
inline constexpr int kNztm2000Epsg = 2193;

/// A projected coordinate in metres.
struct Point2D {
  double easting = 0.0;
  double northing = 0.0;
};

[[nodiscard]] bool operator==(const Point2D& lhs, const Point2D& rhs) noexcept;
[[nodiscard]] bool operator!=(const Point2D& lhs, const Point2D& rhs) noexcept;

/// Axis-aligned extent in metres. An empty box has min > max on both axes.
struct BoundingBox {
  double min_easting = 0.0;
  double min_northing = 0.0;
  double max_easting = 0.0;
  double max_northing = 0.0;

  [[nodiscard]] static BoundingBox empty() noexcept;
  [[nodiscard]] bool is_empty() const noexcept;
  [[nodiscard]] double width() const noexcept;
  [[nodiscard]] double height() const noexcept;
  [[nodiscard]] double area() const noexcept;
  [[nodiscard]] bool contains(Point2D point) const noexcept;
  void expand_to_include(Point2D point) noexcept;
};

/// A simple closed ring in NZTM2000, used for paddock and farm boundaries.
///
/// The closing vertex is implicit: edges wrap from the last vertex back to the
/// first, and a repeated final vertex is dropped on construction. Winding order
/// is preserved; `signed_area()` is positive for counter-clockwise rings.
class Polygon {
 public:
  Polygon() = default;
  explicit Polygon(std::vector<Point2D> vertices);

  /// Axis-aligned rectangle, counter-clockwise, anchored at its lower-left corner.
  [[nodiscard]] static Polygon rectangle(Point2D lower_left, double width, double height);

  [[nodiscard]] const std::vector<Point2D>& vertices() const noexcept { return vertices_; }

  [[nodiscard]] std::size_t vertex_count() const noexcept { return vertices_.size(); }

  /// A polygon is usable once it has three vertices and a non-zero area.
  [[nodiscard]] bool is_valid() const noexcept;

  [[nodiscard]] double signed_area() const noexcept;
  [[nodiscard]] double area() const noexcept;
  [[nodiscard]] double area_hectares() const noexcept;
  [[nodiscard]] double perimeter() const noexcept;

  /// Area centroid. Returns the origin for a degenerate ring.
  [[nodiscard]] Point2D centroid() const noexcept;
  [[nodiscard]] BoundingBox bounds() const noexcept;

  /// Crossing-number test. Points exactly on an edge are not guaranteed to be
  /// classified consistently; paddock masks always use cell centres, which are
  /// never on a boundary in practice.
  [[nodiscard]] bool contains(Point2D point) const noexcept;

 private:
  std::vector<Point2D> vertices_;
};

/// Square metres in one hectare.
inline constexpr double kSquareMetresPerHectare = 10000.0;

}  // namespace paddock::core
