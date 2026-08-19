// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <vtkActor.h>
#include <vtkLight.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkStructuredGrid.h>

#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/viz/ColourScale.hpp>
#include <paddock/viz/MobMarkers.hpp>

namespace paddock::viz {

/// The same farm, on the ground it actually sits on.
///
/// The flat map answers "how much is there and where"; this answers "and what
/// is the ground doing", which for a pastoral farm is not decoration: a north
/// face and a south face of one hill grow differently, and stock spend energy
/// walking a slope. Both are in the model. Until there was a view of the
/// terrain there was no way to look at either.
///
/// It draws the elevation the model ran on and never one of its own. A view
/// that generated its own surface would be a picture of a different farm from
/// the one the numbers came from.
class TerrainScene {
 public:
  TerrainScene();

  /// Drapes `field` over `elevation`. Both must be the same shape - they come
  /// from the same grid, and a mismatch means one of them is from another run.
  ///
  /// `title` is the legend's caption and carries the units, because a number on
  /// a map with no unit is a decoration.
  void show(const core::Raster<double>& field, const core::Raster<double>& elevation,
            const ColourScale& scale, const std::string& title);

  /// Fences, draped on the same surface. Same contract as MapScene: the
  /// boundaries are fixed for a run and the grazed set is not.
  void set_boundaries(const std::vector<core::Polygon>& boundaries);
  void show_grazed(const std::vector<std::size_t>& grazed);
  void clear_boundaries();

  /// Puts the stock on the ground, lifted onto the surface as the fences are.
  void show_mobs(const std::vector<MobMarker>& markers);

  /// How much the heights are stretched, and **it is a lie in a known
  /// direction**.
  ///
  /// A New Zealand paddock block is a kilometre across and tens of metres of
  /// relief, so drawn true to scale the ground looks flat and the thing the
  /// view exists to show is invisible. Exaggeration is the usual answer and it
  /// makes every slope look steeper than it is, so the factor is reported
  /// rather than quietly applied, and one is the default.
  /// Light the ground with the sun of a particular day, and colour the sky by
  /// how much of that sun reached the ground.
  ///
  /// **Everything here is a number the run was driven by.** The sun's position
  /// is FAO-56 geometry from the date and the latitude - the same declination
  /// the growth model integrates radiation over - so a face drawn in sunlight
  /// is the face the model grew more grass on. The sky comes from the
  /// clearness index Rs/Ra of the day's own measured radiation. Nothing is
  /// drawn from nothing: there are no cloud shapes, because the series carries
  /// no cloud, and no wind in the picture, because the series carries a speed
  /// and no direction.
  ///
  /// The hour is the caller's, and it is solar time. Passing a wall clock hour
  /// would put the sun in the wrong part of the sky by half an hour of
  /// rotation at this longitude.
  void light_for(double latitude_degrees, int day_of_year, double solar_hour,
                 double clearness_index);

  void set_vertical_exaggeration(double factor);

  [[nodiscard]] double vertical_exaggeration() const noexcept { return exaggeration_; }

  /// The scene's sun. Exposed so that where it is and how bright it is can be
  /// checked without a render window: a light that never moved would look
  /// entirely plausible on ground that falls six metres in a kilometre.
  [[nodiscard]] vtkLight* sun() const noexcept { return sun_; }

  /// Looks at the farm from the north-west and above, the angle a farm is
  /// usually photographed from.
  void reset_camera();

  [[nodiscard]] vtkRenderer* renderer() const noexcept { return renderer_; }

  [[nodiscard]] bool has_field() const noexcept { return has_field_; }

  /// The true elevation range of what is being drawn, in metres, before
  /// exaggeration. Empty scene gives {0, 0}.
  [[nodiscard]] std::pair<double, double> elevation_range() const noexcept {
    return {lowest_m_, highest_m_};
  }

  [[nodiscard]] std::size_t fence_ring_count() const;
  [[nodiscard]] std::size_t grazed_ring_count() const;

 private:
  /// Height at a world point, interpolated the way VTK shades between the same
  /// grid's points, so a fence sits on the surface instead of cutting through
  /// it and floating over it in turn.
  [[nodiscard]] double height_at(core::Point2D point) const;

  void rebuild_surface();
  void rebuild_fences();
  void rebuild_mobs();

  core::Raster<double> elevation_;
  core::Raster<double> field_;
  std::vector<core::Polygon> boundaries_;
  std::vector<std::size_t> grazed_;

  vtkNew<vtkStructuredGrid> surface_;

  /// The sun. One directional light standing in for a body 150 million km
  /// away, which is what a directional light is for.
  vtkNew<vtkLight> sun_;

  /// The sky. A second light from overhead standing in for the light that
  /// arrives from every direction rather than from the sun.
  vtkNew<vtkLight> sky_;
  vtkNew<vtkLookupTable> lookup_;
  vtkNew<vtkPolyDataMapper> surface_mapper_;
  vtkNew<vtkActor> surface_actor_;
  vtkNew<vtkScalarBarActor> legend_;

  vtkNew<vtkPolyData> fences_;
  vtkNew<vtkPolyDataMapper> fence_mapper_;
  vtkNew<vtkActor> fence_actor_;
  vtkNew<vtkPolyData> grazed_fences_;
  vtkNew<vtkPolyDataMapper> grazed_mapper_;
  vtkNew<vtkActor> grazed_actor_;

  std::vector<MobMarker> mobs_;
  std::vector<vtkNew<vtkPolyData>> mob_shapes_;
  std::vector<vtkNew<vtkPolyDataMapper>> mob_mappers_;
  std::vector<vtkNew<vtkActor>> mob_actors_;

  vtkNew<vtkRenderer> renderer_;

  double exaggeration_ = 1.0;
  double lowest_m_ = 0.0;
  double highest_m_ = 0.0;
  bool has_field_ = false;
};

}  // namespace paddock::viz
