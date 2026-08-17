// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>

#include <paddock/core/DataSource.hpp>
#include <paddock/core/Geometry.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/Terrain.hpp>

namespace paddock::gis {

/// Elevation read from a local GeoTIFF, typically a LINZ DEM snapshot.
///
/// The file adapter of the three CLAUDE.md asks for. It holds no credential and
/// makes no network call: fetching is a separate step (scripts/linz-snapshot.py)
/// that writes the file and records its hash, so a simulation reads only what is
/// already on disk. See docs/adr/0012-linz-sources.md.
///
/// GDAL types stop here. The constructor takes a path and the class hands back
/// core's Raster<double>, so nothing downstream needs a GDAL include path.
class GeoTiffElevationSource : public core::ElevationSource {
 public:
  /// Does not open the file. Construction is cheap and always succeeds so that
  /// `test_connection()` can be the thing that reports a missing or unreadable
  /// file, in the words a person can act on.
  explicit GeoTiffElevationSource(std::string path);

  [[nodiscard]] core::SourceDescription describe() const override;
  [[nodiscard]] core::ConnectionStatus test_connection() const override;

  /// Elevation over `area`, resampled to `cell_size_m`.
  ///
  /// Throws std::out_of_range when the file does not cover the whole area, and
  /// std::runtime_error when it cannot be read at all. A DEM that quietly
  /// returns only its overlapping part would read downstream as flat ground
  /// outside it - a slope of zero and a growth modifier of one, wrong and
  /// invisible.
  [[nodiscard]] core::Raster<double> fetch(const core::BoundingBox& area,
                                           double cell_size_m) const override;

  /// The extent the file covers, in NZTM2000 metres. Empty if it cannot be
  /// read.
  [[nodiscard]] core::BoundingBox coverage() const;

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

 private:
  std::string path_;
};

}  // namespace paddock::gis
