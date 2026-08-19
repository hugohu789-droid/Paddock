// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkStructuredGridGeometryFilter.h>
#include <vtkTextProperty.h>

#include <paddock/core/Solar.hpp>
#include <paddock/viz/ColourTable.hpp>
#include <paddock/viz/MobMarkers.hpp>
#include <paddock/viz/TerrainScene.hpp>

namespace paddock::viz {

namespace {

constexpr int kLegendLabels = 5;

/// How far above the ground a fence is drawn, in metres before exaggeration.
///
/// Enough that the line is not fighting the surface for the same depth, small
/// enough that it does not read as a floating wire. A quarter of a metre is
/// about a fence post's worth.
constexpr double kFenceLift = 0.25;

constexpr float kFenceWidth = 1.5F;
constexpr float kGrazedFenceWidth = 4.0F;
constexpr double kFenceOpacity = 0.5;

/// The same marker size the flat map uses, so a mob is the same size in both.
// One animal, not one mob. At 8 m across a sheep is far larger than life -
// a real one is about a metre - but a marker has to be findable on a farm
// drawn a kilometre wide, and a flock of them reads as a flock.
constexpr double kMarkerSizeM = 8.0;

constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

}  // namespace

TerrainScene::TerrainScene() {
  renderer_->SetBackground(0.10, 0.11, 0.13);

  // The scene's own light, replacing the headlight VTK would otherwise supply.
  // A headlight sits at the camera and lights whatever you look at from where
  // you look at it, which is exactly the wrong thing here: it hides the shape
  // of the ground, and it cannot say anything about the sun.
  sun_->SetLightTypeToSceneLight();
  sun_->SetPositional(0);
  renderer_->AddLight(sun_);

  // The sky, from overhead. Not an ambient term - that was tried and it was
  // wrong twice over. VTK's ambient carries its own colour, white by default,
  // and scalar colouring replaces only the diffuse one, so an ambient share
  // mixes white into the grass: a paddock at 3500 kg DM/ha came out grey-green
  // instead of green, and looked like it had less feed on it rather than more.
  // A second light goes through the diffuse term like the sun does, so it
  // brightens the ground in the ground's own colour.
  sky_->SetLightTypeToSceneLight();
  sky_->SetPositional(0);
  sky_->SetFocalPoint(0.0, 0.0, 0.0);
  sky_->SetPosition(0.0, 0.0, 100000.0);
  renderer_->AddLight(sky_);

  renderer_->SetAutomaticLightCreation(0);

  surface_mapper_->SetLookupTable(lookup_);
  surface_mapper_->SetScalarModeToUsePointData();
  surface_mapper_->SetColorModeToMapScalars();
  surface_actor_->SetMapper(surface_mapper_);
  renderer_->AddActor(surface_actor_);

  legend_->SetLookupTable(lookup_);
  legend_->SetNumberOfLabels(kLegendLabels);
  legend_->SetWidth(0.09);
  legend_->SetHeight(0.55);
  legend_->SetPosition(0.88, 0.2);
  legend_->UnconstrainedFontSizeOn();
  for (vtkTextProperty* text : {legend_->GetLabelTextProperty(), legend_->GetTitleTextProperty()}) {
    text->SetColor(1.0, 1.0, 1.0);
    text->SetShadow(0);
    text->SetItalic(0);
    text->SetFontSize(14);
  }
  renderer_->AddViewProp(legend_);

  fence_mapper_->SetInputData(fences_);
  fence_actor_->SetMapper(fence_mapper_);
  fence_actor_->GetProperty()->SetColor(1.0, 1.0, 1.0);
  fence_actor_->GetProperty()->SetLineWidth(kFenceWidth);
  fence_actor_->GetProperty()->SetOpacity(kFenceOpacity);
  fence_actor_->GetProperty()->LightingOff();
  renderer_->AddActor(fence_actor_);

  grazed_mapper_->SetInputData(grazed_fences_);
  grazed_actor_->SetMapper(grazed_mapper_);
  // The same amber the flat map uses, so "being grazed today" means one thing
  // across both views.
  grazed_actor_->GetProperty()->SetColor(1.0, 0.72, 0.2);
  grazed_actor_->GetProperty()->SetLineWidth(kGrazedFenceWidth);
  grazed_actor_->GetProperty()->LightingOff();
  renderer_->AddActor(grazed_actor_);

  const std::size_t kinds = marker_kinds().size();
  mob_shapes_.resize(kinds);
  mob_mappers_.resize(kinds);
  mob_actors_.resize(kinds);
  for (std::size_t i = 0; i < kinds; ++i) {
    const std::array<double, 3> colour = colour_of(marker_kinds()[i]);
    mob_mappers_[i]->SetInputData(mob_shapes_[i]);
    mob_actors_[i]->SetMapper(mob_mappers_[i]);
    mob_actors_[i]->GetProperty()->SetColor(colour[0], colour[1], colour[2]);
    mob_actors_[i]->GetProperty()->LightingOff();
    renderer_->AddActor(mob_actors_[i]);
  }
}

void TerrainScene::show_mobs(const std::vector<MobMarker>& markers) {
  mobs_ = markers;
  rebuild_mobs();
}

void TerrainScene::rebuild_mobs() {
  // Lifted onto the surface the same way a fence is, and by the same amount, so
  // a mob stands on the ground rather than in it.
  const auto height = [this](core::Point2D point) {
    return (height_at(point) + kFenceLift) * exaggeration_;
  };
  for (std::size_t i = 0; i < marker_kinds().size() && i < mob_shapes_.size(); ++i) {
    build_mob_markers(mobs_, marker_kinds()[i], kMarkerSizeM, height, boundaries_, mob_shapes_[i]);
  }
}

double TerrainScene::height_at(core::Point2D point) const {
  if (elevation_.empty()) {
    return 0.0;
  }
  const core::GeoTransform& transform = elevation_.transform();
  const double cell = transform.cell_size;

  // Position in units of cells, measured from the centre of cell (0, 0) - the
  // same place the surface's points sit, so this interpolates between exactly
  // the points VTK shades between.
  const double x = ((point.easting - transform.origin_easting) / cell) - 0.5;
  const double y = ((transform.origin_northing - point.northing) / cell) - 0.5;

  const auto last_col = static_cast<double>(elevation_.cols() - 1);
  const auto last_row = static_cast<double>(elevation_.rows() - 1);
  const double clamped_x = std::clamp(x, 0.0, last_col);
  const double clamped_y = std::clamp(y, 0.0, last_row);

  const auto col = static_cast<std::size_t>(std::floor(clamped_x));
  const auto row = static_cast<std::size_t>(std::floor(clamped_y));
  const std::size_t next_col = std::min(col + 1, elevation_.cols() - 1);
  const std::size_t next_row = std::min(row + 1, elevation_.rows() - 1);
  const double fx = clamped_x - static_cast<double>(col);
  const double fy = clamped_y - static_cast<double>(row);

  const double top = (elevation_(col, row) * (1.0 - fx)) + (elevation_(next_col, row) * fx);
  const double bottom =
      (elevation_(col, next_row) * (1.0 - fx)) + (elevation_(next_col, next_row) * fx);
  return (top * (1.0 - fy)) + (bottom * fy);
}

void TerrainScene::show(const core::Raster<double>& field, const core::Raster<double>& elevation,
                        const ColourScale& scale, const std::string& title) {
  if (field.empty() || elevation.empty()) {
    has_field_ = false;
    return;
  }
  if (field.cols() != elevation.cols() || field.rows() != elevation.rows()) {
    throw std::invalid_argument(
        "TerrainScene::show: the field and the elevation are different shapes, so they are not "
        "the same farm");
  }

  field_ = field;
  elevation_ = elevation;

  lowest_m_ = std::numeric_limits<double>::max();
  highest_m_ = std::numeric_limits<double>::lowest();
  for (const double height : elevation_.values()) {
    lowest_m_ = std::min(lowest_m_, height);
    highest_m_ = std::max(highest_m_, height);
  }

  fill_lookup_table(lookup_, scale);
  surface_mapper_->SetScalarRange(scale.minimum(), scale.maximum());

  legend_->SetTitle(title.c_str());
  legend_->SetLookupTable(lookup_);
  const std::string format = tick_label_format(scale.minimum(), scale.maximum(), kLegendLabels);
  legend_->SetLabelFormat(format.c_str());

  has_field_ = true;
  rebuild_surface();
  rebuild_fences();
  rebuild_mobs();
}

void TerrainScene::rebuild_surface() {
  if (!has_field_) {
    return;
  }
  const auto cols = static_cast<int>(field_.cols());
  const auto rows = static_cast<int>(field_.rows());

  vtkNew<vtkPoints> points;
  points->SetNumberOfPoints(static_cast<vtkIdType>(cols) * rows);
  vtkNew<vtkDoubleArray> scalars;
  scalars->SetNumberOfComponents(1);
  scalars->SetNumberOfTuples(static_cast<vtkIdType>(cols) * rows);

  // Points sit at cell CENTRES, as the flat map's samples do, so the two views
  // put a value in the same place. The surface therefore stops half a cell
  // inside the farm boundary on every side, which is as far as point samples
  // reach.
  //
  // Row 0 of a Paddock raster is the northernmost and VTK's y increases north,
  // so the rows are walked from the south edge up. Getting this backwards
  // produces a farm that is upside down and perfectly plausible.
  for (int row = 0; row < rows; ++row) {
    const int source_row = rows - 1 - row;
    for (int col = 0; col < cols; ++col) {
      const auto c = static_cast<std::size_t>(col);
      const auto r = static_cast<std::size_t>(source_row);
      const core::Point2D centre = field_.cell_centre(c, r);
      const vtkIdType index = (static_cast<vtkIdType>(row) * cols) + col;
      points->SetPoint(index, centre.easting, centre.northing, elevation_(c, r) * exaggeration_);
      scalars->SetTuple1(index, field_(c, r));
    }
  }

  surface_->SetDimensions(cols, rows, 1);
  surface_->SetPoints(points);
  surface_->GetPointData()->SetScalars(scalars);
  surface_->Modified();

  // Normals averaged at the shared points, so the ground is lit as one surface
  // rather than as a field of facets.
  //
  // Without them every quad is lit by its own flat normal, and adjacent cells
  // that differ by a few centimetres take visibly different shades. On a farm
  // whose whole relief is six metres in a kilometre, and drawn at five times
  // vertical exaggeration, that reads as a rough, crumpled paddock. The ground
  // is not being changed here - the points are exactly where they were - only
  // how the light is worked out across them.
  //
  // Splitting is off: a crease sharp enough to split would be an artefact of
  // sampling at this resolution rather than a real edge in the ground.
  vtkNew<vtkStructuredGridGeometryFilter> geometry;
  geometry->SetInputData(surface_);

  vtkNew<vtkPolyDataNormals> normals;
  normals->SetInputConnection(geometry->GetOutputPort());
  normals->ComputePointNormalsOn();
  normals->ComputeCellNormalsOff();
  normals->SplittingOff();
  normals->ConsistencyOn();
  normals->AutoOrientNormalsOn();

  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputConnection(normals->GetOutputPort());
  mapper->SetLookupTable(lookup_);
  mapper->SetScalarRange(lookup_->GetTableRange());
  mapper->SetScalarModeToUsePointData();
  mapper->SetColorModeToMapScalars();
  surface_actor_->SetMapper(mapper);
  surface_actor_->GetProperty()->SetInterpolationToGouraud();
}

void TerrainScene::rebuild_fences() {
  const auto build = [this](const std::vector<std::size_t>& indices, vtkPolyData* into) {
    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> lines;
    for (const std::size_t index : indices) {
      if (index >= boundaries_.size()) {
        continue;
      }
      const std::vector<core::Point2D>& vertices = boundaries_[index].vertices();
      if (vertices.size() < 3) {
        continue;
      }
      // Each side is walked in steps rather than drawn corner to corner.
      //
      // A paddock side is a hundred metres or more, and between its corners the
      // ground rises and falls. A straight line between two draped corners cuts
      // through every rise in between, so the fence came out dashed - visible
      // in the hollows, buried over the humps, and looking like a rendering
      // fault rather than a fence. Stepping at the grid's own resolution puts a
      // point wherever the surface has one, which is as closely as this can
      // follow ground it only knows at that resolution.
      const double step = elevation_.empty() ? 0.0 : elevation_.transform().cell_size;

      const vtkIdType first = points->GetNumberOfPoints();
      vtkIdType placed = 0;
      for (std::size_t i = 0; i < vertices.size(); ++i) {
        const core::Point2D& from = vertices[i];
        const core::Point2D& to = vertices[(i + 1) % vertices.size()];
        const double length = std::hypot(to.easting - from.easting, to.northing - from.northing);
        const auto steps =
            step > 0.0 ? std::max<std::size_t>(1, static_cast<std::size_t>(length / step)) : 1;

        // The far end is left to the next side, which starts there: adding it
        // here would put two points on every corner.
        for (std::size_t s = 0; s < steps; ++s) {
          const double t = static_cast<double>(s) / static_cast<double>(steps);
          const core::Point2D along{from.easting + ((to.easting - from.easting) * t),
                                    from.northing + ((to.northing - from.northing) * t)};
          const double height = (height_at(along) + kFenceLift) * exaggeration_;
          points->InsertNextPoint(along.easting, along.northing, height);
          ++placed;
        }
      }

      lines->InsertNextCell(static_cast<int>(placed + 1));
      for (vtkIdType i = 0; i < placed; ++i) {
        lines->InsertCellPoint(first + i);
      }
      // Closed, because a paddock is a ring and an open one is drawn with a
      // fence missing.
      lines->InsertCellPoint(first);
    }
    into->SetPoints(points);
    into->SetLines(lines);
    into->Modified();
  };

  std::vector<std::size_t> all(boundaries_.size());
  for (std::size_t i = 0; i < all.size(); ++i) {
    all[i] = i;
  }
  build(all, fences_);
  build(grazed_, grazed_fences_);
}

void TerrainScene::set_boundaries(const std::vector<core::Polygon>& boundaries) {
  boundaries_ = boundaries;
  grazed_.clear();
  rebuild_fences();
}

void TerrainScene::show_grazed(const std::vector<std::size_t>& grazed) {
  grazed_ = grazed;
  rebuild_fences();
}

void TerrainScene::clear_boundaries() {
  boundaries_.clear();
  grazed_.clear();
  rebuild_fences();
}

void TerrainScene::light_for(double latitude_degrees, int day_of_year, double solar_hour,
                             double clearness_index) {
  const core::SunPosition sun = core::sun_position(latitude_degrees, day_of_year, solar_hour);

  // A direction on the unit sphere, from the compass bearing and the height.
  // VTK's y is north and its x is east, matching the ground coordinates this
  // scene draws in.
  const double elevation = sun.elevation_degrees * kDegreesToRadians;
  const double azimuth = sun.azimuth_degrees * kDegreesToRadians;
  const double horizontal = std::cos(elevation);
  const double east = horizontal * std::sin(azimuth);
  const double north = horizontal * std::cos(azimuth);
  const double up = std::sin(elevation);

  // The light shines from the sun towards the focal point, so the light's
  // position is out along the sun's direction. Distance is arbitrary for a
  // directional light and only has to be outside the scene.
  constexpr double kFarAway = 100000.0;
  sun_->SetFocalPoint(0.0, 0.0, 0.0);
  sun_->SetPosition(east * kFarAway, north * kFarAway, std::max(up, 0.05) * kFarAway);

  // How much of the sun got through, and what that leaves the sky looking
  // like. The intensities are a drawing choice; the number driving them is
  // not. Clear is not full brightness, because a surface lit at 1.0 with no
  // ambient reads as white paper rather than as a paddock.
  const double clearness = std::clamp(clearness_index, 0.0, 1.0);
  const core::SkyCondition sky = core::sky_from_clearness(clearness);

  // Between the Angstrom endpoints, so a dull day is dimmer than a bright one
  // by the ratio the radiation actually differed by.
  constexpr double kOvercastClearness = 0.25;
  constexpr double kClearClearness = 0.75;
  const double lit = std::clamp(
      (clearness - kOvercastClearness) / (kClearClearness - kOvercastClearness), 0.0, 1.0);

  // **The split between the two lights is the clearness index itself**, and
  // that is not a fudge to keep the picture bright. Rs/Ra IS the share of the
  // sky's radiation that arrived as direct beam rather than scattered - that
  // is what FAO-56 Eq. 35's Angstrom relation describes - so a clear day is
  // mostly sun and an overcast one is mostly sky. Their sum is held roughly
  // level, because a dull day is duller and not darker.
  //
  // The consequence worth stating: relief is easiest to see on a clear day and
  // nearly flat on an overcast one, which is what a paddock looks like.
  //
  // Their SPLIT is the clearness index and is not a choice. Their SUM is a
  // drawing choice, and is said to be one: it is set so that level ground
  // lands a little under full brightness, because a surface lit to 1.0 washes
  // the colour ramp out - a paddock at 3500 kg DM/ha came back grey-green
  // instead of green, and read as having less feed on it rather than more.
  sun_->SetIntensity(0.25 + (0.55 * lit));
  sky_->SetIntensity(0.45 - (0.20 * lit));

  // A dull sky is a little bluer than a bright one, and the sun a little
  // warmer. Both stay close to white: this is lighting, not a filter.
  sky_->SetColor(0.90 + (0.06 * lit), 0.93 + (0.04 * lit), 1.0);

  // Warm and low under a clear sky, flat and grey under cloud - which is what
  // the light itself does, not a filter over the picture.
  sun_->SetColor(1.0 - (0.10 * (1.0 - lit)), 0.98 - (0.06 * (1.0 - lit)),
                 0.92 + (0.06 * (1.0 - lit)));

  switch (sky) {
    case core::SkyCondition::Clear:
      renderer_->SetBackground(0.09, 0.13, 0.20);
      break;
    case core::SkyCondition::PartlyCloudy:
      renderer_->SetBackground(0.13, 0.15, 0.18);
      break;
    case core::SkyCondition::Overcast:
      renderer_->SetBackground(0.17, 0.18, 0.19);
      break;
  }

  // Below the horizon there is no sun to light anything with. It cannot happen
  // at 14:00 solar time anywhere in New Zealand - the lowest is about 17
  // degrees at midwinter - but this scene does not know it will only ever be
  // asked about that hour.
  if (!sun.is_up()) {
    sun_->SetIntensity(0.15);
  }
}

void TerrainScene::set_vertical_exaggeration(double factor) {
  // A factor of zero or less is a flat farm drawn as a claim about terrain,
  // which is worse than not drawing it.
  exaggeration_ = std::max(factor, 0.01);
  rebuild_surface();
  rebuild_fences();
  rebuild_mobs();
}

std::size_t TerrainScene::fence_ring_count() const {
  return static_cast<std::size_t>(fences_->GetNumberOfLines());
}

std::size_t TerrainScene::grazed_ring_count() const {
  return static_cast<std::size_t>(grazed_fences_->GetNumberOfLines());
}

void TerrainScene::reset_camera() {
  // Placed from the data's own bounds rather than rotated away from whatever
  // ResetCamera chose.
  //
  // ResetCamera puts the camera on the +z axis looking straight down, which for
  // ground this flat is almost exactly overhead. Setting the view up to +z from
  // there makes it parallel to the view direction, the camera basis collapses,
  // and the farm disappears - a black window with a legend on it, which is what
  // this drew until the geometry tests said the surface was fine and the camera
  // was not.
  std::array<double, 6> bounds{};
  renderer_->ComputeVisiblePropBounds(bounds.data());
  if (bounds[0] > bounds[1]) {
    return;
  }

  const double centre_x = (bounds[0] + bounds[1]) / 2.0;
  const double centre_y = (bounds[2] + bounds[3]) / 2.0;
  const double centre_z = (bounds[4] + bounds[5]) / 2.0;
  const double width = std::max(bounds[1] - bounds[0], bounds[3] - bounds[2]);

  vtkCamera* camera = renderer_->GetActiveCamera();
  camera->ParallelProjectionOff();
  camera->SetFocalPoint(centre_x, centre_y, centre_z);
  // From the north-west and above: the angle a farm is photographed from, and
  // the one that shows a slope as a slope. Straight down would be the flat map
  // again, and straight on would hide everything behind the first ridge.
  camera->SetPosition(centre_x - width, centre_y - width, centre_z + (width * 0.8));
  camera->SetViewUp(0.0, 0.0, 1.0);
  renderer_->ResetCamera();
  renderer_->ResetCameraClippingRange();
}

}  // namespace paddock::viz
