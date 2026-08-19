// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <vtkAppendPolyData.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkContourFilter.h>
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkGPUVolumeRayCastMapper.h>
#include <vtkPerlinNoise.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkSampleFunction.h>
#include <vtkSphereSource.h>
#include <vtkStructuredGridGeometryFilter.h>
#include <vtkTextProperty.h>
#include <vtkVolumeProperty.h>

#include <paddock/core/Distributions.hpp>
#include <paddock/core/Solar.hpp>
#include <paddock/viz/CloudLayer.hpp>
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

constexpr double kPi = 3.14159265358979323846;

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

  // The sky. Lighting is off on all four: these are symbols and their colour
  // is their meaning, so a light falling across them would change what they
  // say.
  sun_actor_->SetMapper(nullptr);
  for (const auto& pair : {std::make_pair(sun_disc_.Get(), sun_actor_.Get()),
                           std::make_pair(rain_lines_.Get(), rain_actor_.Get()),
                           std::make_pair(wind_marks_.Get(), wind_actor_.Get())}) {
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(pair.first);
    // **No scalar colouring on any of these.** Their colour is their meaning
    // and it is set on the actor. The cloud is cut from a noise field and
    // carries that field's values with it; mapped through a lookup table they
    // painted it bright red, which is a data colour on a thing that is not
    // data.
    mapper->ScalarVisibilityOff();
    pair.second->SetMapper(mapper);
    pair.second->GetProperty()->LightingOff();
    renderer_->AddActor(pair.second);
  }

  sun_actor_->GetProperty()->SetColor(1.0, 0.93, 0.62);

  // The cloud is a volume, not an actor with a surface. Ray casting through a
  // density field is how cloud is actually drawn: there is no shell to catch
  // the light wrongly, and the edges fray because the density does.
  vtkNew<vtkGPUVolumeRayCastMapper> cloud_mapper;
  cloud_mapper->SetInputData(cloud_density_);
  cloud_mapper->SetBlendModeToComposite();

  vtkNew<vtkVolumeProperty> cloud_property;
  cloud_property->SetColor(cloud_colour_);
  cloud_property->SetScalarOpacity(cloud_opacity_);
  cloud_property->SetInterpolationTypeToLinear();
  // **Unshaded, and the colour transfer function is what makes it read as a
  // volume.** Volume shading works from the gradient of the density, and the
  // gradient of a noise field points every which way: every sample picked up
  // its own facet of shadow and the cloud came out nearly black whatever
  // colour it was given. The light and dark of a cloud is already in the
  // colour ramp - pale at the thin edges, darker through the core - and that
  // is a quantity, not a lighting accident.
  cloud_property->ShadeOff();

  cloud_volume_->SetMapper(cloud_mapper);
  cloud_volume_->SetProperty(cloud_property);
  // Hidden until a day has been drawn. An empty volume still has bounds - a
  // unit box at the origin - and a renderer asked for the extent of what it
  // can see would hand back a farm stretching from Lincoln to the middle of
  // the projection. That is not a test artefact: the camera is placed from
  // those bounds, so a stray volume would have framed every scene wrongly.
  cloud_volume_->SetVisibility(0);
  renderer_->AddVolume(cloud_volume_);
  rain_actor_->GetProperty()->SetColor(0.62, 0.76, 0.95);
  rain_actor_->GetProperty()->SetLineWidth(1.5);
  wind_actor_->GetProperty()->SetColor(0.80, 0.85, 0.92);
  wind_actor_->GetProperty()->SetLineWidth(2.0);

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

  // Where to hang the sky. Taken from the field rather than from the render
  // window, so it is the farm's extent and not whatever happens to be in view.
  {
    const core::GeoTransform& transform = field_.transform();
    sky_width_ = static_cast<double>(cols) * transform.cell_size;
    sky_height_ = static_cast<double>(rows) * transform.cell_size;
    sky_east_ = transform.origin_easting + (sky_width_ / 2.0);
    sky_north_ = transform.origin_northing - (sky_height_ / 2.0);
    // **Clear of the ground, whatever the ground is doing.** The sky has to sit
    // above the terrain as drawn, not above sea level, or a farm at twenty
    // times exaggeration reaches up into its own weather - which put a
    // translucent cloud between the camera and the paddocks and tinted them.
    // That is the same fault the weather was moved out of the lighting to
    // avoid, arriving through a different door.
    sky_top_ = (highest_m_ * exaggeration_) + (std::max(sky_width_, sky_height_) * 0.85);
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

void TerrainScene::light_the_ground() {
  // **The ground is lit by a fixed light, and the weather does not touch it.**
  //
  // The colour on this surface is a reading off a legend: 3500 kg DM/ha has to
  // be the same pixel colour on every day of the run, or the map cannot be
  // compared with its own scale, let alone with yesterday. Lighting it with
  // the day's sun broke exactly that - the same paddock came out pale under
  // cloud and was read as having less feed on it, when what had changed was
  // the light.
  //
  // So this light never moves and never dims. What it gives is relief: a
  // slope facing it is brighter than one facing away, which is the hillshade
  // of any topographic map, and it is the same hillshade every day. The
  // weather is drawn in the sky instead, where it is a picture of itself
  // rather than a filter over the data.
  //
  // North-west at 45 degrees, which is where cartographic convention puts the
  // light. It is not where the sun is - the real sun is drawn overhead by
  // show_sky() - and it is not meant to be.
  constexpr double kHillshadeBearing = 315.0 * kDegreesToRadians;
  constexpr double kHillshadeElevation = 45.0 * kDegreesToRadians;
  constexpr double kFarAway = 100000.0;

  const double horizontal = std::cos(kHillshadeElevation);
  sun_->SetFocalPoint(0.0, 0.0, 0.0);
  sun_->SetPosition(horizontal * std::sin(kHillshadeBearing) * kFarAway,
                    horizontal * std::cos(kHillshadeBearing) * kFarAway,
                    std::sin(kHillshadeElevation) * kFarAway);
  sun_->SetIntensity(0.55);
  sun_->SetColor(1.0, 1.0, 1.0);

  // A fill from overhead so that ground facing away from the hillshade is
  // shaded rather than black. Also fixed.
  sky_->SetFocalPoint(0.0, 0.0, 0.0);
  sky_->SetPosition(0.0, 0.0, kFarAway);
  sky_->SetIntensity(0.45);
  sky_->SetColor(1.0, 1.0, 1.0);
}

namespace {

/// A ball of `radius` at `centre`, as polygons.
///
/// A ball rather than a disc, and the reason is not decoration: a flat disc
/// has to face somewhere, and from any other angle it is an ellipse. The sun
/// came out squashed that way. A sphere is round from everywhere, which is
/// what the sun is.
void append_ball(const std::array<double, 3>& centre, double radius, int resolution,
                 vtkAppendPolyData* into) {
  vtkNew<vtkSphereSource> ball;
  ball->SetCenter(centre[0], centre[1], centre[2]);
  ball->SetRadius(radius);
  ball->SetThetaResolution(resolution);
  ball->SetPhiResolution(resolution);
  ball->Update();
  into->AddInputData(ball->GetOutput());
}

}  // namespace

bool TerrainScene::cloud_matches_farm() const noexcept {
  if (!cloud_built_) {
    return false;
  }
  // Exact comparison. These are read from the manifest and passed through
  // rather than computed, so two farms are the same farm when their extents
  // are the same numbers - and a tolerance here would only invent a distance
  // below which one farm's cloud may hang over another's.
  return cloud_built_east_ == sky_east_ && cloud_built_north_ == sky_north_ &&
         cloud_built_span_ == std::max(sky_width_, sky_height_) && cloud_built_top_ == sky_top_;
}

void TerrainScene::build_cloud_density() {
  // 128 x 128 x 64 voxels over the sky above the farm - a million of them,
  // which is plenty for a farm and cheap enough to build once.
  //
  // **Built once, on purpose.** The field never changes; what changes with the
  // day is the opacity it is drawn at. Rebuilding it every frame would cost a
  // million noise evaluations for a picture that must not move anyway - the
  // series carries no cloud field, so a cloud that rearranged itself overnight
  // would be showing weather nobody recorded.
  constexpr int kAcross = 128;
  constexpr int kDeep = 64;
  const double span = std::max(sky_width_, sky_height_);
  const double half_w = sky_width_ * 0.55;
  const double half_h = sky_height_ * 0.55;
  const double thickness = span * 0.22;

  cloud_density_->SetDimensions(kAcross, kAcross, kDeep);
  cloud_density_->SetOrigin(sky_east_ - half_w, sky_north_ - half_h, sky_top_ - (thickness / 2.0));
  cloud_density_->SetSpacing((2.0 * half_w) / (kAcross - 1), (2.0 * half_h) / (kAcross - 1),
                             thickness / (kDeep - 1));
  cloud_density_->AllocateScalars(VTK_FLOAT, 1);

  vtkNew<vtkPerlinNoise> noise;
  const double frequency = 4.5 / span;
  noise->SetFrequency(frequency, frequency, frequency);

  // A second, finer pass laid over the first. One frequency gives smooth
  // lumps; two give a cloud an edge that frays.
  vtkNew<vtkPerlinNoise> detail;
  const double fine = frequency * 3.1;
  detail->SetFrequency(fine, fine, fine);
  detail->SetPhase(0.3, 0.7, 0.1);

  auto* voxels = static_cast<float*>(cloud_density_->GetScalarPointer());
  const double base = sky_top_ - (thickness / 2.0);
  for (int k = 0; k < kDeep; ++k) {
    const double z = base + (k * (thickness / (kDeep - 1)));

    // Where in the slab this layer sits, 0 at the base and 1 at the top.
    const double up = static_cast<double>(k) / (kDeep - 1);

    // A cloud has a flat underside and a domed top: dense low down, thinning
    // upward. Both ends are faded so the field does not end in a hard face.
    const double shape = std::sin(3.14159265358979323846 * std::pow(up, 0.7));

    for (int j = 0; j < kAcross; ++j) {
      const double y = (sky_north_ - half_h) + (j * ((2.0 * half_h) / (kAcross - 1)));
      for (int i = 0; i < kAcross; ++i) {
        const double x = (sky_east_ - half_w) + (i * ((2.0 * half_w) / (kAcross - 1)));

        std::array<double, 3> at{x, y, z};
        const double coarse = noise->EvaluateFunction(at.data());
        const double fine_value = detail->EvaluateFunction(at.data());
        const double lumpy = (0.72 * coarse) + (0.28 * fine_value);

        // Fade towards the edges of the box, so the cloud does not end in a
        // wall where the sampling does.
        const double edge_x = 1.0 - std::pow(std::abs((x - sky_east_) / half_w), 3.0);
        const double edge_y = 1.0 - std::pow(std::abs((y - sky_north_) / half_h), 3.0);
        const double edge = std::clamp(edge_x, 0.0, 1.0) * std::clamp(edge_y, 0.0, 1.0);

        const double density = std::clamp((lumpy + 1.0) / 2.0, 0.0, 1.0) * shape * edge;
        voxels[(static_cast<std::size_t>(k) * kAcross * kAcross) +
               (static_cast<std::size_t>(j) * kAcross) + static_cast<std::size_t>(i)] =
            static_cast<float>(density);
      }
    }
  }
  cloud_density_->Modified();
  cloud_built_east_ = sky_east_;
  cloud_built_north_ = sky_north_;
  cloud_built_span_ = std::max(sky_width_, sky_height_);
  cloud_built_top_ = sky_top_;
  cloud_built_ = true;
}

void TerrainScene::show_weather(bool visible) {
  weather_shown_ = visible;
  const int on = visible ? 1 : 0;
  sun_actor_->SetVisibility(on);
  cloud_volume_->SetVisibility(on);
  rain_actor_->SetVisibility(on);
  wind_actor_->SetVisibility(on);
}

void TerrainScene::show_sky(double latitude_degrees, int day_of_year, double solar_hour,
                            double clearness_index, double rainfall_mm, double wind_speed_m_per_s,
                            double uv_index) {
  if (!has_field_) {
    return;
  }
  const double clearness = std::clamp(clearness_index, 0.0, 1.0);
  const double span = std::max(sky_width_, sky_height_);

  // How high the weather floats over the ground as drawn. Everything else in
  // the sky is placed against this, and it already clears the terrain at any
  // exaggeration - see where sky_top_ is worked out.
  const double cloud_height = sky_top_;

  // ------------------------------------------------------------------ the sun
  //
  // **On its true ray.** The bearing and the elevation are FAO-56 geometry for
  // the date, the latitude and the hour, and neither is bent to tidy the
  // picture. What is chosen is how far along that ray it sits, and it is
  // chosen so that it clears the cloud: a sun drawn behind the weather it is
  // shining through is a picture of nothing.
  //
  // A low winter sun therefore sits further out than a high summer one, which
  // is what a low sun does.
  const core::SunPosition sun = core::sun_position(latitude_degrees, day_of_year, solar_hour);
  const double elevation = sun.elevation_degrees * kDegreesToRadians;
  const double azimuth = sun.azimuth_degrees * kDegreesToRadians;
  // **On a dome whose origin sits above the cloud.**
  //
  // The bearing and the elevation are FAO-56 geometry for the date, the
  // latitude and the hour, and neither is bent to tidy the picture. What is
  // chosen is where that dome is centred and how wide it is - and the distance
  // to the sun means nothing in a drawing, it being 150 million km away, so
  // choosing it costs no truth.
  //
  // Centred above the cloud rather than on the ground, so the sun is never
  // drawn behind the weather it is shining through; and narrow enough that it
  // stays over the farm rather than out past its edge, which is where an
  // earlier version put it.
  const double cloud_top = cloud_height + (span * 0.10);
  const double dome = span * 0.55;
  const std::array<double, 3> sun_at{sky_east_ + (dome * std::cos(elevation) * std::sin(azimuth)),
                                     sky_north_ + (dome * std::cos(elevation) * std::cos(azimuth)),
                                     cloud_top + (dome * std::sin(std::max(elevation, 0.0)))};
  {
    vtkNew<vtkAppendPolyData> append;
    append_ball(sun_at, span * 0.06, 24, append);
    append->Update();
    sun_disc_->DeepCopy(append->GetOutput());
  }

  // Brightness from the ultraviolet index, which is measured, and not from the
  // radiation, which is a different quantity entirely. New Zealand's runs from
  // about 1 in midwinter to above 12 in midsummer, so 12 is the top of the
  // scale. A series carrying no ultraviolet gets a fixed sun rather than an
  // invented one.
  const double uv = std::clamp(uv_index / 12.0, 0.0, 1.0);
  const double glow = uv_index > 0.0 ? 0.35 + (0.65 * uv) : 0.7;
  sun_actor_->GetProperty()->SetColor(1.0, 0.80 + (0.15 * glow), 0.30 + (0.45 * glow));
  sun_actor_->GetProperty()->SetOpacity(sun.is_up() ? 0.55 + (0.45 * glow) : 0.2);

  // ---------------------------------------------------------------- the cloud
  //
  // A density field, ray cast. The field is built once and never moves; what
  // the day changes is how much of it is visible.
  //
  // **What is measured and what is drawn.** The clearness index says how much
  // of the sky's radiation reached the ground, read against FAO-56 Eq. 35's
  // Angstrom endpoints - 0.75 for full sun, 0.25 for none of it direct. That
  // becomes the layer's coverage, and coverage sets where the opacity ramp
  // starts: a clear day makes only the densest cores visible and leaves a few
  // fair-weather lumps, an overcast day makes nearly the whole field visible.
  // Everything else about the cloud - how high it floats, how deep it is, how
  // lumpy - is a drawing choice and says so in CloudLayer.
  const CloudLayer layer{std::clamp((0.75 - clearness) / 0.5, 0.0, 1.0), cloud_height, span * 0.22};
  {
    if (!cloud_matches_farm()) {
      build_cloud_density();
    }

    // Where the ramp begins. High on a clear day, so almost nothing shows;
    // low on a dull one, so most of the field does.
    const double onset = 0.62 - (0.42 * layer.coverage);
    const double peak = std::min(0.98, onset + 0.30);

    cloud_opacity_->RemoveAllPoints();
    cloud_opacity_->AddPoint(0.0, 0.0);
    cloud_opacity_->AddPoint(onset, 0.0);
    cloud_opacity_->AddPoint(peak, 0.16 + (0.30 * layer.coverage));
    cloud_opacity_->AddPoint(1.0, 0.22 + (0.38 * layer.coverage));

    // **White to grey, and rain is most of what decides it.**
    //
    // A fair-weather cumulus is white; a cloud that is raining is grey to
    // black from below, and that is not a mood - it is optical depth. Water
    // deep enough to fall out of a cloud is deep enough to stop the light
    // getting through it. So the day's rainfall darkens it hardest, and how
    // much of the sky it covers darkens it a little more.
    //
    // A dry overcast day therefore stays pale, which is what a high sheet of
    // cloud looks like, and 8 mm of rain gives the dark base of a shower.
    const double wet = std::clamp(rainfall_mm / 12.0, 0.0, 1.0);
    const double dark = std::clamp((0.62 * wet) + (0.24 * layer.coverage), 0.0, 1.0);
    const double top = 1.0 - (0.45 * dark);
    const double base = 1.0 - (0.72 * dark);

    // Lighter where the cloud is thin and darker in its core, which is the
    // other half of what makes a cloud read as a volume rather than a lump of
    // one colour.
    cloud_colour_->RemoveAllPoints();
    cloud_colour_->AddRGBPoint(0.0, top, top, std::min(1.0, top + 0.02));
    cloud_colour_->AddRGBPoint(0.55, (top + base) / 2.0, (top + base) / 2.0,
                               std::min(1.0, ((top + base) / 2.0) + 0.02));
    cloud_colour_->AddRGBPoint(1.0, base, base, std::min(1.0, base + 0.03));

    cloud_volume_->SetVisibility(weather_shown_ && layer.coverage > 0.02 ? 1 : 0);
  }

  // ----------------------------------------------------------------- the rain
  //
  // Falling over the whole farm, because the day's rainfall is one number for
  // the whole farm. How many lines follows the millimetres; where each one
  // falls means nothing and is fixed, so the picture does not shimmer while
  // the timeline plays.
  {
    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> lines;
    const int drops = static_cast<int>(std::clamp(rainfall_mm * 14.0, 0.0, 450.0));
    std::mt19937_64 placer(0x5EED5EEDULL);
    for (int i = 0; i < drops; ++i) {
      const double east = sky_east_ + (core::uniform(placer, -0.45, 0.45) * sky_width_);
      const double north = sky_north_ + (core::uniform(placer, -0.45, 0.45) * sky_height_);
      const double top = cloud_height * core::uniform(placer, 0.30, 0.94);
      const double length = span * 0.035;
      const vtkIdType first = points->GetNumberOfPoints();
      points->InsertNextPoint(east, north, top);
      points->InsertNextPoint(east, north, top - length);
      lines->InsertNextCell(2);
      lines->InsertCellPoint(first);
      lines->InsertCellPoint(first + 1);
    }
    rain_lines_->SetPoints(points);
    rain_lines_->SetLines(lines);
    rain_lines_->Modified();
    rain_actor_->SetVisibility(drops > 0 ? 1 : 0);
    rain_actor_->GetProperty()->SetOpacity(0.45 + std::clamp(rainfall_mm / 30.0, 0.0, 0.45));
  }

  // ----------------------------------------------------------------- the wind
  //
  // **Strength and nothing else.** The series carries a speed and no bearing,
  // so the gusts are laid out around the whole compass at once and no one of
  // them is the wind's. A set of parallel streaks would have drawn the single
  // thing the data does not have.
  //
  // Curved and multi-segment so they read as moving air rather than as marks
  // on a pane.
  {
    const double strength = std::clamp(wind_speed_m_per_s / 20.0, 0.0, 1.0);
    const int gusts = static_cast<int>(std::round(strength * 8.0));
    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> lines;

    for (int gust = 0; gust < gusts; ++gust) {
      const double bearing = (2.0 * kPi * gust) / std::max(gusts, 1);
      const double radius = span * (0.34 + (0.06 * (gust % 3)));
      const double height = cloud_height + (span * 0.10) + (span * 0.02 * ((gust % 4) - 1.5));
      const double arc = 0.55 + (0.45 * strength);

      constexpr int kSegments = 12;
      const vtkIdType first = points->GetNumberOfPoints();
      for (int i = 0; i <= kSegments; ++i) {
        const double along = bearing + ((arc * i) / kSegments) - (arc / 2.0);
        points->InsertNextPoint(sky_east_ + (radius * std::sin(along)),
                                sky_north_ + (radius * std::cos(along) * 0.55),
                                height + (span * 0.015 * std::sin(3.0 * along)));
      }
      lines->InsertNextCell(kSegments + 1);
      for (int i = 0; i <= kSegments; ++i) {
        lines->InsertCellPoint(first + i);
      }
    }

    wind_marks_->SetPoints(points);
    wind_marks_->SetLines(lines);
    wind_marks_->Modified();
    wind_actor_->SetVisibility(gusts > 0 ? 1 : 0);
    wind_actor_->GetProperty()->SetLineWidth(static_cast<float>(1.0 + (2.5 * strength)));
    wind_actor_->GetProperty()->SetOpacity(0.40 + (0.45 * strength));
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
