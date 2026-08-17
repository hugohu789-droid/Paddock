// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>
#include <vtkAxisActor2D.h>
#include <vtkCamera.h>
#include <vtkTextProperty.h>

#include <paddock/viz/MapScene.hpp>

namespace paddock::viz {

namespace {

/// Entries in the lookup table. 256 is more than a display can distinguish and
/// small enough to build every frame without noticing.
constexpr int kLookupEntries = 256;

constexpr double kColourMaximum = 255.0;

/// Labels on the colour bar.
constexpr int kLegendLabels = 5;

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
  // The image sits at the raster's south-west corner in NZTM2000 metres, so
  // that a later layer - a paddock boundary, a farm track - can be drawn in the
  // same coordinates without a conversion step.
  image_->SetOrigin(raster.transform().origin_easting,
                    raster.transform().origin_northing - (rows * cell), 0.0);
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

  const std::vector<Rgb> table = scale.lookup_table(kLookupEntries);
  lookup_->SetNumberOfTableValues(static_cast<vtkIdType>(table.size()));
  lookup_->SetTableRange(scale.minimum(), scale.maximum());
  for (std::size_t i = 0; i < table.size(); ++i) {
    lookup_->SetTableValue(static_cast<vtkIdType>(i), table[i].red / kColourMaximum,
                           table[i].green / kColourMaximum, table[i].blue / kColourMaximum, 1.0);
  }
  lookup_->Build();

  colours_->SetInputData(image_);
  colours_->Update();
  actor_->SetInputData(colours_->GetOutput());

  legend_->SetTitle(title.c_str());
  legend_->SetLookupTable(lookup_);
  const std::string format = tick_label_format(scale.minimum(), scale.maximum(), kLegendLabels);
  legend_->SetLabelFormat(format.c_str());

  has_field_ = true;
}

void MapScene::reset_camera() {
  renderer_->ResetCamera();
  // A map is looked at from directly above, at the scale the raster covers;
  // VTK's default camera would show it in perspective and at an angle.
  renderer_->GetActiveCamera()->ParallelProjectionOn();
  renderer_->ResetCameraClippingRange();
}

}  // namespace paddock::viz
