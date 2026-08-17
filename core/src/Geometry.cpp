// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include <paddock/core/Geometry.hpp>

namespace paddock::core {

namespace {

/// Vertices closer than this are treated as the same point when dropping a
/// repeated closing vertex. One micrometre is far below any survey precision
/// NZTM2000 data arrives with.
constexpr double kVertexEpsilonMetres = 1e-6;

bool same_point(Point2D lhs, Point2D rhs) noexcept {
  return std::abs(lhs.easting - rhs.easting) < kVertexEpsilonMetres &&
         std::abs(lhs.northing - rhs.northing) < kVertexEpsilonMetres;
}

}  // namespace

bool operator==(const Point2D& lhs, const Point2D& rhs) noexcept {
  return lhs.easting == rhs.easting && lhs.northing == rhs.northing;
}

bool operator!=(const Point2D& lhs, const Point2D& rhs) noexcept {
  return !(lhs == rhs);
}

BoundingBox BoundingBox::empty() noexcept {
  BoundingBox box;
  box.min_easting = std::numeric_limits<double>::max();
  box.min_northing = std::numeric_limits<double>::max();
  box.max_easting = std::numeric_limits<double>::lowest();
  box.max_northing = std::numeric_limits<double>::lowest();
  return box;
}

bool BoundingBox::is_empty() const noexcept {
  return min_easting > max_easting || min_northing > max_northing;
}

double BoundingBox::width() const noexcept {
  return is_empty() ? 0.0 : max_easting - min_easting;
}

double BoundingBox::height() const noexcept {
  return is_empty() ? 0.0 : max_northing - min_northing;
}

double BoundingBox::area() const noexcept {
  return width() * height();
}

bool BoundingBox::contains(Point2D point) const noexcept {
  return !is_empty() && point.easting >= min_easting && point.easting <= max_easting &&
         point.northing >= min_northing && point.northing <= max_northing;
}

void BoundingBox::expand_to_include(Point2D point) noexcept {
  min_easting = std::min(point.easting, min_easting);
  min_northing = std::min(point.northing, min_northing);
  max_easting = std::max(point.easting, max_easting);
  max_northing = std::max(point.northing, max_northing);
}

Polygon::Polygon(std::vector<Point2D> vertices) : vertices_(std::move(vertices)) {
  while (vertices_.size() > 1 && same_point(vertices_.front(), vertices_.back())) {
    vertices_.pop_back();
  }
}

Polygon Polygon::rectangle(Point2D lower_left, double width, double height) {
  return Polygon({lower_left,
                  {lower_left.easting + width, lower_left.northing},
                  {lower_left.easting + width, lower_left.northing + height},
                  {lower_left.easting, lower_left.northing + height}});
}

bool Polygon::is_valid() const noexcept {
  return vertices_.size() >= 3 && area() > kVertexEpsilonMetres;
}

double Polygon::signed_area() const noexcept {
  if (vertices_.size() < 3) {
    return 0.0;
  }

  // The shoelace formula, measured from the first vertex rather than from the
  // false origin of the projection.
  //
  // Applied to absolute NZTM2000 coordinates it loses most of its precision to
  // cancellation: eastings near 1.6e6 and northings near 5.2e6 give products
  // near 8e12, and a 2.5 ha paddock is the 2.5e4 left after subtracting two of
  // them. The last bits of an 8e12 double are worth about 2e-4, so each edge
  // contributes that much noise to a number four hundred million times
  // smaller. Summed over a farm's worth of paddocks it reached 23 cm2, which is
  // what a macOS run caught - Apple Silicon's fused multiply-add rounds the
  // products differently, so the x86 builds had been getting away with it.
  //
  // Subtracting the first vertex makes every coordinate a local offset, tens or
  // hundreds of metres rather than millions, and the cancellation disappears.
  // The result is unchanged in exact arithmetic: translating a polygon does not
  // change its area.
  const Point2D origin = vertices_.front();
  double twice_area = 0.0;
  for (std::size_t i = 0; i < vertices_.size(); ++i) {
    const Point2D& current = vertices_[i];
    const Point2D& next = vertices_[(i + 1) % vertices_.size()];
    const double current_east = current.easting - origin.easting;
    const double current_north = current.northing - origin.northing;
    const double next_east = next.easting - origin.easting;
    const double next_north = next.northing - origin.northing;
    twice_area += (current_east * next_north) - (next_east * current_north);
  }
  return twice_area / 2.0;
}

double Polygon::area() const noexcept {
  return std::abs(signed_area());
}

double Polygon::area_hectares() const noexcept {
  return area() / kSquareMetresPerHectare;
}

double Polygon::perimeter() const noexcept {
  if (vertices_.size() < 2) {
    return 0.0;
  }
  double total = 0.0;
  for (std::size_t i = 0; i < vertices_.size(); ++i) {
    const Point2D& current = vertices_[i];
    const Point2D& next = vertices_[(i + 1) % vertices_.size()];
    total += std::hypot(next.easting - current.easting, next.northing - current.northing);
  }
  return total;
}

Point2D Polygon::centroid() const noexcept {
  const double signed_area_value = signed_area();
  if (vertices_.size() < 3 || std::abs(signed_area_value) < kVertexEpsilonMetres) {
    return Point2D{};
  }
  double easting_moment = 0.0;
  double northing_moment = 0.0;
  for (std::size_t i = 0; i < vertices_.size(); ++i) {
    const Point2D& current = vertices_[i];
    const Point2D& next = vertices_[(i + 1) % vertices_.size()];
    const double cross = (current.easting * next.northing) - (next.easting * current.northing);
    easting_moment += (current.easting + next.easting) * cross;
    northing_moment += (current.northing + next.northing) * cross;
  }
  const double factor = 1.0 / (6.0 * signed_area_value);
  return Point2D{easting_moment * factor, northing_moment * factor};
}

BoundingBox Polygon::bounds() const noexcept {
  BoundingBox box = BoundingBox::empty();
  for (const Point2D& vertex : vertices_) {
    box.expand_to_include(vertex);
  }
  return box;
}

bool Polygon::contains(Point2D point) const noexcept {
  if (vertices_.size() < 3) {
    return false;
  }
  bool inside = false;
  for (std::size_t i = 0; i < vertices_.size(); ++i) {
    const Point2D& current = vertices_[i];
    const Point2D& next = vertices_[(i + 1) % vertices_.size()];
    const bool straddles = (current.northing > point.northing) != (next.northing > point.northing);
    if (!straddles) {
      continue;
    }
    const double along_edge =
        (point.northing - current.northing) / (next.northing - current.northing);
    const double crossing_easting =
        current.easting + (along_edge * (next.easting - current.easting));
    if (point.easting < crossing_easting) {
      inside = !inside;
    }
  }
  return inside;
}

}  // namespace paddock::core
