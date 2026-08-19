// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
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

std::vector<core::Point2D> scatter_within(const core::Polygon& paddock, std::size_t paddock_index,
                                          int head, core::AnimalKind kind) {
  std::vector<core::Point2D> placed;
  if (head <= 0 || paddock.vertices().size() < 3) {
    return placed;
  }

  core::BoundingBox area = core::BoundingBox::empty();
  for (const core::Point2D& vertex : paddock.vertices()) {
    area.expand_to_include(vertex);
  }
  if (area.width() <= 0.0 || area.height() <= 0.0) {
    return placed;
  }

  // Seeded from the paddock and the species, and not from the date. Stock that
  // stay where they are between one day and the next are drawn where they were:
  // the model does not move them within a paddock, so neither does this. A
  // date in the seed would make them jitter every frame and look like motion
  // the model had computed.
  std::mt19937_64 generator((static_cast<std::uint64_t>(paddock_index) << 8U) ^
                            static_cast<std::uint64_t>(kind) ^ 0x9E3779B97F4A7C15ULL);
  std::uniform_real_distribution<double> across(area.min_easting, area.max_easting);
  std::uniform_real_distribution<double> along(area.min_northing, area.max_northing);

  // Rejection sampling against the real boundary, so no animal stands outside
  // its own fence. Capped so an awkward polygon cannot spin here for ever; a
  // paddock is a farm subdivision and its area is a decent share of its box, so
  // the cap is not normally approached.
  const int attempts_allowed = head * 64;
  placed.reserve(static_cast<std::size_t>(head));
  for (int attempt = 0;
       attempt < attempts_allowed && placed.size() < static_cast<std::size_t>(head); ++attempt) {
    const core::Point2D candidate{across(generator), along(generator)};
    if (paddock.contains(candidate)) {
      placed.push_back(candidate);
    }
  }
  return placed;
}

const std::vector<core::AnimalKind>& marker_kinds() {
  static const std::vector<core::AnimalKind> kinds{core::AnimalKind::Sheep,
                                                   core::AnimalKind::Cattle, core::AnimalKind::Deer,
                                                   core::AnimalKind::Other};
  return kinds;
}

void build_mob_markers(const std::vector<MobMarker>& markers, core::AnimalKind kind, double size_m,
                       const std::function<double(core::Point2D)>& height,
                       const std::vector<core::Polygon>& paddocks, vtkPolyData* into) {
  if (into == nullptr) {
    return;
  }
  vtkNew<vtkPoints> points;
  vtkNew<vtkCellArray> polygons;

  // One shape per animal, spread over the paddock it is on. Falls back to the
  // single mark at marker.at when there is no boundary to spread over - a farm
  // with no paddock polygons still has to show that its stock are somewhere.
  const auto draw_at = [&](core::Point2D where) {
    const std::vector<core::Point2D> outline = outline_of(kind, where, size_m);
    const vtkIdType first = points->GetNumberOfPoints();
    for (const core::Point2D& point : outline) {
      points->InsertNextPoint(point.easting, point.northing, height ? height(point) : 0.0);
    }
    polygons->InsertNextCell(static_cast<int>(outline.size()));
    for (std::size_t i = 0; i < outline.size(); ++i) {
      polygons->InsertCellPoint(first + static_cast<vtkIdType>(i));
    }
  };

  for (const MobMarker& marker : markers) {
    if (marker.kind != kind) {
      continue;
    }
    if (marker.head_here > 0 && marker.paddock < paddocks.size()) {
      const std::vector<core::Point2D> animals =
          scatter_within(paddocks[marker.paddock], marker.paddock, marker.head_here, kind);
      if (!animals.empty()) {
        for (const core::Point2D& animal : animals) {
          draw_at(animal);
        }
        continue;
      }
    }
    // Nothing to spread over: one mark for the whole mob, at the size asked
    // for. Not enlarged to stand for the mob - a caller that asks for a size
    // gets that size, and a mark that silently drew at five times it would make
    // the scale on the map a thing you cannot trust.
    draw_at(marker.at);
  }

  into->SetPoints(points);
  into->SetPolys(polygons);
  into->Modified();
}

}  // namespace paddock::viz
