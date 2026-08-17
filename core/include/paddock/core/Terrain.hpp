// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/core/DataSource.hpp>
#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>

/// The ports through which ground shape and farm boundaries enter the model.
///
/// These are declared in core and implemented outside it. Core states what an
/// elevation model and a set of paddocks are in its own types - Raster<double>
/// and Polygon - and never learns that GDAL exists. The adapters that read a
/// real GeoTIFF or GeoPackage live in gis/; the ones here need nothing but the
/// standard library, which is what lets the whole scientific suite run on a
/// machine with only a compiler (CLAUDE.md, principle 1).
namespace paddock::core {

/// One paddock: a boundary and the name a farmer calls it by.
struct Paddock {
  std::string name;
  Polygon boundary;

  [[nodiscard]] double area_hectares() const { return boundary.area_hectares(); }
};

/// The port every source of ground elevation implements.
class ElevationSource {
 public:
  ElevationSource() = default;
  ElevationSource(const ElevationSource&) = default;
  ElevationSource& operator=(const ElevationSource&) = default;
  ElevationSource(ElevationSource&&) = default;
  ElevationSource& operator=(ElevationSource&&) = default;
  virtual ~ElevationSource() = default;

  [[nodiscard]] virtual SourceDescription describe() const = 0;
  [[nodiscard]] virtual ConnectionStatus test_connection() const = 0;

  /// Elevation in metres over `area`, sampled on a `cell_size_m` grid.
  ///
  /// `area` is in NZTM2000 metres, like everything else inside the model.
  /// Throws std::out_of_range when the source cannot cover the whole area:
  /// a raster that quietly stops short would read downstream as flat ground,
  /// which is a slope of zero and a growth modifier of one - wrong, and
  /// invisible.
  [[nodiscard]] virtual Raster<double> fetch(const BoundingBox& area, double cell_size_m) const = 0;
};

/// The port every source of farm boundaries implements.
class ParcelSource {
 public:
  ParcelSource() = default;
  ParcelSource(const ParcelSource&) = default;
  ParcelSource& operator=(const ParcelSource&) = default;
  ParcelSource(ParcelSource&&) = default;
  ParcelSource& operator=(ParcelSource&&) = default;
  virtual ~ParcelSource() = default;

  [[nodiscard]] virtual SourceDescription describe() const = 0;
  [[nodiscard]] virtual ConnectionStatus test_connection() const = 0;

  /// Every paddock intersecting `area`, in NZTM2000 metres.
  [[nodiscard]] virtual std::vector<Paddock> fetch(const BoundingBox& area) const = 0;
};

}  // namespace paddock::core
