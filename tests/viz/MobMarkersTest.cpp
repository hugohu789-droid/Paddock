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
  build_mob_markers(markers, kind, kSize, {}, shape);
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
  build_mob_markers(mixed, core::AnimalKind::Sheep, kSize, {}, sheep);
  build_mob_markers(mixed, core::AnimalKind::Cattle, kSize, {}, cattle);
  build_mob_markers(mixed, core::AnimalKind::Deer, kSize, {}, deer);

  EXPECT_EQ(sheep->GetNumberOfPolys(), 2);
  EXPECT_EQ(cattle->GetNumberOfPolys(), 1);
  EXPECT_EQ(deer->GetNumberOfPolys(), 0) << "a kind with no stock in it should draw nothing";
}

// A mark is a farm-sized object, so it is built in metres on the ground rather
// than in pixels: zooming in makes a mob larger, as it makes a paddock larger.
TEST(MobMarkersTest, AMarkIsTheSizeItWasAskedForOnTheGround) {
  const std::vector<MobMarker> markers{at(1000.0, 2000.0, core::AnimalKind::Cattle)};
  vtkNew<vtkPolyData> shape;
  build_mob_markers(markers, core::AnimalKind::Cattle, kSize, {}, shape);

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
  build_mob_markers(markers, core::AnimalKind::Sheep, kSize, {}, flat);
  std::array<double, 6> flat_bounds{};
  flat->GetBounds(flat_bounds.data());
  EXPECT_DOUBLE_EQ(flat_bounds[4], 0.0);
  EXPECT_DOUBLE_EQ(flat_bounds[5], 0.0);

  vtkNew<vtkPolyData> raised;
  build_mob_markers(
      markers, core::AnimalKind::Sheep, kSize, [](core::Point2D) { return 37.5; }, raised);
  std::array<double, 6> raised_bounds{};
  raised->GetBounds(raised_bounds.data());
  EXPECT_DOUBLE_EQ(raised_bounds[4], 37.5);
  EXPECT_DOUBLE_EQ(raised_bounds[5], 37.5);
}

}  // namespace
}  // namespace paddock::viz
