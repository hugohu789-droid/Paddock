// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/core/DataSource.hpp>
#include <paddock/core/Geometry.hpp>
#include <paddock/core/Terrain.hpp>

namespace paddock::gis {

/// Paddock boundaries read from a local GeoPackage, typically a LINZ parcel
/// snapshot clipped to one farm.
///
/// GeoPackage rather than shapefile, as CLAUDE.md requires: one file instead of
/// six, no 10-character field-name limit, no 2 GB ceiling, and a real coordinate
/// reference system rather than a sidecar .prj that goes missing.
///
/// The file adapter, so it holds no credential and makes no network call - see
/// docs/adr/0012-linz-sources.md. GDAL types stop here; callers get core's
/// Polygon.
class GeoPackageParcelSource : public core::ParcelSource {
 public:
  /// `layer` empty means the file's first layer, which is what a single-layer
  /// export from LINZ produces.
  ///
  /// `name_field` is the attribute a paddock's name comes from. When it is
  /// empty, or the field is missing from the layer, paddocks are numbered in
  /// the order the file lists them - readable, and stable for a given file.
  explicit GeoPackageParcelSource(std::string path, std::string layer = {},
                                  std::string name_field = {});

  [[nodiscard]] core::SourceDescription describe() const override;
  [[nodiscard]] core::ConnectionStatus test_connection() const override;

  /// Every paddock whose boundary intersects `area`.
  ///
  /// Throws std::runtime_error when the file cannot be read. An empty result is
  /// not an error - a farm boundary that misses the area asked for is a
  /// question about the scenario, not about the file - but it is worth
  /// noticing, so test_connection() reports how many features the layer holds.
  [[nodiscard]] std::vector<core::Paddock> fetch(const core::BoundingBox& area) const override;

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

 private:
  std::string path_;
  std::string layer_;
  std::string name_field_;
};

}  // namespace paddock::gis
