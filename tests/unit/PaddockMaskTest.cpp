// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Turning paddock boundaries into the cells they own.
//
// The property that matters is that the result is a partition: every cell has
// exactly one owner or none, no cell has two, and the counts add up. Everything
// downstream - grazing, rotation, per-paddock reporting - assumes it, and an
// overlap or a gap would show up there as feed appearing or disappearing rather
// than as a geometry error.

#include <gtest/gtest.h>

#include <cstddef>
#include <numeric>
#include <string>
#include <vector>

#include <paddock/core/PaddockMask.hpp>
#include <paddock/core/SyntheticTerrain.hpp>

namespace paddock::core {
namespace {

constexpr double kWest = 1570000.0;
constexpr double kSouth = 5179000.0;
constexpr double kFarmWidth = 400.0;
constexpr double kFarmHeight = 300.0;

BoundingBox farm_area() {
  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{kWest, kSouth});
  area.expand_to_include(Point2D{kWest + kFarmWidth, kSouth + kFarmHeight});
  return area;
}

/// A grid over the farm, used only for its shape and georeferencing.
Raster<double> grid_at(double cell_size_m) {
  return SyntheticElevationSource().fetch(farm_area(), cell_size_m);
}

/// Paddocks that tile the farm exactly, from the synthetic source that already
/// guarantees no gap and no overlap.
std::vector<Paddock> tiling_paddocks(double hectares) {
  return SyntheticParcelSource(hectares).fetch(farm_area());
}

// The partition property, stated three ways because each failure looks
// different downstream: a lost cell is feed that vanishes, a doubled cell is
// feed counted twice, and a miscount is a paddock that grazes at the wrong rate.
TEST(PaddockMaskTest, EveryCellHasExactlyOneOwnerOrNone) {
  const Raster<double> shape = grid_at(10.0);
  const std::vector<Paddock> paddocks = tiling_paddocks(2.0);

  const PaddockMask mask(shape, paddocks);

  EXPECT_EQ(mask.contested_cells(), 0U) << "two paddocks claimed the same cell";

  const std::size_t owned =
      std::accumulate(mask.cell_counts().begin(), mask.cell_counts().end(), std::size_t{0});
  EXPECT_EQ(owned + mask.unowned_cells(), shape.size())
      << "cells went missing or were counted twice";
}

// Paddocks that tile the farm should leave nothing unowned, because every cell
// centre is inside one of them.
TEST(PaddockMaskTest, TilingPaddocksLeaveNoGroundUnowned) {
  const Raster<double> shape = grid_at(10.0);

  const PaddockMask mask(shape, tiling_paddocks(2.0));

  EXPECT_EQ(mask.unowned_cells(), 0U);
}

// Boundaries that fall on cell edges rasterise exactly, which is worth
// asserting rather than assuming: it says there is no systematic bias in the
// rule, only the error that comes from cutting cells.
//
// The synthetic paddocks divide this farm into 200 m by 150 m blocks, and 200
// and 150 are whole numbers of 25 m, 10 m and 5 m cells alike.
TEST(PaddockMaskTest, BoundariesOnCellEdgesRasteriseExactly) {
  const std::vector<Paddock> paddocks = tiling_paddocks(2.0);

  for (const double cell : {25.0, 10.0, 5.0}) {
    const PaddockMask mask(grid_at(cell), paddocks);
    EXPECT_DOUBLE_EQ(mask.area_error_hectares(paddocks), 0.0) << "at " << cell << " m cells";
  }
}

// The general case: a boundary that cuts across cells. A triangle's hypotenuse
// does so at every resolution, so the error is real and has to shrink as the
// cells get smaller. That is the price of giving whole cells to one owner, and
// the point is that it is bounded and predictable rather than arbitrary.
TEST(PaddockMaskTest, TheAreaErrorFallsAsTheGridGetsFiner) {
  const std::vector<Paddock> paddocks{
      Paddock{"Triangle", Polygon(std::vector<Point2D>{
                              {kWest, kSouth}, {kWest + 300.0, kSouth}, {kWest, kSouth + 300.0}})}};

  const double coarse = PaddockMask(grid_at(25.0), paddocks).area_error_hectares(paddocks);
  const double fine = PaddockMask(grid_at(10.0), paddocks).area_error_hectares(paddocks);
  const double finer = PaddockMask(grid_at(5.0), paddocks).area_error_hectares(paddocks);

  EXPECT_GT(coarse, 0.0) << "a cut boundary should cost something";
  EXPECT_LT(fine, coarse) << "coarse " << coarse << ", fine " << fine;
  EXPECT_LT(finer, fine) << "fine " << fine << ", finer " << finer;
  // The ordering above is the property being tested and follows from the rule.
  // This last bound is a regression pin from this implementation: it says the
  // error is small in absolute terms, not that a tenth of a hectare is the
  // right threshold for anything in particular.
  EXPECT_LT(finer, 0.1) << "at 5 m cells the triangle should be within a tenth of a hectare";
}

// The total area is conserved even where individual paddocks are rounded, since
// every cell the farm covers goes to exactly one of them.
TEST(PaddockMaskTest, TheWholeFarmIsAccountedFor) {
  const Raster<double> shape = grid_at(10.0);
  const std::vector<Paddock> paddocks = tiling_paddocks(2.0);

  const PaddockMask mask(shape, paddocks);

  double rasterised = 0.0;
  for (std::size_t i = 0; i < paddocks.size(); ++i) {
    rasterised += mask.rasterised_hectares(i);
  }
  const double farm_hectares = (kFarmWidth * kFarmHeight) / kSquareMetresPerHectare;
  EXPECT_NEAR(rasterised, farm_hectares, 1e-9);
}

// Ground outside every paddock belongs to nobody rather than to the nearest one.
// Grazing ground the farm does not own is the failure this prevents.
TEST(PaddockMaskTest, GroundOutsideEveryPaddockIsUnowned) {
  // One paddock in the south-west corner, on a grid covering the whole farm.
  const std::vector<Paddock> paddocks{
      Paddock{"Corner", Polygon::rectangle(Point2D{kWest, kSouth}, 100.0, 100.0)}};
  const Raster<double> shape = grid_at(10.0);

  const PaddockMask mask(shape, paddocks);

  EXPECT_EQ(mask.cell_counts().front(), 100U);  // 10 x 10 cells
  EXPECT_EQ(mask.unowned_cells(), shape.size() - 100U);
  EXPECT_EQ(mask.owner(0, shape.rows() - 1), 0U) << "the south-west corner belongs to the paddock";
  EXPECT_EQ(mask.owner(shape.cols() - 1, 0), PaddockMask::kUnowned)
      << "the north-east corner belongs to nobody";
}

// Overlapping paddocks are counted rather than quietly resolved. Cadastral data
// contains slivers, and a farm whose paddocks overlap should be looked at
// before its areas are trusted.
TEST(PaddockMaskTest, OverlapsAreCountedRatherThanHidden) {
  const std::vector<Paddock> paddocks{
      Paddock{"First", Polygon::rectangle(Point2D{kWest, kSouth}, 200.0, 200.0)},
      Paddock{"Second", Polygon::rectangle(Point2D{kWest + 100.0, kSouth}, 200.0, 200.0)}};

  const PaddockMask mask(grid_at(10.0), paddocks);

  EXPECT_GT(mask.contested_cells(), 0U) << "an overlap of 100 m by 200 m went unnoticed";
  // First wins, so it keeps its whole 200 x 200 block and the second loses the
  // strip they share.
  EXPECT_EQ(mask.cell_counts()[0], 400U);
  EXPECT_EQ(mask.cell_counts()[1], 200U);
}

TEST(PaddockMaskTest, CellsAndPaddocksOutsideTheGridAreRefused) {
  const Raster<double> shape = grid_at(25.0);
  const std::vector<Paddock> paddocks = tiling_paddocks(2.0);
  const PaddockMask mask(shape, paddocks);

  EXPECT_THROW(static_cast<void>(mask.owner(mask.cols(), 0)), std::out_of_range);
  EXPECT_THROW(static_cast<void>(mask.rasterised_hectares(paddocks.size())), std::out_of_range);
  EXPECT_THROW(static_cast<void>(mask.area_error_hectares({})), std::invalid_argument);
}

}  // namespace
}  // namespace paddock::core
