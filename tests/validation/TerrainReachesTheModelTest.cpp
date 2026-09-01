// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Terrain, connected to a run at last.
//
// `Farm::set_slopes` had no callers anywhere in this repository - not one, not
// even a test - and `FarmletGrid::set_terrain` had callers only in tests. So
// two sourced pieces of the model had never fired in a single scenario the
// project shipped: what it costs an animal to walk a slope (TMC Eq. 23), and
// what radiation a slope actually receives (Gillingham et al.). Both were
// implemented, both were tested in isolation, and neither was reachable from a
// bundle. Every farm ran flat and no report said so.
//
// TopographyTest already checks Horn's method against known derivatives and
// SlopeRadiationTest checks the radiation ratio. What none of them could check
// is the wiring, which is what this file is for: that a bundle asking for a
// hill gets one, and that the hill changes the answer.
//
// The surface is INVENTED. What is asserted here is therefore direction and
// reachability, never magnitude: a north face grows more than a south face, and
// a slope costs more to walk than a terrace. Both are consequences the sources
// state; neither depends on the hill being a real hill.

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <paddock/config/ScenarioRun.hpp>

#include "../support/ShippedBundle.hpp"

namespace paddock::config {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string bundle_path() {
  return std::string(PADDOCK_DATA_DIR) + "/scenarios/canterbury-grazed";
}

core::DietQuality pasture_diet() {
  core::DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

core::ManagementPolicy policy() {
  core::ManagementPolicy chosen;
  chosen.minimum_cover_kg_dm_per_ha = 1600.0;
  chosen.target_liveweight_gain_kg_per_day = 0.0;
  chosen.maximum_graze_days = 3;
  chosen.minimum_spell_days = 35;
  chosen.rotation_cover_threshold_kg_dm_per_ha = 2200.0;
  chosen.supplement_me_mj_per_kg_dm = 10.0;
  chosen.may_buy_feed = true;
  return chosen;
}

/// The shipped bundle with a slope put under it. Mutating the loaded bundle
/// rather than adding a sloped scenario keeps every other input identical, so a
/// difference between two runs is the ground and nothing else.
ScenarioBundle on_a_slope(double gradient_east, double gradient_north) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.terrain.kind = TerrainSpec::Kind::Synthetic;
  bundle.terrain.surface.gradient_east = gradient_east;
  bundle.terrain.surface.gradient_north = gradient_north;
  bundle.terrain.surface.undulation_amplitude_m = 0.0;
  return bundle;
}

// The bundles that ship are flat, and that has to stay true: every baseline in
// the project was recorded on level ground.
// The rest of this suite runs the shipped bundle with its ground taken away -
// see tests/support/ShippedBundle.hpp for why - so this checks the taking away
// works, and that flat means flat all the way down.
TEST(TerrainReachesTheModelTest, GroundTakenAwayIsFlatAllTheWayDown) {
  const ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());

  EXPECT_TRUE(bundle.terrain.is_flat());
  EXPECT_FALSE(bundle.make_elevation().has_value())
      << "flat ground has no surface to sample, and generating one would cost a scan of the farm "
         "to learn that every slope is zero";
  EXPECT_FALSE(bundle.make_topography().has_value());

  // And that the bundle on disk is not flat, so this is a choice the test made
  // rather than a description of what was shipped. Without this the helper
  // could stop working and nothing would notice.
  EXPECT_FALSE(load_scenario(bundle_path()).terrain.is_flat());
}

