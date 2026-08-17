// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <gtest/gtest.h>

#include <vector>

#include <paddock/core/Geometry.hpp>

namespace paddock::core {
namespace {

// A 100 m x 200 m block placed on plausible NZTM2000 coordinates for the
// Canterbury Plains, so the tests exercise the magnitudes real data arrives at.
constexpr double kEasting = 1570000.0;
constexpr double kNorthing = 5180000.0;

TEST(PolygonTest, RectangleHasExpectedAreaAndPerimeter) {
  const Polygon paddock = Polygon::rectangle({kEasting, kNorthing}, 100.0, 200.0);

  EXPECT_EQ(paddock.vertex_count(), 4U);
  EXPECT_TRUE(paddock.is_valid());
  EXPECT_DOUBLE_EQ(paddock.area(), 20000.0);
  EXPECT_DOUBLE_EQ(paddock.area_hectares(), 2.0);
  EXPECT_DOUBLE_EQ(paddock.perimeter(), 600.0);
}

TEST(PolygonTest, WindingOrderShowsInTheSignOfTheArea) {
  const Polygon counter_clockwise = Polygon::rectangle({kEasting, kNorthing}, 100.0, 200.0);
  const Polygon clockwise(std::vector<Point2D>{{kEasting, kNorthing},
                                               {kEasting, kNorthing + 200.0},
                                               {kEasting + 100.0, kNorthing + 200.0},
                                               {kEasting + 100.0, kNorthing}});

  EXPECT_GT(counter_clockwise.signed_area(), 0.0);
  EXPECT_LT(clockwise.signed_area(), 0.0);
  EXPECT_DOUBLE_EQ(counter_clockwise.area(), clockwise.area());
}

TEST(PolygonTest, RepeatedClosingVertexIsDropped) {
  const Polygon paddock(std::vector<Point2D>{{kEasting, kNorthing},
                                             {kEasting + 100.0, kNorthing},
                                             {kEasting + 100.0, kNorthing + 100.0},
                                             {kEasting, kNorthing}});

  EXPECT_EQ(paddock.vertex_count(), 3U);
  EXPECT_DOUBLE_EQ(paddock.area(), 5000.0);
}

TEST(PolygonTest, CentroidOfATriangleIsTheVertexMean) {
  const Polygon triangle(std::vector<Point2D>{{0.0, 0.0}, {300.0, 0.0}, {0.0, 300.0}});

  const Point2D centre = triangle.centroid();

  EXPECT_DOUBLE_EQ(centre.easting, 100.0);
  EXPECT_DOUBLE_EQ(centre.northing, 100.0);
}

TEST(PolygonTest, ContainsDistinguishesInsideFromOutside) {
  const Polygon paddock = Polygon::rectangle({kEasting, kNorthing}, 100.0, 200.0);

  EXPECT_TRUE(paddock.contains({kEasting + 50.0, kNorthing + 100.0}));
  EXPECT_FALSE(paddock.contains({kEasting - 1.0, kNorthing + 100.0}));
  EXPECT_FALSE(paddock.contains({kEasting + 50.0, kNorthing + 201.0}));
}

TEST(PolygonTest, ContainsHandlesAConcaveRing) {
  // An L-shaped paddock: the notch must not be counted as inside.
  const Polygon l_shape(std::vector<Point2D>{
      {0.0, 0.0}, {200.0, 0.0}, {200.0, 100.0}, {100.0, 100.0}, {100.0, 200.0}, {0.0, 200.0}});

  EXPECT_TRUE(l_shape.contains({50.0, 150.0}));
  EXPECT_TRUE(l_shape.contains({150.0, 50.0}));
  EXPECT_FALSE(l_shape.contains({150.0, 150.0}));
  EXPECT_DOUBLE_EQ(l_shape.area(), 30000.0);
}

TEST(PolygonTest, DegenerateRingsAreNotValid) {
  const Polygon empty;
  const Polygon line(std::vector<Point2D>{{0.0, 0.0}, {100.0, 0.0}});
  const Polygon collinear(std::vector<Point2D>{{0.0, 0.0}, {100.0, 0.0}, {200.0, 0.0}});

  EXPECT_FALSE(empty.is_valid());
  EXPECT_FALSE(line.is_valid());
  EXPECT_FALSE(collinear.is_valid());
  EXPECT_FALSE(collinear.contains({50.0, 0.0}));
}

TEST(BoundingBoxTest, BoundsOfAPolygonCoverEveryVertex) {
  const Polygon paddock = Polygon::rectangle({kEasting, kNorthing}, 100.0, 200.0);
  const BoundingBox box = paddock.bounds();

  EXPECT_DOUBLE_EQ(box.min_easting, kEasting);
  EXPECT_DOUBLE_EQ(box.max_easting, kEasting + 100.0);
  EXPECT_DOUBLE_EQ(box.min_northing, kNorthing);
  EXPECT_DOUBLE_EQ(box.max_northing, kNorthing + 200.0);
  EXPECT_DOUBLE_EQ(box.area(), 20000.0);
  EXPECT_TRUE(box.contains({kEasting + 1.0, kNorthing + 1.0}));
}

TEST(BoundingBoxTest, AnEmptyBoxContainsNothing) {
  const BoundingBox box = BoundingBox::empty();

  EXPECT_TRUE(box.is_empty());
  EXPECT_DOUBLE_EQ(box.width(), 0.0);
  EXPECT_FALSE(box.contains({kEasting, kNorthing}));
}

}  // namespace
}  // namespace paddock::core
