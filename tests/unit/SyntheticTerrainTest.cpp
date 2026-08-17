// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The synthetic terrain adapters: ground and paddocks that need no files, no
// network and no GDAL, so that the whole scientific suite still runs on a
// machine with only a compiler.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <paddock/core/Geometry.hpp>
#include <paddock/core/SyntheticTerrain.hpp>

namespace paddock::core {
namespace {

BoundingBox farm_area() {
  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{1570000.0, 5179000.0});
  area.expand_to_include(Point2D{1571000.0, 5179800.0});
  return area;
}

// A plane is a plane: the elevation at any point is arithmetic a reader can do
// on paper, which is what makes it usable as a fixture for the slope and aspect
// work in task #19.
TEST(SyntheticTerrainTest, APlaneHasExactlyTheElevationArithmeticSaysItDoes) {
  SyntheticSurface surface;
  surface.reference_easting = 1570000.0;
  surface.reference_northing = 5179000.0;
  surface.base_elevation_m = 100.0;
  surface.gradient_east = 0.05;
  surface.gradient_north = -0.10;
  surface.undulation_amplitude_m = 0.0;

  EXPECT_DOUBLE_EQ(surface.elevation_at(Point2D{1570000.0, 5179000.0}), 100.0);
  // 200 m east at 0.05 rise per metre is 10 m up.
  EXPECT_DOUBLE_EQ(surface.elevation_at(Point2D{1570200.0, 5179000.0}), 110.0);
  // 300 m north at -0.10 is 30 m down.
  EXPECT_DOUBLE_EQ(surface.elevation_at(Point2D{1570000.0, 5179300.0}), 70.0);
  // Both together.
  EXPECT_DOUBLE_EQ(surface.elevation_at(Point2D{1570200.0, 5179300.0}), 80.0);
}

// The property ADR 0008 insists on for weather, in space rather than time: what
// a source returns for a piece of ground must not depend on the window asked
// around it. Getting this wrong would make a farm's elevation change when the
// simulated area was extended, and every slope with it.
TEST(SyntheticTerrainTest, TheSameGroundHasTheSameHeightWhateverWindowIsAsked) {
  SyntheticSurface surface;
  surface.undulation_amplitude_m = 6.0;
  const SyntheticElevationSource source(surface);

  const BoundingBox whole = farm_area();
  const Raster<double> all = source.fetch(whole, 10.0);

  // An aligned window 200 m east and 400 m north of the corner: whole-raster
  // column 20 and row 40 are the same ground as the window's column 0, row 0.
  BoundingBox window = BoundingBox::empty();
  window.expand_to_include(Point2D{whole.min_easting + 200.0, whole.min_northing + 100.0});
  window.expand_to_include(Point2D{whole.min_easting + 600.0, whole.min_northing + 400.0});
  const Raster<double> part = source.fetch(window, 10.0);

  ASSERT_EQ(part.cols(), 40U);
  ASSERT_EQ(part.rows(), 30U);
  for (std::size_t row = 0; row < part.rows(); ++row) {
    for (std::size_t col = 0; col < part.cols(); ++col) {
      ASSERT_DOUBLE_EQ(part(col, row), all(col + 20, row + 40))
          << "window cell (" << col << ", " << row << ")";
    }
  }
}

TEST(SyntheticTerrainTest, TheRasterCoversTheAreaRequested) {
  const SyntheticElevationSource source;
  const BoundingBox area = farm_area();

  const Raster<double> elevation = source.fetch(area, 25.0);

  EXPECT_EQ(elevation.cols(), 40U);  // 1000 m at 25 m
  EXPECT_EQ(elevation.rows(), 32U);  // 800 m at 25 m
  EXPECT_DOUBLE_EQ(elevation.transform().cell_size, 25.0);
  EXPECT_DOUBLE_EQ(elevation.transform().origin_easting, area.min_easting);
  // Row 0 is the northernmost, so the transform's origin is the top-left.
  EXPECT_DOUBLE_EQ(elevation.transform().origin_northing, area.max_northing);
}

// Rasterising these into per-paddock masks is task #21, and a gap there is a
// cell belonging to no paddock: never grazed, never noticed.
TEST(SyntheticTerrainTest, PaddocksTileTheFarmWithoutGapOrOverlap) {
  const SyntheticParcelSource source(2.5);
  const BoundingBox area = farm_area();

  const std::vector<Paddock> paddocks = source.fetch(area);

  ASSERT_FALSE(paddocks.empty());
  double total_m2 = 0.0;
  for (const Paddock& paddock : paddocks) {
    EXPECT_TRUE(paddock.boundary.is_valid()) << paddock.name;
    EXPECT_FALSE(paddock.name.empty());
    total_m2 += paddock.boundary.area();
  }
  EXPECT_NEAR(total_m2, area.area(), 1e-6);
}

TEST(SyntheticTerrainTest, PaddocksAreNearTheSizeAskedFor) {
  const SyntheticParcelSource source(2.5);

  const std::vector<Paddock> paddocks = source.fetch(farm_area());

  for (const Paddock& paddock : paddocks) {
    EXPECT_GT(paddock.area_hectares(), 1.5) << paddock.name;
    EXPECT_LT(paddock.area_hectares(), 3.5) << paddock.name;
  }
}

// A farm smaller than one paddock still has a paddock. Returning none would
// read downstream as a farm with no grazeable area.
TEST(SyntheticTerrainTest, AFarmSmallerThanOnePaddockGetsOne) {
  const SyntheticParcelSource source(2.5);
  BoundingBox tiny = BoundingBox::empty();
  tiny.expand_to_include(Point2D{1570000.0, 5179000.0});
  tiny.expand_to_include(Point2D{1570050.0, 5179040.0});

  const std::vector<Paddock> paddocks = source.fetch(tiny);

  ASSERT_EQ(paddocks.size(), 1U);
  EXPECT_NEAR(paddocks.front().boundary.area(), tiny.area(), 1e-6);
}

TEST(SyntheticTerrainTest, EmptyAreasAndImpossibleResolutionsAreRefused) {
  const SyntheticElevationSource elevation;
  const SyntheticParcelSource parcels;
  const BoundingBox nothing = BoundingBox::empty();

  EXPECT_THROW(static_cast<void>(elevation.fetch(nothing, 10.0)), std::out_of_range);
  EXPECT_THROW(static_cast<void>(elevation.fetch(farm_area(), 0.0)), std::out_of_range);
  EXPECT_THROW(static_cast<void>(elevation.fetch(farm_area(), -5.0)), std::out_of_range);
  EXPECT_THROW(static_cast<void>(parcels.fetch(nothing)), std::out_of_range);
  EXPECT_THROW(SyntheticParcelSource(0.0), std::out_of_range);
}

// describe() is what `paddock source test` prints, and a synthetic source has
// to say plainly that it is not data.
TEST(SyntheticTerrainTest, SyntheticSourcesSayTheyAreNotMeasurements) {
  const SyntheticElevationSource elevation;
  const SyntheticParcelSource parcels;

  for (const SourceDescription& description : {elevation.describe(), parcels.describe()}) {
    EXPECT_FALSE(description.name.empty());
    EXPECT_NE(description.licence.find("generated"), std::string::npos) << description.licence;
    EXPECT_FALSE(description.coverage.empty());
    EXPECT_FALSE(description.cadence.empty());
  }
  EXPECT_TRUE(elevation.test_connection().ok);
  EXPECT_TRUE(parcels.test_connection().ok);
}

}  // namespace
}  // namespace paddock::core
