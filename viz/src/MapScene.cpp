// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>
#include <vtkAxisActor2D.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>

#include <paddock/viz/ColourTable.hpp>
#include <paddock/viz/MapScene.hpp>
#include <paddock/viz/MobMarkers.hpp>

namespace paddock::viz {

namespace {

/// Entries in the lookup table. 256 is more than a display can distinguish and
/// small enough to build every frame without noticing.
constexpr int kLookupEntries = 256;

/// Labels on the colour bar.
constexpr int kLegendLabels = 5;

/// Fence widths, in pixels. The rested paddocks are drawn thin and half
/// transparent so they read as context; the grazed ones are what the eye should
/// go to.
constexpr float kFenceWidth = 1.0F;
constexpr float kGrazedFenceWidth = 3.0F;

/// Thicker than the grazed ring, which it is often drawn on top of.
constexpr float kSelectedFenceWidth = 5.0F;
constexpr double kFenceOpacity = 0.45;

/// How wide a mob's marker is drawn on the ground, in metres.
///
/// In ground units rather than pixels, so it scales with the farm: a mob is a
/// thing standing in a paddock, and zooming in should make it larger the way
/// the paddock does. Forty metres is small against a 150 m paddock and visible
/// on a 1.2 km farm.
// One animal, not one mob. At 8 m across a sheep is far larger than life -
// a real one is about a metre - but a marker has to be findable on a farm
// drawn a kilometre wide, and a flock of them reads as a flock.
constexpr double kMarkerSizeM = 8.0;

/// Builds one polydata of closed rings from the polygons at `indices`.
void build_rings(const std::vector<core::Polygon>& boundaries,
                 const std::vector<std::size_t>& indices, vtkPolyData* into) {
  vtkNew<vtkPoints> points;
  vtkNew<vtkCellArray> lines;

  for (const std::size_t index : indices) {
    if (index >= boundaries.size()) {
      continue;
    }
    const std::vector<core::Point2D>& vertices = boundaries[index].vertices();
    if (vertices.size() < 3) {
      continue;
    }
    const vtkIdType first = points->GetNumberOfPoints();
    for (const core::Point2D& vertex : vertices) {
      // Slightly above the image, or the fence and the field fight for the same
      // depth and the line disappears in patches as the camera moves.
      points->InsertNextPoint(vertex.easting, vertex.northing, 0.1);
    }
    const auto count = static_cast<vtkIdType>(vertices.size());
    // InsertNextCell counts points in an int while the ids are vtkIdType, which
    // is 64-bit. A paddock has a handful of corners, so the cast is safe here
    // and saying so explicitly is better than letting it narrow silently.
    lines->InsertNextCell(static_cast<int>(count + 1));
    for (vtkIdType i = 0; i < count; ++i) {
      lines->InsertCellPoint(first + i);
    }
    // Back to the start: a paddock is a closed ring, and leaving it open draws
    // every paddock with one fence missing.
    lines->InsertCellPoint(first);
  }

  into->SetPoints(points);
  into->SetLines(lines);
  into->Modified();
}

}  // namespace

MapScene::MapScene() {
  renderer_->SetBackground(0.12, 0.13, 0.15);
  renderer_->AddActor(actor_);

  legend_->SetLookupTable(lookup_);
  legend_->SetNumberOfLabels(kLegendLabels);
  legend_->SetWidth(0.09);
  legend_->SetHeight(0.55);
  legend_->SetPosition(0.88, 0.2);
  // Left to itself, vtkScalarBarActor scales label text to fill the space each
  // label is allotted, so a short label is drawn in a huge font: "0" and "1"
  // came out taller than the bar was wide. A fixed size keeps the legend
  // legible whether the numbers are one character or six.
  legend_->UnconstrainedFontSizeOn();
  for (vtkTextProperty* text : {legend_->GetLabelTextProperty(), legend_->GetTitleTextProperty()}) {
    text->SetColor(1.0, 1.0, 1.0);
    text->SetShadow(0);
    text->SetItalic(0);
    text->SetFontSize(14);
  }
  // AddViewProp rather than AddActor2D: the latter is deprecated in VTK 9.5,
  // and the deprecation fires at the call site, so -isystem does not hide it.
  renderer_->AddViewProp(legend_);

  // Distances across the farm, not absolute coordinates.
  //
  // NZTM2000 eastings and northings are seven digits, and labelled absolutely
  // they are unreadable at any font size that fits: the first attempt printed
  // 1.57e+06 five times, and the second printed seven digits that the window
  // edge cut in half. On a farm 1.2 km across, what a reader needs from an axis
  // is how far it is from one side to the other; the exact corner coordinates
  // are in the window title, where they are read once.
  axes_->TopAxisVisibilityOff();
  axes_->RightAxisVisibilityOff();
  axes_->SetLabelModeToDistance();
  axes_->GetLeftAxis()->SetTitle("metres north-south");
  axes_->GetBottomAxis()->SetTitle("metres east-west");
  for (vtkAxisActor2D* axis : {axes_->GetLeftAxis(), axes_->GetBottomAxis()}) {
    axis->SetLabelFormat("%.0f");
    axis->SetNumberOfLabels(3);
    axis->SetFontFactor(0.8);
    for (vtkTextProperty* text : {axis->GetLabelTextProperty(), axis->GetTitleTextProperty()}) {
      text->SetItalic(0);
      text->SetShadow(0);
    }
  }
  // The left axis title reads up the side of the map, as it does on every
  // printed map and plot. Horizontally it is both unconventional and wide
  // enough that the window edge cuts the first characters off.
  axes_->GetLeftAxis()->GetTitleTextProperty()->SetOrientation(90);
  axes_->SetLeftBorderOffset(100);
  axes_->SetBottomBorderOffset(90);
  renderer_->AddViewProp(axes_);

  colours_->SetLookupTable(lookup_);
  colours_->SetOutputFormatToRGB();

  fence_mapper_->SetInputData(fences_);
  fence_actor_->SetMapper(fence_mapper_);
  fence_actor_->GetProperty()->SetColor(1.0, 1.0, 1.0);
  fence_actor_->GetProperty()->SetLineWidth(kFenceWidth);
  fence_actor_->GetProperty()->SetOpacity(kFenceOpacity);
  // Lighting would shade a flat line by its normal, which for a ring drawn in
  // the ground plane means it comes out grey and uneven.
  fence_actor_->GetProperty()->LightingOff();
  renderer_->AddActor(fence_actor_);

  grazed_mapper_->SetInputData(grazed_fences_);
  grazed_actor_->SetMapper(grazed_mapper_);
  // Amber against the greens and the viridis blues, neither of which has
  // anything near it, so "being grazed today" cannot be mistaken for a value on
  // the colour scale.
  grazed_actor_->GetProperty()->SetColor(1.0, 0.72, 0.2);
  grazed_actor_->GetProperty()->SetLineWidth(kGrazedFenceWidth);
  grazed_actor_->GetProperty()->LightingOff();
  renderer_->AddActor(grazed_actor_);

  selected_mapper_->SetInputData(selected_fence_);
  selected_actor_->SetMapper(selected_mapper_);
  // White and thicker than the amber, because the two can be on the same
  // paddock at once - a mob is often standing on the one somebody clicked - and
  // a selection that only differed by shade would be lost under it.
  selected_actor_->GetProperty()->SetColor(1.0, 1.0, 1.0);
  selected_actor_->GetProperty()->SetLineWidth(kSelectedFenceWidth);
  selected_actor_->GetProperty()->LightingOff();
  renderer_->AddActor(selected_actor_);

  // A layer per kind, built once. Which kinds exist is asked of MobMarkers
  // rather than listed here, so adding an animal does not need this loop found
  // and edited.
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

void MapScene::show_mobs(const std::vector<MobMarker>& markers) {
  for (std::size_t i = 0; i < marker_kinds().size() && i < mob_shapes_.size(); ++i) {
    build_mob_markers(markers, marker_kinds()[i], kMarkerSizeM, {}, boundaries_, mob_shapes_[i]);
  }
}

void MapScene::set_boundaries(const std::vector<core::Polygon>& boundaries) {
  boundaries_ = boundaries;
  std::vector<std::size_t> all(boundaries_.size());
  for (std::size_t i = 0; i < all.size(); ++i) {
    all[i] = i;
  }
  build_rings(boundaries_, all, fences_);
  build_rings(boundaries_, {}, grazed_fences_);
  // A new set of fences is a new farm, and a ring left over from the old one
  // would sit on whichever paddock happened to take that index.
  build_rings(boundaries_, {}, selected_fence_);
}

void MapScene::show_grazed(const std::vector<std::size_t>& grazed) {
  build_rings(boundaries_, grazed, grazed_fences_);
}

void MapScene::show_selected(const std::vector<std::size_t>& selected) {
  build_rings(boundaries_, selected, selected_fence_);
}

std::size_t MapScene::selected_ring_count() const {
  return static_cast<std::size_t>(selected_fence_->GetNumberOfLines());
}

void MapScene::clear_boundaries() {
  boundaries_.clear();
  build_rings(boundaries_, {}, fences_);
  build_rings(boundaries_, {}, grazed_fences_);
  build_rings(boundaries_, {}, selected_fence_);
}

void MapScene::show(const core::Raster<double>& raster, const ColourScale& scale,
                    const std::string& title) {
  if (raster.empty()) {
    has_field_ = false;
    return;
  }

  const auto cols = static_cast<int>(raster.cols());
  const auto rows = static_cast<int>(raster.rows());
  const double cell = raster.transform().cell_size;

  image_->SetDimensions(cols, rows, 1);
  image_->SetSpacing(cell, cell, 1.0);
  // The image is placed in NZTM2000 metres, so that a later layer - a paddock
  // boundary, a farm track - is drawn in the same coordinates with no
  // conversion step.
  //
  // At the CELL CENTRES, not at the south-west corner. A vtkImageData of N
  // samples spans (N - 1) spacings, because its samples are points rather than
  // areas, so anchoring sample zero on the corner of the farm drew every value
  // half a cell south-west of where it belongs and left the field a whole cell
  // short in each direction. Nothing showed it until there were fences to
  // compare against, and then the field sat visibly inside them along the north
  // edge.
  //
  // The consequence of drawing point samples honestly is that the coloured area
  // stops at the outermost cell centres, half a cell inside the boundary, all
  // the way round. That is what the data supports: between two cell centres the
  // colour is interpolated, and beyond the last one there is nothing to
  // interpolate from.
  const double half_cell = cell / 2.0;
  image_->SetOrigin(raster.transform().origin_easting + half_cell,
                    raster.transform().origin_northing - (rows * cell) + half_cell, 0.0);
  image_->AllocateScalars(VTK_DOUBLE, 1);

  // Row 0 of a Paddock raster is the northernmost, because that is how a
  // GeoTIFF is written. VTK's image y axis increases upwards. Copying row for
  // row without this flip produces a map that is upside down and perfectly
  // plausible, which is the worst kind of wrong.
  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      auto* pixel = static_cast<double*>(image_->GetScalarPointer(col, rows - 1 - row, 0));
      *pixel = raster(static_cast<std::size_t>(col), static_cast<std::size_t>(row));
    }
  }
  image_->Modified();

