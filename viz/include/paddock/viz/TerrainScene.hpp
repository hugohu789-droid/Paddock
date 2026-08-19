// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <vtkActor.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkColorTransferFunction.h>
#include <vtkImageData.h>
#include <vtkLight.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkStructuredGrid.h>
#include <vtkVolume.h>

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
  /// Light the ground, the same way on every day of the run.
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
  void light_the_ground();

  /// Draw the day's weather in the sky over the farm.
  ///
  /// **Everything here is a glyph for a number the run was driven by, and
  /// nothing here touches the grass.** The colour on the ground is a reading
  /// off a legend and has to mean the same thing on every day of the run, so
  /// the weather is drawn beside it rather than over it.
  ///
  /// What each thing stands for:
  ///   * the sun sits where FAO-56 geometry puts it for the date, the latitude
  ///     and the hour, and its brightness follows the day's ultraviolet index;
  ///   * the cloud's opacity follows the clearness index Rs/Ra, so a dull day
  ///     has a thicker sheet - the shape is a drawing and the thickness is a
  ///     measurement;
  ///   * the rain's density follows the day's total in millimetres, and it
  ///     falls over the whole farm because the series has one value for the
  ///     whole farm;
  ///   * the wind is drawn as strength alone. **There is no direction in the
  ///     data**, so nothing here points anywhere: an arrow would be inventing
  ///     the one thing the series does not carry.
  ///
  /// `uv_index` is dimensionless and zero when the series carries none, in
  /// which case the sun is drawn at a fixed brightness rather than a made-up
  /// one.
  void show_sky(double latitude_degrees, int day_of_year, double solar_hour, double clearness_index,
                double rainfall_mm = 0.0, double wind_speed_m_per_s = 0.0, double uv_index = 0.0);

  /// Show or hide the weather drawn over the farm.
  ///
  /// **The reason this exists is the colour on the ground.** A cloud rendered
  /// as a density field is translucent by construction - that is what makes it
  /// look like cloud rather than like a grey solid - and anything translucent
  /// between the camera and the paddocks shifts a colour the reader is meant
  /// to match against the legend. Turning the sky off makes the map exact
  /// again. Neither answer serves both jobs, so both are offered.
  void show_weather(bool visible);

  /// Draws the irrigation that was put on today, one depth per cell in the
  /// same order as the field.
  ///
  /// **The picture follows the water; it never decides it.** What is drawn is
  /// read from the depths handed in - where they are non-zero, and how deep -
  /// so a paddock that got nothing shows nothing. Nothing here feeds back into
  /// the model.
  ///
  /// **There is no rotating arm, and its absence is the honest choice.** A real
  /// centre pivot waters the sector it is passing over; this model waters
  /// whichever ground the rule found dry, all of it on the same day. An arm
  /// sweeping round would be an animation with no state behind it, saying
  /// something about how the water arrived that the model does not know. What
  /// is drawn instead is the equipment that does not move - the hub, and the
  /// circle it reaches - with spray standing over the ground that actually got
  /// water.
  void show_irrigation(const core::Raster<double>& applied_mm);

  /// The two layers the scene owns itself. Everything else in the stack is
  /// handed to it by show_stack.
  enum class Layer : std::uint8_t {
    Weather,  ///< Sun, cloud, rain and wind, above everything.
    Pasture,  ///< The ground surface, its fences and its stock: the map itself.
  };

  void show_layer(Layer layer, bool visible);
  [[nodiscard]] bool layer_shown(Layer layer) const;

  /// One sheet of the stack: what to draw, how to colour it, and what to call
  /// it.
  struct StackEntry {
    core::Raster<double> values;
    ColourScale colours;
    std::string name;
  };

  /// Draws a stack of fields under the pasture, one sheet each, in the order
  /// given.
  ///
  /// **Held apart rather than pressed into a section.** The separation is what
  /// the stack is for: seven fields for the same day, all readable at once, so
  /// that a dry patch in the soil moisture sheet can be seen against the growth
  /// sheet directly below the ground it explains. Pushed together they would
  /// hide each other and only the top one would be legible. It is a drawing
  /// choice and stands in for no depth - these are not soil horizons, they are
  /// seven ways of looking at the same piece of country.
  ///
  /// Every sheet carries the measured ground, so a rise in the country rises
  /// through the whole stack, and every sheet carries the paddock fences -
  /// dimmer than the top one, because down there they are for locating a cell
  /// rather than for reading.
  ///
  /// Each is coloured by its own scale. They are different quantities in
  /// different units and a shared ramp would invite a comparison that means
  /// nothing.
  void show_stack(const std::vector<StackEntry>& entries);

  /// What to call the top sheet.
  ///
  /// Separate from the title given to show(), which is the legend's - two lines
  /// naming a unit and a scale. Beside the sheet that is a paragraph; what is
  /// wanted there is the short name the map mode goes by, the same kind of name
  /// the sheets below carry.
  void name_top_layer(const std::string& name);

  /// Shows or hides one sheet of the stack, by its position in it.
  void show_stack_layer(std::size_t index, bool visible);

  [[nodiscard]] std::size_t stack_size() const noexcept { return stack_.size(); }

  [[nodiscard]] bool stack_layer_shown(std::size_t index) const;

  /// How many spray uprights and pieces of equipment were drawn. Here so the
  /// picture can be checked without rendering it: these are the two things
  /// show_irrigation decides, and both are decidable from the geometry alone.
  [[nodiscard]] std::size_t spray_line_count() const;
  [[nodiscard]] std::size_t pivot_line_count() const;

  /// Slides the view up or down the screen without turning it.
  ///
  /// `fraction` is a share of the visible height: 1.0 moves a whole screenful,
  /// positive towards the sky and negative towards the ground. The camera and
  /// its focal point move together, so nothing rotates and the bearing the
  /// mouse left the scene on is kept.
  ///
  /// **This exists because the sky is part of what the camera frames.** The sun
  /// and the cloud sit well above the farm and the view is reset around all of
  /// it, so zooming in on a farm that sits at the bottom of that volume walks
  /// the pasture off the bottom of the window. Turning it back into view by
  /// dragging with the mouse also turns the scene, which loses the bearing;
  /// this changes what is on screen and nothing else.
  void pan_vertically(double fraction);

  /// How far the view has been slid from where reset_camera put it, as a share
  /// of the visible height. Zero after a reset.
  [[nodiscard]] double vertical_pan() const noexcept { return panned_; }

  /// The ground under a point on the screen, in the scene's own coordinates,
  /// or nothing when the point missed the surface. See MapScene::ground_at.
  [[nodiscard]] std::optional<core::Point2D> ground_at(int x, int y) const;

  [[nodiscard]] bool weather_shown() const noexcept { return weather_shown_; }

  void set_vertical_exaggeration(double factor);

  [[nodiscard]] double vertical_exaggeration() const noexcept { return exaggeration_; }

  /// The scene's sun. Exposed so that where it is and how bright it is can be
  /// checked without a render window: a light that never moved would look
  /// entirely plausible on ground that falls six metres in a kilometre.
  [[nodiscard]] vtkLight* sun() const noexcept { return sun_; }

  /// The sky's glyphs, exposed so what they say can be checked without a
  /// render window. A screenshot cannot tell a sun that moves with the season
  /// from one that is painted in place.
  [[nodiscard]] vtkPolyData* sun_disc() const noexcept { return sun_disc_; }

  [[nodiscard]] vtkVolume* cloud() const noexcept { return cloud_volume_; }

  /// The opacity the cloud is drawn at, which is where the day's cloud cover
  /// ends up. Exposed so a test can read the day off the picture.
  [[nodiscard]] vtkPiecewiseFunction* cloud_opacity() const noexcept { return cloud_opacity_; }

  [[nodiscard]] vtkActor* rain() const noexcept { return rain_actor_; }

  [[nodiscard]] vtkPolyData* rain_lines() const noexcept { return rain_lines_; }

  [[nodiscard]] vtkActor* wind() const noexcept { return wind_actor_; }

  [[nodiscard]] vtkPolyData* wind_marks() const noexcept { return wind_marks_; }

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

  /// N, S, E and W, standing at the edges of the farm.
  ///
  /// The scene can be spun freely and a rectangle of paddocks looks much the
  /// same from any side, so after a drag or two there is nothing in the
  /// picture that says which way is north - and north is not decoration here:
  /// it is why one face of a slope grows more grass than the other.
  std::vector<vtkNew<vtkBillboardTextActor3D>> compass_;

  /// The name beside the top sheet. The rest of the stack carries its own, one
  /// per sheet. A billboard for the same reason the compass points are: a flat
  /// label turns edge-on and vanishes when the scene is spun, which is exactly
  /// when somebody needs to know which sheet is which.
  vtkNew<vtkBillboardTextActor3D> pasture_label_;

  void place_compass();

  /// Builds the fence rings for one sheet of the stack, at its own height.
  void build_stack_fences(vtkPolyData* into, double lift) const;

  /// The weather drawn over the farm: the sun, a sheet of cloud, falling rain,
  /// and a mark for the wind's strength. All of them are glyphs, all of them
  /// are driven by the day's own numbers, and none of them colours the ground.
  vtkNew<vtkPolyData> sun_disc_;
  vtkNew<vtkActor> sun_actor_;
  /// The cloud, as a density field rendered by ray casting rather than as a
  /// surface.
  ///
  /// **Built once and never rebuilt.** The noise field is fixed; what changes
  /// from day to day is how much of it is made visible, which is the opacity
  /// transfer function. That is both the fast way and the honest one - a
  /// million voxels is not something to recompute every frame, and the series
  /// carries no cloud field, so cloud that rearranged itself overnight would
  /// be showing weather nobody recorded.
  vtkNew<vtkImageData> cloud_density_;
  vtkNew<vtkVolume> cloud_volume_;
  vtkNew<vtkPiecewiseFunction> cloud_opacity_;
  vtkNew<vtkColorTransferFunction> cloud_colour_;
  /// The farm the density field was built over.
  ///
  /// **"Built once" means once per farm, not once per session.** Not rebuilding
  /// it between days is the point - the series carries no cloud field, so cloud
  /// that rearranged itself overnight would be showing weather nobody recorded.
  /// Not rebuilding it between FARMS was a bug: the sun, the rain and the wind
  /// are placed every frame and moved to the new farm, while the cloud stayed
  /// over the old one, which for the two shipped farms is thirteen kilometres
  /// away.
  double cloud_built_east_ = 0.0;
  double cloud_built_north_ = 0.0;
  double cloud_built_span_ = 0.0;
  double cloud_built_top_ = 0.0;
  bool cloud_built_ = false;

  /// Whether the density field was built over the farm now being drawn.
  [[nodiscard]] bool cloud_matches_farm() const noexcept;

  /// Builds the density field. Called once, on the first day drawn.
  void build_cloud_density();
  vtkNew<vtkPolyData> rain_lines_;
  vtkNew<vtkActor> rain_actor_;
  vtkNew<vtkPolyData> wind_marks_;
  vtkNew<vtkActor> wind_actor_;

  /// Spray standing over watered ground, and the pivot it comes from. Two
  /// actors because they are two things: the spray moves with the water and
  /// the equipment does not.
  vtkNew<vtkPolyData> spray_lines_;
  vtkNew<vtkActor> spray_actor_;

  /// One sheet of the stack. Held by pointer because vtkNew cannot be copied
  /// or moved, and the stack is built from whatever it is handed.
  struct StackedSheet {
    vtkNew<vtkStructuredGrid> grid;
    vtkNew<vtkLookupTable> colours;
    vtkNew<vtkActor> actor;
    vtkNew<vtkPolyData> fences;
    vtkNew<vtkActor> fence_actor;
    vtkNew<vtkBillboardTextActor3D> label;
    bool shown = false;
  };

  std::vector<std::unique_ptr<StackedSheet>> stack_;

  vtkNew<vtkPolyData> pivot_lines_;
  vtkNew<vtkActor> pivot_actor_;

  /// The farm's extent, kept so the sky can be hung over it.
  double sky_east_ = 0.0;
  double sky_north_ = 0.0;
  double sky_width_ = 0.0;
  double sky_height_ = 0.0;
  double sky_top_ = 0.0;

  bool weather_shown_ = true;
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
  /// How far the view has been slid, so the control can be absolute: a slider
  /// says where it is, not how far to go, and applying its value as a step
  /// every time it moved would run the view off the screen.
  double panned_ = 0.0;

  bool has_field_ = false;
};

}  // namespace paddock::viz
