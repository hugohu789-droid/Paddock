// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <string>
#include <vector>

/// What the geospatial stack actually is on the machine this build is running
/// on, as opposed to what the build system was told at configure time.
///
/// Nothing here exposes a GDAL or PROJ type. That is the point: gis/ links
/// them PRIVATE, so a caller in app/ or tests/ needs no GDAL headers and no
/// GDAL include path, and the rule that "GDAL appears only inside gis/"
/// (CLAUDE.md) is enforced by the build graph rather than by discipline.
namespace paddock::gis {

/// Versions as reported by the libraries themselves at run time, next to the
/// versions their headers claimed at compile time.
///
/// The two disagree more often than they should. A machine can compile against
/// one GDAL and load another at run time - different Homebrew generations,
/// a vcpkg build shadowed by a system install, a Qt or VTK bundle carrying its
/// own PROJ - and the symptom is a projection that is subtly wrong rather than
/// a link error. Reporting both makes that visible in one line of CI output.
struct LibraryVersions {
  std::string gdal_runtime;
  std::string gdal_compiled;
  std::string proj_runtime;
  int proj_compiled_major = 0;
  int proj_compiled_minor = 0;
  int proj_compiled_patch = 0;
};

[[nodiscard]] LibraryVersions library_versions();

/// Whether PROJ can resolve EPSG:2193 - New Zealand Transverse Mercator 2000.
///
/// PROJ 6 moved its coordinate operation tables out of the source tree and into
/// a SQLite database, `proj.db`, found at run time through a search path. When
/// that lookup fails - a relocated install, a package that split the data into
/// another package, a Windows build whose PROJ_DATA is not set - PROJ still
/// links and still runs, and every transform fails at the point of use. This is
/// the single most common way a working geospatial build stops working on
/// another machine, so it is checked explicitly rather than discovered later.
///
/// EPSG:2193 specifically, not any CRS, because it is the one this project
/// computes in (CLAUDE.md: "All internal computation in NZTM2000 (EPSG:2193)").
[[nodiscard]] bool nztm_definition_available();

/// The directory PROJ is searching for its database, for an error message that
/// tells someone what to fix.
[[nodiscard]] std::string projection_database_search_path();

/// Whether this GDAL was built with the driver named, for example "GTiff" or
/// "GPKG".
///
/// GDAL's drivers are a build-time choice, and a build without one still links
/// and still runs: the failure arrives when a file is opened, as a null dataset
/// and a message about an unrecognised format. `CLAUDE.md` fixes the formats
/// this project uses - GeoTIFF for rasters, GeoPackage for vectors, never
/// shapefile - so whether they are present is a property of the machine worth
/// asserting once rather than discovering per file.
[[nodiscard]] bool gdal_driver_available(const std::string& name);

/// The drivers this project cannot work without, in the order they are checked.
[[nodiscard]] std::vector<std::string> required_gdal_drivers();

}  // namespace paddock::gis
