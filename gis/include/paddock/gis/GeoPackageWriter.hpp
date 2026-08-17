// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

#include <paddock/core/Terrain.hpp>

namespace paddock::gis {

/// Writes paddock boundaries to a GeoPackage in NZTM2000, one polygon feature
/// each with a `name` field.
///
/// The export half of CLAUDE.md's "vector GeoPackage (never shapefile)", and
/// what lets the reader be tested without a binary fixture in the repository.
///
/// Overwrites `path` if it exists. Throws std::runtime_error naming the path
/// and GDAL's reason if the file cannot be written, including when the
/// coordinate reference system cannot be described - a GeoPackage without one
/// opens as an unplaced shape.
void write_geopackage(const std::vector<core::Paddock>& paddocks, const std::string& path,
                      const std::string& layer_name = "paddocks");

}  // namespace paddock::gis
