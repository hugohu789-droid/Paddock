// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#include <paddock/config/ScenarioConfig.hpp>
#include <paddock/core/Sha256.hpp>

#ifdef PADDOCK_WITH_GIS
#include <paddock/gis/GeoTiffElevation.hpp>
#endif

namespace paddock::app {

/// Gives a bundle a reader for the elevation file its manifest names, having
/// first checked that it is the file the manifest meant.
///
/// This is the wiring the layers are arranged to need. config records that a
/// scenario takes its ground from a file and never opens it, because opening a
/// GeoTIFF needs GDAL and config must not depend on gis. gis knows how to read
/// one and nothing about scenarios. The application is where the two meet, and
/// this is the whole of it.
///
/// **The hash is checked here rather than at load.** Every other input a bundle
/// names is hashed while the manifest is read, but an elevation snapshot is
/// tens of megabytes and is legitimately absent - snapshots are not committed -
/// so hashing it on every load would charge every command for a file most of
/// them never touch, and would turn "not fetched yet" into a parse error. This
/// is the moment the file is actually going to be used, and a hash that is
/// recorded and never checked is decoration.
///
/// Without the geospatial stack this does nothing, and the bundle throws the
/// first time it is asked for ground. That is the honest outcome: a farm
/// quietly running flat when its manifest says otherwise is the failure worth
/// preventing, and a build that cannot read a DEM should say so rather than
/// substitute a terrace for a hill.
inline void attach_elevation(config::ScenarioBundle& bundle, const std::string& bundle_directory) {
#ifdef PADDOCK_WITH_GIS
  if (bundle.terrain.kind != config::TerrainSpec::Kind::Snapshot) {
    return;
  }
  // The manifest's path is relative to the bundle, as every other input's is.
  const std::filesystem::path resolved =
      (std::filesystem::path(bundle_directory) / bundle.terrain.elevation_path).lexically_normal();

  std::ifstream file(resolved, std::ios::binary);
  if (!file) {
    throw std::runtime_error("scenario '" + bundle.name + "' takes its ground from " +
                             resolved.string() +
                             ", which is not there. Snapshots are not committed - fetch it with "
                             "scripts/nz-elevation-snapshot.py.");
  }
  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  if (const std::string actual = core::Sha256::hex_of(contents);
      actual != bundle.terrain.elevation_sha256) {
    throw std::runtime_error("scenario '" + bundle.name + "' expects the ground in " +
                             resolved.string() + " to hash to " + bundle.terrain.elevation_sha256 +
                             ", and it hashes to " + actual +
                             ". This is not the elevation the scenario was written against.");
  }

  bundle.elevation = std::make_shared<gis::GeoTiffElevationSource>(resolved.string());
#else
  (void)bundle;
  (void)bundle_directory;
#endif
}

}  // namespace paddock::app
