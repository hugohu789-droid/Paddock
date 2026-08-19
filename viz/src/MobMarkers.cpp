// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>
#include <vtkCellArray.h>
#include <vtkPoints.h>

#include <paddock/viz/MobMarkers.hpp>

namespace paddock::viz {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Points in the ground plane, anticlockwise, of the shape a kind is drawn as.
///
/// Sheep are a circle, cattle a square, deer a triangle. The choice is
/// arbitrary and the distinctness is not: two mobs on one farm have to be
/// separable at a glance and by somebody who cannot separate the colours.
std::vector<core::Point2D> outline_of(core::AnimalKind kind, core::Point2D at, double size_m) {
  const double radius = size_m / 2.0;
  std::vector<core::Point2D> points;

  switch (kind) {
    case core::AnimalKind::Sheep: {
      constexpr std::size_t kSides = 12;
      points.reserve(kSides);
      for (std::size_t i = 0; i < kSides; ++i) {
        const double angle = (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(kSides);
        points.push_back(
            {at.easting + (radius * std::cos(angle)), at.northing + (radius * std::sin(angle))});
      }
      break;
    }
    case core::AnimalKind::Cattle:
      points = {{at.easting - radius, at.northing - radius},
                {at.easting + radius, at.northing - radius},
                {at.easting + radius, at.northing + radius},
                {at.easting - radius, at.northing + radius}};
      break;
    case core::AnimalKind::Deer:
      points = {{at.easting - radius, at.northing - radius},
                {at.easting + radius, at.northing - radius},
                {at.easting, at.northing + radius}};
      break;
    case core::AnimalKind::Other:
    default:
      // A diamond, which is not any of the others. An animal whose species file
      // does not say what it is gets drawn as an animal whose species file does
      // not say what it is.
      points = {{at.easting, at.northing - radius},
                {at.easting + radius, at.northing},
                {at.easting, at.northing + radius},
                {at.easting - radius, at.northing}};
      break;
  }
  return points;
}

}  // namespace

std::array<double, 3> colour_of(core::AnimalKind kind) {
  switch (kind) {
    case core::AnimalKind::Sheep:
      // Near white, which is what a mob of sheep looks like from a distance and
      // what separates them from the greens underneath.
      return {0.95, 0.95, 0.90};
    case core::AnimalKind::Cattle:
      return {0.55, 0.25, 0.12};
    case core::AnimalKind::Deer:
      return {0.78, 0.55, 0.28};
    case core::AnimalKind::Other:
    default:
      return {0.60, 0.60, 0.65};
  }
}

const std::vector<core::AnimalKind>& marker_kinds() {
  static const std::vector<core::AnimalKind> kinds{core::AnimalKind::Sheep,
                                                   core::AnimalKind::Cattle, core::AnimalKind::Deer,
                                                   core::AnimalKind::Other};
  return kinds;
}

void build_mob_markers(const std::vector<MobMarker>& markers, core::AnimalKind kind, double size_m,
                       const std::function<double(core::Point2D)>& height, vtkPolyData* into) {
  if (into == nullptr) {
    return;
  }
  vtkNew<vtkPoints> points;
  vtkNew<vtkCellArray> polygons;

  for (const MobMarker& marker : markers) {
    if (marker.kind != kind) {
      continue;
    }
    const std::vector<core::Point2D> outline = outline_of(kind, marker.at, size_m);
    const vtkIdType first = points->GetNumberOfPoints();
    for (const core::Point2D& point : outline) {
      points->InsertNextPoint(point.easting, point.northing, height ? height(point) : 0.0);
    }
    polygons->InsertNextCell(static_cast<int>(outline.size()));
    for (std::size_t i = 0; i < outline.size(); ++i) {
      polygons->InsertCellPoint(first + static_cast<vtkIdType>(i));
    }
  }

  into->SetPoints(points);
  into->SetPolys(polygons);
  into->Modified();
}

}  // namespace paddock::viz
