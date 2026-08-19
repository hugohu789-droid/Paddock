// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// What the application does when the ground a scenario names is not there.
//
// This is the one piece of wiring that has to distinguish two failures that
// look alike from a distance. A snapshot nobody has fetched is the ordinary
// state of a fresh clone, and the farm should still open - flat, and saying so.
// A snapshot that is present and hashes to something else is not this
// scenario's ground at all, and no message makes that safe to draw.
//
// The distinction was made by hand once, by renaming the file and looking at
// the window. That is not a check anybody will repeat.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <paddock/config/ScenarioConfig.hpp>

#include "../../app/src/AttachElevation.hpp"

namespace {

using paddock::app::attach_elevation;
using paddock::config::ScenarioBundle;
using paddock::config::TerrainSpec;

/// A bundle that takes its ground from a file, over a grid small enough to
/// sample quickly. The coordinates are Lincoln's, so that a raster this does
/// produce lands somewhere real rather than off the projection.
ScenarioBundle bundle_wanting_ground(const std::string& path) {
  ScenarioBundle bundle;
  bundle.name = "test_farm";
  bundle.terrain.kind = TerrainSpec::Kind::Snapshot;
  bundle.terrain.elevation_path = path;
  bundle.terrain.elevation_sha256 =
      "0000000000000000000000000000000000000000000000000000000000000000";

  paddock::config::GridSpec grid;
  grid.cols = 4;
  grid.rows = 4;
  grid.cell_size_m = 25.0;
  grid.origin_easting = 1546000.0;
  grid.origin_northing = 5171000.0;
  bundle.grid = grid;
  return bundle;
}

/// A directory of this test's own, so that a run does not depend on what is in
/// data/snapshots - which is gitignored, and so differs between machines.
std::filesystem::path scratch_directory(const std::string& name) {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / ("paddock_attach_" + name);
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory;
}

TEST(AttachElevation, MissingFileDrawsFlatAndSaysSo) {
  const std::filesystem::path directory = scratch_directory("missing");
  ScenarioBundle bundle = bundle_wanting_ground("ground.tiff");

  const std::string reason = attach_elevation(bundle, directory.string());

  EXPECT_FALSE(reason.empty()) << "a farm running flat without saying so is the failure this "
                                  "whole arrangement exists to prevent";
  EXPECT_NE(reason.find("not on this machine"), std::string::npos);
  EXPECT_NE(reason.find("nz-elevation-snapshot.py"), std::string::npos)
      << "the message has to say what to do about it";

  // Demoted, so that the bundle's own refusal - Snapshot with no reader behind
  // it - does not fire on the way to the map.
  EXPECT_TRUE(bundle.terrain.is_flat());
  EXPECT_NO_THROW({
    const auto ground = bundle.make_elevation();
    EXPECT_FALSE(ground.has_value());
  });

  std::filesystem::remove_all(directory);
}

TEST(AttachElevation, PresentButDifferentStillRefuses) {
  const std::filesystem::path directory = scratch_directory("mismatch");
  {
    std::ofstream file(directory / "ground.tiff", std::ios::binary);
    file << "not the elevation this bundle recorded";
  }
  ScenarioBundle bundle = bundle_wanting_ground("ground.tiff");

  // Absent is "fetch it"; different is "that is not this farm's ground". The
  // second one has no flat fallback, because there is no way to tell the
  // difference between a file somebody replaced and a file somebody corrupted.
  EXPECT_THROW((void)attach_elevation(bundle, directory.string()), std::runtime_error);
  EXPECT_FALSE(bundle.terrain.is_flat()) << "a refused bundle must not be quietly flattened";

  std::filesystem::remove_all(directory);
}

TEST(AttachElevation, FlatScenarioIsNotAComplaint) {
  ScenarioBundle bundle;
  bundle.name = "flat_farm";

  // A farm that never asked for ground is not missing any. The message belongs
  // to scenarios whose manifest says one thing and whose disk says another.
  EXPECT_TRUE(attach_elevation(bundle, ".").empty());
}

}  // namespace
