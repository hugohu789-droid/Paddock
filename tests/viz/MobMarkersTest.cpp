// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Which animal a mark on the map is.
//
// A map that draws stock without saying what they are withholds the one thing
// a person standing at the gate could tell you without being told. Shape
// carries it and colour reinforces it, so both are asserted - and so is the
// separation, because two mobs on one farm have to be told apart by somebody
// who cannot separate the colours.

#include <gtest/gtest.h>

#include <vector>

#include <paddock/core/AnimalEnergy.hpp>
#include <paddock/viz/MobMarkers.hpp>

namespace paddock::viz {
namespace {

constexpr double kSize = 40.0;

/// No boundaries to spread over, so a mark stands for its whole mob. Most of
/// this file is about which shape a species gets, which the fallback draws too.
const std::vector<core::Polygon> kNoPaddocks;

/// A square paddock, as a thing to scatter inside.
core::Polygon square_paddock(double west, double south, double side) {
  return core::Polygon(
      {{west, south}, {west + side, south}, {west + side, south + side}, {west, south + side}});
}

MobMarker at(double easting, double northing, core::AnimalKind kind, int head = 100) {
  MobMarker marker;
  marker.at = {easting, northing};
  marker.kind = kind;
  marker.head = head;
  return marker;
}

vtkIdType corners_of(core::AnimalKind kind) {
  const std::vector<MobMarker> markers{at(1000.0, 2000.0, kind)};
  vtkNew<vtkPolyData> shape;
  build_mob_markers(markers, kind, kSize, {}, kNoPaddocks, shape);
  if (shape->GetNumberOfPolys() != 1) {
    return -1;
  }
  return shape->GetNumberOfPoints();
}

// Every kind has a shape of its own. The particular shapes are arbitrary; that
// no two share one is not.
TEST(MobMarkersTest, EachKindIsADifferentShape) {
  const vtkIdType sheep = corners_of(core::AnimalKind::Sheep);
  const vtkIdType cattle = corners_of(core::AnimalKind::Cattle);
  const vtkIdType deer = corners_of(core::AnimalKind::Deer);

  EXPECT_EQ(cattle, 4) << "cattle are drawn as a square";
  EXPECT_EQ(deer, 3) << "deer are drawn as a triangle";
  EXPECT_GT(sheep, 8) << "sheep are drawn as a circle, which needs enough sides to read as one";

  EXPECT_NE(sheep, cattle);
  EXPECT_NE(sheep, deer);
  EXPECT_NE(cattle, deer);
}

// An animal whose species file does not say what it is gets drawn as one, not
// as a guess at the nearest thing.
TEST(MobMarkersTest, AnUndeclaredAnimalGetsItsOwnMark) {
  EXPECT_EQ(corners_of(core::AnimalKind::Other), 4);

  const std::array<double, 3> other = colour_of(core::AnimalKind::Other);
  for (const core::AnimalKind kind :
       {core::AnimalKind::Sheep, core::AnimalKind::Cattle, core::AnimalKind::Deer}) {
    EXPECT_NE(colour_of(kind), other) << "an unknown animal is coloured as a known one";
  }
}

// Every kind is coloured differently, so colour reinforces the shape rather
// than contradicting it.
TEST(MobMarkersTest, EveryKindHasItsOwnColour) {
  for (std::size_t i = 0; i < marker_kinds().size(); ++i) {
    for (std::size_t j = i + 1; j < marker_kinds().size(); ++j) {
      EXPECT_NE(colour_of(marker_kinds()[i]), colour_of(marker_kinds()[j]))
          << "kinds " << i << " and " << j << " share a colour";
    }
  }
}

// A layer holds one kind. Sheep and cattle on one farm must not end up in the
// same actor, or one of them would be drawn in the other's colour.
TEST(MobMarkersTest, ALayerTakesOnlyItsOwnKind) {
  const std::vector<MobMarker> mixed{at(0.0, 0.0, core::AnimalKind::Sheep),
                                     at(200.0, 0.0, core::AnimalKind::Cattle),
                                     at(400.0, 0.0, core::AnimalKind::Sheep)};

  vtkNew<vtkPolyData> sheep;
  vtkNew<vtkPolyData> cattle;
  vtkNew<vtkPolyData> deer;
  build_mob_markers(mixed, core::AnimalKind::Sheep, kSize, {}, kNoPaddocks, sheep);
  build_mob_markers(mixed, core::AnimalKind::Cattle, kSize, {}, kNoPaddocks, cattle);
  build_mob_markers(mixed, core::AnimalKind::Deer, kSize, {}, kNoPaddocks, deer);

  EXPECT_EQ(sheep->GetNumberOfPolys(), 2);
  EXPECT_EQ(cattle->GetNumberOfPolys(), 1);
  EXPECT_EQ(deer->GetNumberOfPolys(), 0) << "a kind with no stock in it should draw nothing";
}

// A mark is a farm-sized object, so it is built in metres on the ground rather
// than in pixels: zooming in makes a mob larger, as it makes a paddock larger.
TEST(MobMarkersTest, AMarkIsTheSizeItWasAskedForOnTheGround) {
  const std::vector<MobMarker> markers{at(1000.0, 2000.0, core::AnimalKind::Cattle)};
  vtkNew<vtkPolyData> shape;
  build_mob_markers(markers, core::AnimalKind::Cattle, kSize, {}, kNoPaddocks, shape);

  std::array<double, 6> bounds{};
  shape->GetBounds(bounds.data());
  EXPECT_NEAR(bounds[1] - bounds[0], kSize, 1e-9);
  EXPECT_NEAR(bounds[3] - bounds[2], kSize, 1e-9);
  // Centred on where the mob is, not hung off the corner.
  EXPECT_NEAR((bounds[0] + bounds[1]) / 2.0, 1000.0, 1e-9);
  EXPECT_NEAR((bounds[2] + bounds[3]) / 2.0, 2000.0, 1e-9);
}

// On terrain the mark sits on the surface, which is what the height function is
// for. A flat scene passes none and everything lies at zero.
TEST(MobMarkersTest, AMarkStandsOnTheGroundItIsGiven) {
  const std::vector<MobMarker> markers{at(1000.0, 2000.0, core::AnimalKind::Sheep)};

  vtkNew<vtkPolyData> flat;
  build_mob_markers(markers, core::AnimalKind::Sheep, kSize, {}, kNoPaddocks, flat);
  std::array<double, 6> flat_bounds{};
  flat->GetBounds(flat_bounds.data());
  EXPECT_DOUBLE_EQ(flat_bounds[4], 0.0);
  EXPECT_DOUBLE_EQ(flat_bounds[5], 0.0);

  vtkNew<vtkPolyData> raised;
  build_mob_markers(
      markers, core::AnimalKind::Sheep, kSize, [](core::Point2D) { return 37.5; }, kNoPaddocks,
      raised);
  std::array<double, 6> raised_bounds{};
  raised->GetBounds(raised_bounds.data());
  EXPECT_DOUBLE_EQ(raised_bounds[4], 37.5);
  EXPECT_DOUBLE_EQ(raised_bounds[5], 37.5);
}

}  // namespace

// Every animal inside its own fence.
//
// A dot outside the boundary is a sheep in the neighbour's crop, and it would
// be the map claiming something about where stock are that is not merely
// unmodelled but wrong.
TEST(MobMarkersTest, EveryAnimalIsInsideItsPaddock) {
  const core::Polygon paddock = square_paddock(1000.0, 2000.0, 200.0);

  const std::vector<core::Point2D> animals =
      scatter_within(paddock, 3, 200, core::AnimalKind::Sheep);

  ASSERT_EQ(animals.size(), 200U) << "every animal has to be placed, not most of them";
  for (const core::Point2D& animal : animals) {
    EXPECT_TRUE(paddock.contains(animal)) << "at " << animal.easting << ", " << animal.northing;
  }
}

// The same farm on the same day draws the same picture.
//
// Positions are illustrative, and an illustration that reshuffles itself every
// frame reads as movement - which is the one thing this must not suggest, since
// the model computes no movement at all.
TEST(MobMarkersTest, TheSameMobIsScatteredTheSameWayEveryTime) {
  const core::Polygon paddock = square_paddock(1000.0, 2000.0, 200.0);

  const std::vector<core::Point2D> first = scatter_within(paddock, 3, 50, core::AnimalKind::Sheep);
  const std::vector<core::Point2D> again = scatter_within(paddock, 3, 50, core::AnimalKind::Sheep);

  ASSERT_EQ(first.size(), again.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    EXPECT_DOUBLE_EQ(first[i].easting, again[i].easting) << "animal " << i;
    EXPECT_DOUBLE_EQ(first[i].northing, again[i].northing) << "animal " << i;
  }
}

// Two paddocks are not the same paddock, and two species in one paddock are not
// standing on each other.
TEST(MobMarkersTest, DifferentPaddocksAndSpeciesAreScatteredDifferently) {
  const core::Polygon paddock = square_paddock(1000.0, 2000.0, 200.0);

  const std::vector<core::Point2D> here = scatter_within(paddock, 3, 20, core::AnimalKind::Sheep);
  const std::vector<core::Point2D> elsewhere =
      scatter_within(paddock, 4, 20, core::AnimalKind::Sheep);
  const std::vector<core::Point2D> cattle =
      scatter_within(paddock, 3, 20, core::AnimalKind::Cattle);

  ASSERT_EQ(here.size(), 20U);
  EXPECT_NE(here.front().easting, elsewhere.front().easting);
  EXPECT_NE(here.front().easting, cattle.front().easting);
}

// One shape per animal reaches the geometry, not one per mob.
TEST(MobMarkersTest, AMobOfManyDrawsManyShapes) {
  const core::Polygon paddock = square_paddock(1000.0, 2000.0, 200.0);
  const std::vector<core::Polygon> paddocks{paddock};

  MobMarker marker;
  marker.at = paddock.centroid();
  marker.kind = core::AnimalKind::Sheep;
  marker.head = 40;
  marker.paddock = 0;
  marker.head_here = 40;

  vtkNew<vtkPolyData> shape;
  build_mob_markers({marker}, core::AnimalKind::Sheep, kSize, {}, paddocks, shape);

  EXPECT_EQ(shape->GetNumberOfPolys(), 40);
}

// And a mark with no boundary behind it still draws, because a farm without
// paddock polygons still has stock somewhere.
TEST(MobMarkersTest, WithoutABoundaryTheMobIsStillOneMark) {
  MobMarker marker;
  marker.at = {1000.0, 2000.0};
  marker.kind = core::AnimalKind::Sheep;
  marker.head = 40;
  marker.head_here = 40;
  marker.paddock = 7;  // Out of range of the empty list below.

  vtkNew<vtkPolyData> shape;
  build_mob_markers({marker}, core::AnimalKind::Sheep, kSize, {}, kNoPaddocks, shape);

  EXPECT_EQ(shape->GetNumberOfPolys(), 1);
}

}  // namespace paddock::viz
