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

#include <array>
#include <cmath>
#include <vector>
#include <vtkIdList.h>
#include <vtkVolume.h>

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
                          ColourScale(Ramp::PastureGreen, 0.0, 5000.0), "cover"),
               std::invalid_argument);
}

}  // namespace

// **The ground light does not move.** This is the opposite of what it used to
// assert, and deliberately so.
//
// The colour on the surface is a reading off a legend: 3500 kg DM/ha has to be
// the same pixel colour on every day of the run. Lighting the ground with the
// day's own sun broke that - the same paddock came out pale under cloud and
// was read as carrying less feed, when what had changed was the light. The
// weather moved to the sky; the ground gets a fixed hillshade.
TEST(TerrainSceneTest, TheGroundIsLitTheSameWayOnEveryDay) {
  TerrainScene scene;

  scene.light_the_ground();
  std::array<double, 3> first{};
  scene.sun()->GetPosition(first.data());
  const double first_intensity = scene.sun()->GetIntensity();

  // A different day, a different sky - and the same light on the grass.
  scene.show_sky(-43.64, 172, 14.0, 0.30, 12.0, 9.0, 1.0);
  scene.light_the_ground();
  std::array<double, 3> again{};
  scene.sun()->GetPosition(again.data());

  EXPECT_DOUBLE_EQ(first[0], again[0]);
  EXPECT_DOUBLE_EQ(first[1], again[1]);
  EXPECT_DOUBLE_EQ(first[2], again[2]);
  EXPECT_DOUBLE_EQ(first_intensity, scene.sun()->GetIntensity());
}

// The sun in the SKY does move, which is where the season became visible once
// it left the grass alone.
TEST(TerrainSceneTest, TheSunInTheSkyMovesWithTheDate) {
  TerrainScene scene;
  scene.show(raster(8, 6, 2500.0), sloping(8, 6), ColourScale(Ramp::PastureGreen, 0.0, 5000.0),
             "cover");

  scene.show_sky(-43.64, 172, 14.0, 0.75);  // Midwinter
  std::array<double, 6> midwinter{};
  scene.sun_disc()->GetBounds(midwinter.data());

  scene.show_sky(-43.64, 355, 14.0, 0.75);  // Midsummer
  std::array<double, 6> midsummer{};
  scene.sun_disc()->GetBounds(midsummer.data());

  // Higher in summer, which is the whole of what a season is.
  EXPECT_GT(midsummer[5], midwinter[5])
      << "midwinter top " << midwinter[5] << ", midsummer top " << midsummer[5];
}

// Cloud thickens as less of the sky's radiation reaches the ground.
//
// The field of density is fixed; what the day changes is how much of it is
// made visible. So the measurement to read back is the opacity the cloud is
// drawn at, not its shape - the shape must not change, or the picture would be
// showing a cloud field nobody recorded.
TEST(TerrainSceneTest, CloudThickensAsTheDayDulls) {
  TerrainScene scene;
  scene.show(raster(8, 6, 2500.0), sloping(8, 6), ColourScale(Ramp::PastureGreen, 0.0, 5000.0),
             "cover");

  scene.show_sky(-43.64, 355, 14.0, 0.75);
  const double clear = scene.cloud_opacity()->GetValue(1.0);

  scene.show_sky(-43.64, 355, 14.0, 0.25);
  const double dull = scene.cloud_opacity()->GetValue(1.0);

  EXPECT_GT(dull, clear) << "clear " << clear << ", overcast " << dull;
  EXPECT_GT(clear, 0.0) << "even a clear day has some cloud in it";
}

// **The cloud can be taken away.** A density field is translucent by
// construction, and anything translucent between the camera and the paddocks
// shifts a colour the reader is meant to match against the legend. Turning the
// weather off is what makes the map exact.
TEST(TerrainSceneTest, TheWeatherCanBeTakenOffTheMap) {
  TerrainScene scene;
  scene.show(raster(8, 6, 2500.0), sloping(8, 6), ColourScale(Ramp::PastureGreen, 0.0, 5000.0),
             "cover");
  scene.show_sky(-43.64, 355, 14.0, 0.25, 8.0, 9.0, 6.0);

  EXPECT_TRUE(scene.weather_shown());
  EXPECT_EQ(scene.cloud()->GetVisibility(), 1);

  scene.show_weather(false);
  EXPECT_FALSE(scene.weather_shown());
  EXPECT_EQ(scene.cloud()->GetVisibility(), 0);
  EXPECT_EQ(scene.rain()->GetVisibility(), 0);
  EXPECT_EQ(scene.wind()->GetVisibility(), 0);
}

