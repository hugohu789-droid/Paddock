// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>

#include <paddock/core/Raster.hpp>

namespace paddock::gis {

/// Writes a raster to a north-up GeoTIFF in NZTM2000.
///
/// `CLAUDE.md` asks for GeoTIFF exports that can be opened in QGIS, so this is
/// part of the product rather than a test convenience - though it is also what
/// lets the reader be tested without a binary fixture in the repository.
///
/// Throws std::runtime_error if the file cannot be written, naming the path and
/// GDAL's own reason. Returns nothing: a writer that reports success by a bool
/// invites a caller to ignore it.
void write_geotiff(const core::Raster<double>& raster, const std::string& path);

}  // namespace paddock::gis