// A bundle that asks for a hill gets one, over the grid it actually runs on.
// Two extents would be two farms.
TEST(TerrainReachesTheModelTest, ASlopedBundleSamplesItsSurfaceOverTheGrid) {
  const ScenarioBundle bundle = on_a_slope(0.0, -0.10);
  const std::optional<core::Raster<double>> elevation = bundle.make_elevation();
  const std::optional<core::Topography> ground = bundle.make_topography();

  // Written as plain if-and-FAIL rather than ASSERT_TRUE because the assertion
  // has to be visible to more than a person: clang-tidy's optional analysis
  // cannot see through a gtest macro to know the value is engaged, and value()
  // is no better - it throws, which the check counts as unchecked access too.
  if (!bundle.grid.has_value() || !elevation.has_value() || !ground.has_value()) {
    FAIL() << "a bundle asking for a slope came back without a grid, an elevation or a slope";
  }
  const GridSpec& spec = *bundle.grid;

  EXPECT_EQ(elevation->cols(), spec.cols);
  EXPECT_EQ(elevation->rows(), spec.rows);
  EXPECT_DOUBLE_EQ(elevation->transform().cell_size, spec.cell_size_m);

  // Verification, not a pin: a 10% grade is atan(0.10), which is 5.71 degrees.
  const double middle = ground->slope_degrees(spec.cols / 2, spec.rows / 2);
  EXPECT_NEAR(middle, std::atan(0.10) * 180.0 / kPi, 0.01);
}

// The reason it matters. In the southern hemisphere the sun sits to the north,
// so a north-facing slope intercepts more of it than the south face of the same
// hill, and grows more. That ordering is Gillingham's, and asserting it here
// proves the terrain reached the growth model rather than stopping at the
// bundle.
TEST(TerrainReachesTheModelTest, ANorthFaceOutgrowsTheSouthFaceOfTheSameHill) {
  const RunSummary north =
      run_managed_scenario(on_a_slope(0.0, -0.20), policy(), pasture_diet(), "north facing");
  const RunSummary south =
      run_managed_scenario(on_a_slope(0.0, 0.20), policy(), pasture_diet(), "south facing");

  EXPECT_GT(north.mean_cover_kg_dm_per_ha(), south.mean_cover_kg_dm_per_ha())
      << "north " << north.mean_cover_kg_dm_per_ha() << ", south "
      << south.mean_cover_kg_dm_per_ha() << " kg DM/ha";

  GTEST_LOG_(INFO) << "mean cover: north face " << north.mean_cover_kg_dm_per_ha()
                   << ", south face " << south.mean_cover_kg_dm_per_ha() << " kg DM/ha";
}

// And that the animals are on the same hill as the grass. Walking a slope costs
// energy the model now charges for, so the same farm on a slope feeds its stock
// less well than the terrace it used to be - through the feed bill, because the
// farmer is protecting the stock and buys the difference.
TEST(TerrainReachesTheModelTest, TheSameFarmOnASlopeIsNotTheTerraceItUsedToBe) {
  const RunSummary flat = run_managed_scenario(tests::load_on_flat_ground(bundle_path()), policy(),
                                               pasture_diet(), "flat");
  const RunSummary steep =
      run_managed_scenario(on_a_slope(0.0, 0.20), policy(), pasture_diet(), "south facing slope");

  // Something has to have changed. Which way is the next assertion's business;
  // this one only refuses the outcome that says the wiring is not there.
  const bool cover_moved =
      std::abs(steep.mean_cover_kg_dm_per_ha() - flat.mean_cover_kg_dm_per_ha()) > 1.0;
  const bool feed_moved = std::abs(steep.bought_feed_kg_dm() - flat.bought_feed_kg_dm()) > 1.0;
  EXPECT_TRUE(cover_moved || feed_moved)
      << "a run over a hill came out identical to a run over a terrace, which means the terrain "
         "did not reach the model";

  // **The direction: a shaded slope grows less.**
  //
  // This used to be measured in bought feed - a shaded farm needing more of it
  // to hold the same stock - and that stopped separating the two farms when the
  // cover floor became seasonal and the farm stopped buying feed at all. It was
  // never the direct measure anyway: bought feed is a management response, and
  // what the terrain changes is the grass. So the grass is what this reads.
  const auto grown = [](const RunSummary& run) {
    for (const core::ProcessEntry& entry : run.ledger.entries(core::Budget::DryMatter)) {
      if (entry.process == "pasture_growth") {
        return entry.inflow;
      }
    }
    return 0.0;
  };

  EXPECT_LT(grown(steep), grown(flat)) << "a south face should grow less than level ground: flat "
                                       << grown(flat) << ", slope " << grown(steep);

  GTEST_LOG_(INFO) << "growth: flat " << grown(flat) << " kg DM/ha, south slope " << grown(steep)
                   << " kg DM/ha";
}

