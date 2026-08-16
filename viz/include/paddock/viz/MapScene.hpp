#pragma once

#include <string>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>

#include <paddock/core/Raster.hpp>
#include <paddock/viz/ColourScale.hpp>

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

  /// Fits the camera to the current raster. Called after the first `show` and
  /// whenever the widget is resized.
  void reset_camera();

  [[nodiscard]] vtkRenderer* renderer() const noexcept { return renderer_; }

  /// True once a raster has been shown, so a caller can tell an empty scene
  /// from one that failed to build.
  [[nodiscard]] bool has_field() const noexcept { return has_field_; }

 private:
  vtkNew<vtkImageData> image_;
  vtkNew<vtkLookupTable> lookup_;
  vtkNew<vtkImageMapToColors> colours_;
  vtkNew<vtkImageActor> actor_;
  vtkNew<vtkScalarBarActor> legend_;
  vtkNew<vtkRenderer> renderer_;
  bool has_field_ = false;
};

}  // namespace paddock::viz
