// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Slope and aspect, checked against arithmetic rather than against another
// implementation's output.
//
// This is why the synthetic surface in SyntheticTerrain is analytic: on a
// tilted plane the answers are closed-form, so a test can assert the number a
// reader can work out on paper. A random surface would only ever support
// "looks plausible", and a plausible-looking aspect field is exactly what a
// sign error produces.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

#include <paddock/core/SyntheticTerrain.hpp>
#include <paddock/core/Topography.hpp>

namespace paddock::core {
namespace {

constexpr double kWest = 1570000.0;
constexpr double kSouth = 5179000.0;
constexpr double kCellSizeM = 10.0;

BoundingBox farm_area() {
  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{kWest, kSouth});
  area.expand_to_include(Point2D{kWest + 400.0, kSouth + 300.0});
  return area;
}

/// A plane rising `east` metres per metre east and `north` per metre north.
Raster<double> plane(double rise_east, double rise_north) {
  SyntheticSurface surface;
  surface.reference_easting = kWest;
  surface.reference_northing = kSouth;
  surface.base_elevation_m = 100.0;
  surface.gradient_east = rise_east;
  surface.gradient_north = rise_north;
  surface.undulation_amplitude_m = 0.0;
  return SyntheticElevationSource(surface).fetch(farm_area(), kCellSizeM);
}

/// Cells away from the edge, where the 3x3 window is real rather than made up
/// by repeating the boundary.
template <typename Check>
void for_each_interior_cell(const Raster<double>& raster, Check check) {
  for (std::size_t row = 1; row + 1 < raster.rows(); ++row) {
    for (std::size_t col = 1; col + 1 < raster.cols(); ++col) {
      check(col, row);
    }
  }
}

// A plane falling 1 m in 10 has a slope of exactly atan(0.1) = 5.7106 degrees,
// everywhere, whichever way it faces.
TEST(TopographyTest, APlaneHasTheSlopeArithmeticSaysItDoes) {
  const Topography ground = topography_of(plane(0.0, -0.1));

  const double expected = std::atan(0.1) * 57.29577951308232;
  for_each_interior_cell(ground.slope_degrees, [&](std::size_t col, std::size_t row) {
    ASSERT_NEAR(ground.slope_degrees(col, row), expected, 1e-9) << "at " << col << ", " << row;
  });
  EXPECT_NEAR(expected, 5.7105931, 1e-6);
}

// The four cardinal cases, each with an aspect a reader can name. Ground that
// falls to the north faces north.
TEST(TopographyTest, AspectIsTheDirectionTheGroundFallsTowards) {
  struct Case {
    const char* description;
    double rise_east;
    double rise_north;
    double expected_aspect;
  };

  constexpr std::array<Case, 4> kCases = {{
      {"falls north", 0.0, -0.1, 0.0},
      {"falls east", -0.1, 0.0, 90.0},
      {"falls south", 0.0, 0.1, 180.0},
      {"falls west", 0.1, 0.0, 270.0},
  }};

  for (const Case& scenario : kCases) {
    const Topography ground = topography_of(plane(scenario.rise_east, scenario.rise_north));
    for_each_interior_cell(ground.aspect_degrees, [&](std::size_t col, std::size_t row) {
      const double aspect = ground.aspect_degrees(col, row);
      // 0 and 360 are the same bearing; compare the shorter way round.
      double difference = std::fmod(std::fabs(aspect - scenario.expected_aspect), 360.0);
      if (difference > 180.0) {
        difference = 360.0 - difference;
      }
      ASSERT_NEAR(difference, 0.0, 1e-9) << scenario.description << " gave " << aspect;
    });
  }
}

// A diagonal, where an axis mix-up shows up as a bearing 90 degrees out rather
// than as an obviously wrong answer. Rising east and rising north means falling
// to the south-west: bearing 180 + atan(0.05/0.10) = 206.565 degrees.
TEST(TopographyTest, ADiagonalSlopeGetsTheBearingBetweenTheAxes) {
  const Topography ground = topography_of(plane(0.05, 0.10));

  const double expected_aspect = 180.0 + (std::atan(0.05 / 0.10) * 57.29577951308232);
  const double expected_slope = std::atan(std::hypot(0.05, 0.10)) * 57.29577951308232;

  for_each_interior_cell(ground.aspect_degrees, [&](std::size_t col, std::size_t row) {
    ASSERT_NEAR(ground.aspect_degrees(col, row), expected_aspect, 1e-9);
    ASSERT_NEAR(ground.slope_degrees(col, row), expected_slope, 1e-9);
  });
  EXPECT_NEAR(expected_aspect, 206.5650512, 1e-6);
  EXPECT_NEAR(expected_slope, 6.3793702, 1e-6);
}

// Flat ground has no aspect, and says so. A sentinel that looks like a bearing
// would make every flat paddock face north.
TEST(TopographyTest, FlatGroundHasNoAspectRatherThanNorth) {
  const Topography ground = topography_of(plane(0.0, 0.0));

  for (std::size_t row = 0; row < ground.slope_degrees.rows(); ++row) {
    for (std::size_t col = 0; col < ground.slope_degrees.cols(); ++col) {
      ASSERT_DOUBLE_EQ(ground.slope_degrees(col, row), 0.0);
      ASSERT_TRUE(std::isnan(ground.aspect_degrees(col, row))) << "at " << col << ", " << row;
    }
  }
}

// Slope does not care which way the ground faces: two planes of the same
// steepness in opposite directions have the same slope and opposite aspects.
TEST(TopographyTest, OppositeSlopesShareASteepnessAndDifferBy180Degrees) {
  const Topography north_facing = topography_of(plane(0.0, -0.2));
  const Topography south_facing = topography_of(plane(0.0, 0.2));

  for_each_interior_cell(north_facing.slope_degrees, [&](std::size_t col, std::size_t row) {
    ASSERT_NEAR(north_facing.slope_degrees(col, row), south_facing.slope_degrees(col, row), 1e-12);
    ASSERT_NEAR(north_facing.aspect_degrees(col, row), 0.0, 1e-9);
    ASSERT_NEAR(south_facing.aspect_degrees(col, row), 180.0, 1e-9);
  });
}

// The output keeps the input's georeferencing, so a slope raster can be shown
// on the map and exported beside the DEM it came from.
TEST(TopographyTest, TheResultIsGeoreferencedLikeTheElevation) {
  const Raster<double> elevation = plane(0.02, -0.03);

  const Topography ground = topography_of(elevation);

  EXPECT_EQ(ground.slope_degrees.cols(), elevation.cols());
  EXPECT_EQ(ground.slope_degrees.rows(), elevation.rows());
  EXPECT_DOUBLE_EQ(ground.slope_degrees.transform().origin_easting,
                   elevation.transform().origin_easting);
  EXPECT_DOUBLE_EQ(ground.aspect_degrees.transform().origin_northing,
                   elevation.transform().origin_northing);
  EXPECT_DOUBLE_EQ(ground.aspect_degrees.transform().cell_size, elevation.transform().cell_size);
}

// Cell size is in the denominator of every gradient, so getting it wrong scales
// every slope. A plane sampled at 20 m has the same slope as at 10 m.
TEST(TopographyTest, SlopeDoesNotDependOnTheSamplingResolution) {
  SyntheticSurface surface;
  surface.reference_easting = kWest;
  surface.reference_northing = kSouth;
  surface.gradient_east = 0.0;
  surface.gradient_north = -0.15;
  surface.undulation_amplitude_m = 0.0;
  const SyntheticElevationSource source(surface);

  const Topography fine = topography_of(source.fetch(farm_area(), 10.0));
  const Topography coarse = topography_of(source.fetch(farm_area(), 20.0));

  const double expected = std::atan(0.15) * 57.29577951308232;
  EXPECT_NEAR(fine.slope_degrees(5, 5), expected, 1e-9);
  EXPECT_NEAR(coarse.slope_degrees(5, 5), expected, 1e-9);
}

// A single cell has no neighbour to difference against. Cell size needs no
// check here: Raster's own constructor already refuses a non-positive one, so a
// guard in topography_of would be a branch no test could reach.
TEST(TopographyTest, ARasterTooSmallToHaveAGradientIsRefused) {
  GeoTransform transform;
  transform.origin_easting = kWest;
  transform.origin_northing = kSouth;
  transform.cell_size = kCellSizeM;

  EXPECT_THROW(static_cast<void>(topography_of(Raster<double>(1, 1, transform))),
               std::out_of_range);
  EXPECT_THROW(static_cast<void>(topography_of(Raster<double>(1, 8, transform))),
               std::out_of_range);
}

}  // namespace
}  // namespace paddock::core
