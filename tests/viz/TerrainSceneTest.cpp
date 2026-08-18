// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The terrain view, checked the way the flat map is: by what it puts where.
//
// A three-dimensional view fails in a way a flat one does not - it can be
// perfectly built and simply not visible, because the camera is somewhere else
// or the surface has no extent. So the assertions here are about the geometry
// the scene actually holds, which is the thing a screenshot cannot tell you
// apart from a bad camera angle.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/viz/TerrainScene.hpp>

namespace paddock::viz {
namespace {

constexpr double kCellSize = 25.0;
constexpr double kOriginEasting = 1570000.0;
constexpr double kOriginNorthing = 5180000.0;

core::Raster<double> raster(std::size_t cols, std::size_t rows, double value) {
  core::GeoTransform transform;
  transform.origin_easting = kOriginEasting;
  transform.origin_northing = kOriginNorthing;
  transform.cell_size = kCellSize;
  return {cols, rows, transform, value};
}

/// Ground that falls to the east, so a wrong sign or a dropped axis shows.
core::Raster<double> sloping(std::size_t cols, std::size_t rows) {
  core::Raster<double> ground = raster(cols, rows, 0.0);
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      ground(col, row) = 100.0 - (static_cast<double>(col) * 0.5);
    }
  }
  return ground;
}

TEST(TerrainSceneTest, AnEmptySceneHasNoField) {
  const TerrainScene scene;
  EXPECT_FALSE(scene.has_field());
}

// The one a black screenshot cannot distinguish from a bad camera: the surface
// has to exist and have the farm's extent.
TEST(TerrainSceneTest, TheSurfaceCoversTheFarmAndCarriesItsHeights) {
  const std::size_t cols = 48;
  const std::size_t rows = 32;
  TerrainScene scene;
  scene.show(raster(cols, rows, 2000.0), sloping(cols, rows),
             ColourScale(Ramp::PastureGreen, 1000.0, 3000.0), "cover");

  ASSERT_TRUE(scene.has_field());

  // Verification, not a pin: heights run from 100 m at the west edge down by
  // 0.5 m a column over 48 columns, so 76.5 m at the east edge.
  const auto range = scene.elevation_range();
  EXPECT_NEAR(range.first, 76.5, 1e-9);
  EXPECT_NEAR(range.second, 100.0, 1e-9);

  const double* bounds = scene.renderer()->ComputeVisiblePropBounds();
  ASSERT_NE(bounds, nullptr);
  // Points sit at cell centres, so the surface spans (n - 1) cells: 47 x 25 m
  // across and 31 x 25 m up.
  EXPECT_NEAR(bounds[1] - bounds[0], 47.0 * kCellSize, 1e-6);
  EXPECT_NEAR(bounds[3] - bounds[2], 31.0 * kCellSize, 1e-6);
  EXPECT_GT(bounds[5] - bounds[4], 0.0) << "a sloping farm was drawn with no height at all";
}

// Exaggeration stretches the picture and must not touch the numbers, or a
// reader would take a drawn height for a measured one.
TEST(TerrainSceneTest, ExaggerationStretchesThePictureAndNotTheElevations) {
  const std::size_t cols = 20;
  const std::size_t rows = 16;
  TerrainScene scene;
  scene.show(raster(cols, rows, 2000.0), sloping(cols, rows),
             ColourScale(Ramp::PastureGreen, 1000.0, 3000.0), "cover");

  const double* before = scene.renderer()->ComputeVisiblePropBounds();
  const double drawn_before = before[5] - before[4];

  scene.set_vertical_exaggeration(5.0);
  EXPECT_DOUBLE_EQ(scene.vertical_exaggeration(), 5.0);

  const auto range = scene.elevation_range();
  EXPECT_NEAR(range.second - range.first, 19.0 * 0.5, 1e-9)
      << "exaggeration changed the elevations it was only supposed to draw";

  const double* after = scene.renderer()->ComputeVisiblePropBounds();
  EXPECT_NEAR(after[5] - after[4], drawn_before * 5.0, 1e-6);
}

// Fences sit on the surface, not through it and not floating above it. Checked
// against the ground the scene was given rather than against a redrawing of it.
TEST(TerrainSceneTest, FencesAreDrapedOnTheGround) {
  const std::size_t cols = 48;
  const std::size_t rows = 32;
  TerrainScene scene;
  scene.show(raster(cols, rows, 2000.0), sloping(cols, rows),
             ColourScale(Ramp::PastureGreen, 1000.0, 3000.0), "cover");

  std::vector<core::Polygon> paddocks;
  paddocks.reserve(2);
  for (int i = 0; i < 2; ++i) {
    paddocks.push_back(core::Polygon::rectangle(
        core::Point2D{kOriginEasting + (i * 300.0), kOriginNorthing - 300.0}, 300.0, 300.0));
  }
  scene.set_boundaries(paddocks);
  EXPECT_EQ(scene.fence_ring_count(), 2U);
  EXPECT_EQ(scene.grazed_ring_count(), 0U);

  scene.show_grazed({1});
  EXPECT_EQ(scene.grazed_ring_count(), 1U);
  EXPECT_EQ(scene.fence_ring_count(), 2U) << "highlighting a paddock removed a fence";

  // The fences are inside the height range of the ground they lie on, give or
  // take the small lift that keeps the line off the surface.
  const double* bounds = scene.renderer()->ComputeVisiblePropBounds();
  const auto range = scene.elevation_range();
  EXPECT_GE(bounds[5], range.first);
  EXPECT_LE(bounds[4], range.second + 1.0);
}

// A field and an elevation of different shapes are two different farms, and
// draping one over the other would produce a picture of neither.
TEST(TerrainSceneTest, MismatchedShapesAreRefused) {
  TerrainScene scene;
  EXPECT_THROW(scene.show(raster(48, 32, 2000.0), sloping(20, 16),
                          ColourScale(Ramp::PastureGreen, 0.0, 1.0), "cover"),
               std::invalid_argument);
}

}  // namespace
}  // namespace paddock::viz
