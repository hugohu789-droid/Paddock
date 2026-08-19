// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <vtkActor.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkLegendScaleActor.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>

#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/viz/ColourScale.hpp>
#include <paddock/viz/MobMarkers.hpp>

namespace paddock::viz {

/// The VTK side of the 2D map: a georeferenced raster, a colour scale, and the
/// legend that says what the colours mean.
///
/// Only this file and its translation unit know about VTK; the mapping from
/// value to colour lives in ColourScale, without VTK, so it can be tested
/// headlessly (ADR 0010).
class MapScene {
 public:
  MapScene();

  /// Replaces what the scene shows.
  ///
  /// `title` is the legend's caption and should carry the units, because a
  /// number on a map with no unit is a decoration.
  void show(const core::Raster<double>& raster, const ColourScale& scale, const std::string& title);

  /// Draws the fences over the field.
  ///
  /// Without them the map shows cells, and a rotation reads as a patch of
  /// shorter grass that moves for no visible reason. With them the same run
  /// reads as what it is: a mob in a paddock, and the ground it has already
  /// been over recovering behind it.
  ///
  /// Coordinates are NZTM2000 metres, the same as the raster, which is why
  /// `show` anchors the image at its south-west corner.
  ///
  /// Separate from show_grazed because the fences do not move and the mob
  /// does. Called once a run, against sixty-odd times a second while the
  /// timeline is playing.
  void set_boundaries(const std::vector<core::Polygon>& boundaries);

  /// Picks out the paddocks being grazed today.
  ///
  /// A list of indices rather than one index, because set stocking gives a mob
  /// the run of the whole farm, and a picture that showed only the first
  /// paddock would draw that as a rotation with a very lazy farmer.
  ///
  /// Indices are into the boundaries last passed to set_boundaries; any that
  /// are not are ignored, so a stale day cannot crash the view.
  void show_grazed(const std::vector<std::size_t>& grazed);

  /// Removes the fences, for a scene with no farm behind it.
  void clear_boundaries();

  /// Puts the stock on the map.
  ///
  /// The fences say where the paddocks are and the amber says which is being
  /// grazed; neither says whether those are sheep or cattle, which is the one
  /// thing a person at the gate could tell you without being told.
  void show_mobs(const std::vector<MobMarker>& markers);

  /// Fits the camera to the current raster. Called after the first `show` and
  /// whenever the widget is resized.
  void reset_camera();

  [[nodiscard]] vtkRenderer* renderer() const noexcept { return renderer_; }

  /// Where the coloured field actually sits, in NZTM2000 metres.
  ///
  /// Exposed to be asserted on. The field and the fences are drawn by different
  /// machinery in the same coordinates, and when they disagreed the map looked
  /// entirely reasonable - the error only became visible once there were fences
  /// to compare against. This is how that stays fixed without a person looking
  /// at a picture.
  [[nodiscard]] core::BoundingBox field_bounds() const;

  /// The ground under a point on the screen, in the map's own coordinates,
  /// or nothing when the point missed the farm.
  ///
  /// `x` and `y` are VTK display pixels, measured from the bottom left - Qt
  /// measures from the top, and the caller flips. Returning the coordinate
  /// rather than a paddock index is deliberate: the scene draws ground and
  /// knows nothing about who owns it, and a renderer that started resolving
  /// ownership would be a second answer to a question the model already
  /// answers.
  [[nodiscard]] std::optional<core::Point2D> ground_at(int x, int y) const;

  /// How many closed rings each fence layer holds. For tests, and for telling
  /// an empty layer from one that failed to build.
  [[nodiscard]] std::size_t fence_ring_count() const;
  [[nodiscard]] std::size_t grazed_ring_count() const;

  /// True once a raster has been shown, so a caller can tell an empty scene
  /// from one that failed to build.
  [[nodiscard]] bool has_field() const noexcept { return has_field_; }

 private:
  vtkNew<vtkImageData> image_;
  vtkNew<vtkLookupTable> lookup_;
  vtkNew<vtkImageMapToColors> colours_;
  vtkNew<vtkImageActor> actor_;
  vtkNew<vtkScalarBarActor> legend_;
  /// Coordinate labels around the edges and a distance scale. Without them the
  /// map is a coloured rectangle: a reader cannot tell where it is, how big it
  /// is, or which way is north.
  vtkNew<vtkLegendScaleActor> axes_;
  /// Two actors rather than one with per-cell colour: the grazed paddocks are
  /// drawn over the rest, so a fence shared by a grazed and a rested paddock
  /// shows as grazed instead of depending on which was built last.
  /// Kept so show_grazed can build rings without the caller handing the
  /// polygons back every frame.
  std::vector<core::Polygon> boundaries_;
  vtkNew<vtkPolyData> fences_;
  vtkNew<vtkPolyDataMapper> fence_mapper_;
  vtkNew<vtkActor> fence_actor_;
  vtkNew<vtkPolyData> grazed_fences_;
  vtkNew<vtkPolyDataMapper> grazed_mapper_;
  vtkNew<vtkActor> grazed_actor_;

  /// One layer per kind of animal, so each keeps its own colour without
  /// per-cell colouring, and a kind with no stock in it draws nothing.
  std::vector<vtkNew<vtkPolyData>> mob_shapes_;
  std::vector<vtkNew<vtkPolyDataMapper>> mob_mappers_;
  std::vector<vtkNew<vtkActor>> mob_actors_;

  vtkNew<vtkRenderer> renderer_;
  bool has_field_ = false;
};

}  // namespace paddock::viz
