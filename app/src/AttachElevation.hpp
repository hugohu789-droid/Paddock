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
/// **Absent and different are not the same failure.** A snapshot nobody has
/// fetched is the ordinary state of a fresh clone, and refusing to run over it
/// would make the elevation an installation step rather than an input. So a
/// missing file returns a reason and leaves the bundle without ground, for the
/// caller to say out loud and carry on flat. A file that is present and hashes
/// to something else is a different matter - that is not this scenario's
/// ground - and it still throws.
///
/// A farm quietly running flat when its manifest says otherwise is still the
/// failure worth preventing. Quietly is the word doing the work: the reason
/// goes on the line under the map, into the report's Ground row, and to stdout
/// on the headless path - where a screenshot of flat ground would otherwise
/// look the same either way.
///
/// Returns empty when the bundle has its elevation, or names none. Otherwise
/// returns why it has not, in words meant to be shown to somebody.
[[nodiscard]] inline std::string attach_elevation(config::ScenarioBundle& bundle,
                                                  const std::string& bundle_directory) {
#ifdef PADDOCK_WITH_GIS
  if (bundle.terrain.kind != config::TerrainSpec::Kind::Snapshot) {
    return {};
  }
  // The manifest's path is relative to the bundle, as every other input's is.
  const std::filesystem::path resolved =
      (std::filesystem::path(bundle_directory) / bundle.terrain.elevation_path).lexically_normal();

  std::ifstream file(resolved, std::ios::binary);
  if (!file) {
    // Demoted, not merely left unattached. ScenarioBundle::make_elevation
    // refuses a Snapshot with no reader behind it - rightly, because that is
    // how a farm would run flat without saying so - and this is the one place
    // that knows the file is absent rather than unreadable, and has already
    // written the sentence that says so out loud.
    bundle.terrain.kind = config::TerrainSpec::Kind::Flat;
    // **Names the easy route, not the one that needs Python.** This used to
    // send people to scripts/nz-elevation-snapshot.py, which walks a catalogue
    // looking for the tile that covers a point - work this scenario has already
    // had done for it, since its manifest pins the tile's address. See ADR 0015.
    return "This farm has measured ground and the file is not on this machine, so it is being "
           "drawn flat. Fetch it with 'paddock ground fetch', or the Fetch ground button.";
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
  return {};
#else
  (void)bundle_directory;
  if (bundle.terrain.kind != config::TerrainSpec::Kind::Snapshot) {
    return {};
  }
  bundle.terrain.kind = config::TerrainSpec::Kind::Flat;
  return "This farm has measured ground and this build has no geospatial stack to read it with, "
         "so it is being drawn flat. Build with the desktop preset - see docs/setup.md.";
#endif
}

}  // namespace paddock::app
