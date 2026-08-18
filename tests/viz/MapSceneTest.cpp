// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Where the map puts things.
//
// ADR 0010 keeps the value-to-colour mapping out of VTK so it can be tested
// without a display, and that is what ColourScaleTest covers. What it does not
// cover is placement, and placement is where this went wrong: the field was
// drawn half a cell south-west of the ground it described, and a whole cell
// short in each direction, and the map looked perfectly reasonable that way for
// as long as there was nothing on it to compare against.
//
// None of this needs a window. MapScene builds its data structures on
// construction and fills them in show(); a render window is only wanted when
// somebody wants pixels.

#include <gtest/gtest.h>

#include <vector>

#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/viz/MapScene.hpp>

namespace paddock::viz {
namespace {

constexpr double kCellSize = 25.0;
constexpr double kOriginEasting = 1570000.0;
constexpr double kOriginNorthing = 5180000.0;

core::Raster<double> test_raster(std::size_t cols, std::size_t rows) {
  core::GeoTransform transform;
  transform.origin_easting = kOriginEasting;
  transform.origin_northing = kOriginNorthing;
  transform.cell_size = kCellSize;

  core::Raster<double> raster(cols, rows, transform, 0.0);
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      raster(col, row) = static_cast<double>((row * cols) + col);
    }
  }
  return raster;
}

// The one that was wrong.
//
// A vtkImageData of N samples spans (N - 1) spacings, because its samples are
// points and not areas. Anchoring sample zero on the corner of the farm
// therefore drew every value half a cell south-west of where it belonged. The
// bounds a reader should get are the outermost CELL CENTRES: half a cell inside
// the farm on every side, which is as far as point samples can honestly reach.
TEST(MapSceneTest, TheFieldIsPlacedOnCellCentresRatherThanOnTheCorner) {
  const core::Raster<double> raster = test_raster(48, 32);
  MapScene scene;
  scene.show(raster, ColourScale(Ramp::PastureGreen, 0.0, 1536.0), "test");

  ASSERT_TRUE(scene.has_field());
  const core::BoundingBox bounds = scene.field_bounds();
  const double half = kCellSize / 2.0;

  // Verification, not a pin: 48 columns of 25 m is 1200 m, and the west-most
  // cell centre is 12.5 m in from the west edge.
  EXPECT_NEAR(bounds.min_easting, kOriginEasting + half, 1e-9);
  EXPECT_NEAR(bounds.max_easting, kOriginEasting + 1200.0 - half, 1e-9);
  EXPECT_NEAR(bounds.min_northing, kOriginNorthing - 800.0 + half, 1e-9);
  EXPECT_NEAR(bounds.max_northing, kOriginNorthing - half, 1e-9);
}

// Stated the other way round, because the symptom was an asymmetry: the field
// sat inside the fences along the north edge and met them along the south. Any
// inset there is must be the same on all four sides.
TEST(MapSceneTest, TheFieldIsInsetEquallyOnEverySide) {
  const core::Raster<double> raster = test_raster(20, 12);
  MapScene scene;
  scene.show(raster, ColourScale(Ramp::PastureGreen, 0.0, 240.0), "test");

  const core::BoundingBox bounds = scene.field_bounds();
  const double west = bounds.min_easting - kOriginEasting;
  const double east = (kOriginEasting + (20 * kCellSize)) - bounds.max_easting;
  const double north = kOriginNorthing - bounds.max_northing;
  const double south = bounds.min_northing - (kOriginNorthing - (12 * kCellSize));

  EXPECT_NEAR(west, east, 1e-9);
  EXPECT_NEAR(north, south, 1e-9);
  EXPECT_NEAR(west, north, 1e-9);
}

// Containment, which is weaker than the two above and worth having anyway: the
// fences and the field are built by different machinery, and the field must
// never spill outside the farm it is drawn on. This one would have passed
// against the old placement - the field was shifted south-west and short, so it
// stayed inside - which is why the alignment is pinned by cell centres rather
// than by this.
TEST(MapSceneTest, TheFencesEncloseTheFieldTheyAreDrawnOver) {
  const core::Raster<double> raster = test_raster(48, 32);
  core::BoundingBox farm = core::BoundingBox::empty();
  farm.expand_to_include(core::Point2D{kOriginEasting, kOriginNorthing - 800.0});
  farm.expand_to_include(core::Point2D{kOriginEasting + 1200.0, kOriginNorthing});

  MapScene scene;
  scene.show(raster, ColourScale(Ramp::PastureGreen, 0.0, 1536.0), "test");
  scene.set_boundaries({core::Polygon::rectangle(core::Point2D{farm.min_easting, farm.min_northing},
                                                 1200.0, 800.0)});

  const core::BoundingBox field = scene.field_bounds();
  EXPECT_GE(field.min_easting, farm.min_easting);
  EXPECT_LE(field.max_easting, farm.max_easting);
  EXPECT_GE(field.min_northing, farm.min_northing);
  EXPECT_LE(field.max_northing, farm.max_northing);
}

// Every paddock gets a ring, and the grazed ones get a second one drawn over
// the top. A paddock that lost its closing segment would show with one fence
// missing, which reads as a gate rather than as a bug.
TEST(MapSceneTest, EveryPaddockIsOneClosedRingAndGrazedOnesAreDrawnTwice) {
  std::vector<core::Polygon> paddocks;
  paddocks.reserve(6);
  for (int i = 0; i < 6; ++i) {
    paddocks.push_back(core::Polygon::rectangle(
        core::Point2D{kOriginEasting + (i * 100.0), kOriginNorthing - 100.0}, 100.0, 100.0));
  }

  MapScene scene;
  scene.set_boundaries(paddocks);
  EXPECT_EQ(scene.fence_ring_count(), 6U);
  EXPECT_EQ(scene.grazed_ring_count(), 0U);

  scene.show_grazed({2, 3});
  EXPECT_EQ(scene.fence_ring_count(), 6U) << "highlighting a paddock should not remove a fence";
  EXPECT_EQ(scene.grazed_ring_count(), 2U);

  // Set stocking: the mob has the run of the farm, so every paddock is grazed.
  scene.show_grazed({0, 1, 2, 3, 4, 5});
  EXPECT_EQ(scene.grazed_ring_count(), 6U);
}

// An index that is not a paddock is ignored rather than read off the end. It
// can happen: the timeline holds days from the run that has just finished, and
// a new run with fewer paddocks can be shown before the day is reset.
TEST(MapSceneTest, AnIndexThatIsNotAPaddockIsIgnored) {
  MapScene scene;
  scene.set_boundaries({core::Polygon::rectangle(
      core::Point2D{kOriginEasting, kOriginNorthing - 100.0}, 100.0, 100.0)});

  scene.show_grazed({0, 7, 99});
  EXPECT_EQ(scene.grazed_ring_count(), 1U);
}

TEST(MapSceneTest, AnEmptyRasterLeavesTheSceneWithoutAField) {
  MapScene scene;
  scene.show(core::Raster<double>(), ColourScale(Ramp::PastureGreen, 0.0, 1.0), "test");
  EXPECT_FALSE(scene.has_field());
}

}  // namespace
}  // namespace paddock::viz
