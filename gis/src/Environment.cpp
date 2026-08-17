// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <gdal.h>
#include <gdal_version.h>
#include <proj.h>
#include <string>
#include <vector>

#include <paddock/gis/Environment.hpp>

namespace paddock::gis {

namespace {

/// GDAL_RELEASE_NAME is a string literal in gdal_version.h; GDALVersionInfo
/// asks the loaded library. They are the compile-time and run-time answers to
/// the same question.
constexpr const char* kCompiledGdalVersion = GDAL_RELEASE_NAME;

}  // namespace

LibraryVersions library_versions() {
  LibraryVersions versions;
  versions.gdal_compiled = kCompiledGdalVersion;

  const char* runtime = GDALVersionInfo("RELEASE_NAME");
  versions.gdal_runtime = runtime != nullptr ? runtime : "unknown";

  const PJ_INFO info = proj_info();
  versions.proj_runtime = info.version != nullptr ? info.version : "unknown";
  versions.proj_compiled_major = PROJ_VERSION_MAJOR;
  versions.proj_compiled_minor = PROJ_VERSION_MINOR;
  versions.proj_compiled_patch = PROJ_VERSION_PATCH;

  return versions;
}

bool nztm_definition_available() {
  // proj_create returns null when the database cannot be read or the code is
  // not in it. Either way the answer to "can this machine do NZTM" is no.
  PJ* nztm = proj_create(PJ_DEFAULT_CTX, "EPSG:2193");
  if (nztm == nullptr) {
    return false;
  }
  proj_destroy(nztm);
  return true;
}

bool gdal_driver_available(const std::string& name) {
  // Registration is idempotent and cheap after the first call, so doing it here
  // means a caller cannot forget it and get a false negative.
  GDALAllRegister();
  return GDALGetDriverByName(name.c_str()) != nullptr;
}

std::vector<std::string> required_gdal_drivers() {
  // GeoTIFF for rasters and GeoPackage for vectors, as CLAUDE.md specifies.
  // Shapefile is deliberately absent: it is excluded by the same line, and
  // asserting a driver this project must not use would be worse than useless.
  return {"GTiff", "GPKG"};
}

std::string projection_database_search_path() {
  const PJ_INFO info = proj_info();
  return info.searchpath != nullptr ? info.searchpath : "";
}

}  // namespace paddock::gis