// Rain is drawn when it rained and not when it did not.
TEST(TerrainSceneTest, RainIsDrawnOnlyOnDaysItRained) {
  TerrainScene scene;
  scene.show(raster(8, 6, 2500.0), sloping(8, 6), ColourScale(Ramp::PastureGreen, 0.0, 5000.0),
             "cover");

  scene.show_sky(-43.64, 355, 14.0, 0.5, 0.0);
  EXPECT_EQ(scene.rain()->GetVisibility(), 0);

  scene.show_sky(-43.64, 355, 14.0, 0.5, 12.0);
  EXPECT_EQ(scene.rain()->GetVisibility(), 1);
  const vtkIdType wet = scene.rain_lines()->GetNumberOfLines();

  scene.show_sky(-43.64, 355, 14.0, 0.5, 2.0);
  EXPECT_LT(scene.rain_lines()->GetNumberOfLines(), wet)
      << "a wetter day should draw more rain than a damp one";
}

// **The wind has strength and no direction, and the picture must not give it
// one.** The series carries a speed and no bearing. These strokes are all
// parallel and all level; if a future change makes one of them point
// somewhere, it is drawing data that does not exist.
TEST(TerrainSceneTest, TheWindIsDrawnWithoutADirection) {
  TerrainScene scene;
  scene.show(raster(8, 6, 2500.0), sloping(8, 6), ColourScale(Ramp::PastureGreen, 0.0, 5000.0),
             "cover");

  scene.show_sky(-43.64, 355, 14.0, 0.5, 0.0, 0.0);
  EXPECT_EQ(scene.wind()->GetVisibility(), 0) << "still air draws nothing";

  scene.show_sky(-43.64, 355, 14.0, 0.5, 0.0, 12.0);
  EXPECT_EQ(scene.wind()->GetVisibility(), 1);

  // **The gusts are spread around the compass, so the set as a whole points
  // nowhere.** This is the invariant that matters, and it is not "every stroke
  // is level" - that was the first thing asserted here and it was the wrong
  // thing, because a set of parallel level strokes still reads as flow along
  // one axis. What must never happen is that the picture agrees on a bearing
  // the data does not carry.
  //
  // Measured as the mean direction of the gusts: if they all ran one way its
  // length would be near one, and spread evenly it is near zero.
  vtkPolyData* marks = scene.wind_marks();
  ASSERT_NE(marks->GetPoints(), nullptr);
  ASSERT_GT(marks->GetNumberOfLines(), 1);

  double sum_east = 0.0;
  double sum_north = 0.0;
  int counted = 0;
  vtkNew<vtkIdList> ids;
  marks->GetLines()->InitTraversal();
  while (marks->GetLines()->GetNextCell(ids) != 0) {
    if (ids->GetNumberOfIds() < 2) {
      continue;
    }
    std::array<double, 3> from{};
    std::array<double, 3> to{};
    marks->GetPoint(ids->GetId(0), from.data());
    marks->GetPoint(ids->GetId(ids->GetNumberOfIds() - 1), to.data());
    const double east = to[0] - from[0];
    const double north = to[1] - from[1];
    const double length = std::hypot(east, north);
    if (length <= 0.0) {
      continue;
    }
    sum_east += east / length;
    sum_north += north / length;
    ++counted;
  }
  ASSERT_GT(counted, 1);

  const double resultant = std::hypot(sum_east, sum_north) / counted;
  EXPECT_LT(resultant, 0.5) << "the gusts agree on a bearing (resultant " << resultant
                            << "), which is the one thing the wind series does not carry";
}

}  // namespace paddock::viz
