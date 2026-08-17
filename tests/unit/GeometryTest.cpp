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

// Moving a polygon does not change its area, and the arithmetic has to agree.
//
// The shoelace formula applied to absolute coordinates loses its precision to
// cancellation: the area is what remains after subtracting two products of the
// coordinates, so the further the ring sits from the origin, the more of the
// result is noise. On NZTM2000 that cost a farm's worth of paddocks 23 cm2,
// which only a macOS run caught - Apple Silicon's fused multiply-add rounds the
// products differently, and the x86 builds had been getting away with it.
//
// A single NZTM rectangle is therefore not a usable regression guard: on x86
// the old code returned exactly the right answer for one of them, tolerance or
// no tolerance. The third case below is, because at a billion metres the
// products reach 1e18, where a double's last bit is worth about 128 - so the
// naive formula cannot return 25 000 on any conforming implementation. It is
// not a coordinate New Zealand uses; it is the magnitude at which the property
// being asserted - that area does not depend on position - stops holding by
// luck and has to hold by construction.
TEST(PolygonTest, AreaDoesNotDependOnDistanceFromTheProjectionOrigin) {
  constexpr double kWidth = 200.0;
  constexpr double kHeight = 125.0;

  const Polygon at_origin = Polygon::rectangle(Point2D{0.0, 0.0}, kWidth, kHeight);
  const Polygon on_a_canterbury_farm =
      Polygon::rectangle(Point2D{1570000.0, 5179000.0}, kWidth, kHeight);

  // Bit-identical, not merely close. Measuring from the first vertex makes the
  // two computations the same arithmetic on the same offsets - 1570000.0 + 200.0
  // - 1570000.0 is exact in binary floating point - so anything less than
  // equality means the absolute coordinates are still in the sum somewhere. A
  // tolerance was tried first and had no teeth: on x86 the old code was already
  // within a nanometre squared for one rectangle, and only showed itself after
  // a hundred of them were added up on a machine that rounds differently.
  EXPECT_DOUBLE_EQ(at_origin.area(), kWidth * kHeight);
  EXPECT_DOUBLE_EQ(on_a_canterbury_farm.area(), kWidth * kHeight);
  EXPECT_DOUBLE_EQ(on_a_canterbury_farm.area(), at_origin.area());

  const Polygon far_away = Polygon::rectangle(Point2D{1.0e9, 1.0e9}, kWidth, kHeight);
  EXPECT_DOUBLE_EQ(far_away.area(), kWidth * kHeight);
}

}  // namespace
}  // namespace paddock::core
