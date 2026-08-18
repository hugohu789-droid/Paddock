// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/core/DataSource.hpp>
#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/Terrain.hpp>

namespace paddock::core {

/// Ground shaped by a formula rather than by a random draw.
///
/// The synthetic adapter for every other source in this project generates from
/// an RNG. This one does not, deliberately, for two reasons.
///
/// The first is determinism without ceremony: an analytic surface is the same
/// on every machine and every standard library, with no seed to derive and no
/// question about what happens when a caller asks for one corner of it rather
/// than the whole farm (ADR 0007, ADR 0008).
///
/// The second is that it makes the terrain work testable. Slope and aspect on a
/// tilted plane have closed-form answers - a plane falling 1 m in 10 to the
/// north has a slope of exactly 5.711 degrees and an aspect of exactly 0 - so
/// the derivatives computed in gis/ can be checked against arithmetic instead
/// of against another implementation's output. A random surface would only ever
/// permit "looks plausible".
struct SyntheticSurface {
  /// The point the surface is defined from, in NZTM2000 metres, and the
  /// elevation there.
  ///
  /// Fixed, not taken from whatever area a caller asks for. Tying it to the
  /// request would mean the same piece of ground came back at a different
  /// height depending on the window around it, which is the mistake ADR 0008
  /// names for weather: fetching March alone must return exactly the March a
  /// full-year fetch returns. The default sits on the central meridian at a
  /// round northing, so offsets stay small anywhere in New Zealand, and the
  /// elevation is Ruakura's - about 40 m above sea level.
  double reference_easting = 1600000.0;
  double reference_northing = 5000000.0;
  double base_elevation_m = 40.0;

  /// Rise in metres per metre travelled east, and per metre travelled north.
  /// Negative values fall in that direction. The default is a plane tilted
  /// gently down to the north-east.
  double gradient_east = -0.01;
  double gradient_north = -0.02;

  /// A smooth undulation laid over the plane. Zero amplitude leaves a pure
  /// plane, which is the case with exact known derivatives; anything else gives
  /// ground that varies in both directions without being noise.
  double undulation_amplitude_m = 0.0;
  double undulation_wavelength_m = 400.0;

  /// Elevation at a point in NZTM2000 metres.
  [[nodiscard]] double elevation_at(Point2D point) const noexcept;
};

/// An ElevationSource over a SyntheticSurface. Covers any area asked of it,
/// because a formula has no extent.
class SyntheticElevationSource : public ElevationSource {
 public:
  explicit SyntheticElevationSource(SyntheticSurface surface = {});

  [[nodiscard]] SourceDescription describe() const override;
  [[nodiscard]] ConnectionStatus test_connection() const override;
  [[nodiscard]] Raster<double> fetch(const BoundingBox& area, double cell_size_m) const override;

  [[nodiscard]] const SyntheticSurface& surface() const noexcept { return surface_; }

 private:
  SyntheticSurface surface_;
};

/// Rectangular paddocks laid out in rows and columns over the area.
///
/// The default paddock size is 2.5 ha, in the middle of the 1.5-3.5 ha range
/// Massey University's Dairy 4 subdivides its 221 effective hectares into -
/// "approximately 80 x 1.5-3.5 hectare paddocks all with race access", from
/// Massey's own farm page, cited in data/farms/massey-dairy-4.toml. It is a
/// stand-in for a real cadastral layer, not a model of one: real paddocks
/// follow fences, drains and contours, and none of those are rectangles.
class SyntheticParcelSource : public ParcelSource {
 public:
  explicit SyntheticParcelSource(double target_paddock_hectares = 2.5);

  [[nodiscard]] SourceDescription describe() const override;
  [[nodiscard]] ConnectionStatus test_connection() const override;
  [[nodiscard]] std::vector<Paddock> fetch(const BoundingBox& area) const override;

 private:
  double target_paddock_hectares_;
};

}  // namespace paddock::core