// A bundle may take its ground from a file instead of a formula.
//
// config records the reference and never opens it: reading a GeoTIFF needs
// GDAL, GDAL lives in gis/, and config must not depend on it. So the manifest
// names the file and whoever can read it supplies the source - the same shape
// as `weather`, and the same shape `[boundary] kind = "geopackage"` already
// uses for a cadastre.
TEST(TerrainReachesTheModelTest, ASnapshotBundleRecordsTheFileWithoutOpeningIt) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.terrain.kind = TerrainSpec::Kind::Snapshot;
  bundle.terrain.elevation_path = "../../snapshots/lincoln-dem-1m.tiff";
  bundle.terrain.elevation_sha256 = "not checked here";

  EXPECT_FALSE(bundle.terrain.is_flat());
  EXPECT_EQ(bundle.elevation, nullptr) << "loading a bundle must not open its elevation file";
}

// And a bundle that names a file nobody can read must say so rather than run.
//
// The failure this refuses is the quiet one: a farm that asked for hills,
// found no reader, and went round the year as a terrace with every report
// saying the ground was a snapshot.
TEST(TerrainReachesTheModelTest, ASnapshotWithNoReaderRefusesToRunFlat) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.terrain.kind = TerrainSpec::Kind::Snapshot;
  bundle.terrain.elevation_path = "../../snapshots/lincoln-dem-1m.tiff";
  bundle.terrain.elevation_sha256 = "not checked here";

  EXPECT_THROW(static_cast<void>(bundle.make_elevation()), std::runtime_error);
}

// The seam itself, exercised without GDAL. A synthetic source stands in for the
// GeoTIFF reader: what is checked is that a supplied source is asked for the
// grid's own extent and resolution, which is the part config is responsible for
// and the part that would put a farm on the wrong ground if it were wrong.
TEST(TerrainReachesTheModelTest, ASuppliedSourceIsSampledOverTheGrid) {
  ScenarioBundle bundle = tests::load_on_flat_ground(bundle_path());
  bundle.terrain.kind = TerrainSpec::Kind::Snapshot;
  bundle.terrain.elevation_path = "stand-in";
  bundle.terrain.elevation_sha256 = "stand-in";

  core::SyntheticSurface surface;
  surface.gradient_east = 0.0;
  surface.gradient_north = -0.10;
  surface.undulation_amplitude_m = 0.0;
  bundle.elevation = std::make_shared<core::SyntheticElevationSource>(surface);

  const std::optional<core::Raster<double>> ground = bundle.make_elevation();
  if (!bundle.grid.has_value() || !ground.has_value()) {
    FAIL() << "a bundle with a supplied elevation source returned no ground";
  }
  const GridSpec& spec = *bundle.grid;
  EXPECT_EQ(ground->cols(), spec.cols);
  EXPECT_EQ(ground->rows(), spec.rows);
  EXPECT_DOUBLE_EQ(ground->transform().cell_size, spec.cell_size_m);
  EXPECT_DOUBLE_EQ(ground->transform().origin_easting, spec.origin_easting);
}

// Conservation does not care what shape the ground is. If a slope could make
// dry matter appear, the terrain wiring would have broken the one gate this
// project rests on.
TEST(TerrainReachesTheModelTest, TheBudgetsStillCloseOverAHill) {
  const RunSummary run =
      run_managed_scenario(on_a_slope(0.05, -0.15), policy(), pasture_diet(), "sidling");
  constexpr double kTolerance = 1e-9;

  EXPECT_TRUE(run.ledger.closes(core::Budget::DryMatter, run.closing_cover_kg_dm, kTolerance))
      << run.ledger.report(core::Budget::DryMatter, run.closing_cover_kg_dm);
  EXPECT_TRUE(run.ledger.closes(core::Budget::Nitrogen, run.closing_nitrogen_kg, kTolerance));
  EXPECT_TRUE(run.ledger.closes(core::Budget::Water, run.closing_water_mm, kTolerance));
}

}  // namespace
}  // namespace paddock::config