  fill_lookup_table(lookup_, scale, kLookupEntries);

  colours_->SetInputData(image_);
  colours_->Update();
  actor_->SetInputData(colours_->GetOutput());

  legend_->SetTitle(title.c_str());
  legend_->SetLookupTable(lookup_);
  const std::string format = tick_label_format(scale.minimum(), scale.maximum(), kLegendLabels);
  legend_->SetLabelFormat(format.c_str());

  has_field_ = true;
}

std::optional<core::Point2D> MapScene::ground_at(int x, int y) const {
  if (!has_field_) {
    return std::nullopt;
  }

  // A cell picker rather than a prop picker, because what is wanted is the
  // point on the ground and not merely which actor was hit. Restricted to the
  // field: without that the picker happily returns a point on a fence line, a
  // stock marker or - in the terrain view - the underside of a cloud, and a
  // click on a sheep would report the ground somewhere above the paddock.
  vtkNew<vtkCellPicker> picker;
  picker->SetTolerance(0.0005);
  picker->InitializePickList();
  picker->AddPickList(actor_);
  picker->PickFromListOn();

  if (picker->Pick(x, y, 0.0, renderer_) == 0) {
    return std::nullopt;
  }

  std::array<double, 3> position{};
  picker->GetPickPosition(position.data());
  return core::Point2D{position[0], position[1]};
}

core::BoundingBox MapScene::field_bounds() const {
  core::BoundingBox bounds = core::BoundingBox::empty();
  if (!has_field_) {
    return bounds;
  }
  std::array<double, 6> extent{};
  image_->GetBounds(extent.data());
  bounds.expand_to_include(core::Point2D{extent[0], extent[2]});
  bounds.expand_to_include(core::Point2D{extent[1], extent[3]});
  return bounds;
}

std::size_t MapScene::fence_ring_count() const {
  return static_cast<std::size_t>(fences_->GetNumberOfLines());
}

std::size_t MapScene::grazed_ring_count() const {
  return static_cast<std::size_t>(grazed_fences_->GetNumberOfLines());
}

void MapScene::reset_camera() {
  renderer_->ResetCamera();
  // A map is looked at from directly above, at the scale the raster covers;
  // VTK's default camera would show it in perspective and at an angle.
  renderer_->GetActiveCamera()->ParallelProjectionOn();
  renderer_->ResetCameraClippingRange();
}

}  // namespace paddock::viz
