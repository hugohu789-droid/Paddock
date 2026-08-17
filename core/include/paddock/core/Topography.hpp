// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <paddock/core/Raster.hpp>

/// Slope and aspect from an elevation raster.
///
/// Pure arithmetic on core's own Raster<double>, so it needs no GDAL and runs
/// on a machine with only a compiler. Reading the DEM is gis/'s job; deciding
/// what its shape means is the model's, and the model lives here.
namespace paddock::core {

/// Ground steepness in degrees from horizontal, and the compass direction the
/// ground faces, in degrees clockwise from true north.
struct Topography {
  Raster<double> slope_degrees;

  /// 0 is north, 90 east, 180 south, 270 west.
  ///
  /// **NaN where the ground is flat**, because flat ground has no aspect. A
  /// sentinel like -9999 (GDAL's choice) or 0 (ArcGIS's other choice) both read
  /// as a real direction to code that forgets to check, and "flat ground faces
  /// north" is exactly the kind of wrong that never announces itself. NaN
  /// propagates into anything that uses it without checking, which fails a test
  /// rather than tilting a farm.
  Raster<double> aspect_degrees;
};

/// Slope and aspect by Horn's method.
///
/// Horn, B.K.P. (1981), "Hill shading and the reflectance map", Proceedings of
/// the IEEE 69(1):14-47. It is the third-order finite difference over a 3x3
/// window that GDAL's `gdaldem` and ArcGIS both use by default, which is what
/// lets this implementation be checked against theirs rather than only against
/// itself.
///
/// Gradients are taken in world terms - metres of rise per metre east and per
/// metre north - and aspect is the compass bearing of steepest descent:
/// atan2(-rise_east, -rise_north). Ground that falls to the north faces north.
///
/// Edge cells have no full 3x3 window, so the outermost row and column are
/// repeated to fill it - the same convention as `gdaldem -compute_edges`. The
/// raster therefore keeps its size, and edge values are estimates from half a
/// window: fine for a farm interior, not to be trusted on the boundary of a
/// clipped DEM.
///
/// Throws std::out_of_range if `elevation` is smaller than 2x2 in either
/// direction: there is no neighbour to difference against. A non-positive cell
/// size cannot arrive here - Raster's constructor rejects it.
[[nodiscard]] Topography topography_of(const Raster<double>& elevation);

}  // namespace paddock::core
