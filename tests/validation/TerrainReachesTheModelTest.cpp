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
#include <string>

#include <paddock/config/ScenarioRun.hpp>

namespace paddock::config {
namespace {

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
  ScenarioBundle bundle = load_scenario(bundle_path());
  bundle.terrain.kind = TerrainSpec::Kind::Synthetic;
  bundle.terrain.surface.gradient_east = gradient_east;
  bundle.terrain.surface.gradient_north = gradient_north;
  bundle.terrain.surface.undulation_amplitude_m = 0.0;
  return bundle;
}

// The bundles that ship are flat, and that has to stay true: every baseline in
// the project was recorded on level ground.
TEST(TerrainReachesTheModelTest, TheShippedBundlesAreStillFlat) {
  const ScenarioBundle bundle = load_scenario(bundle_path());

  EXPECT_TRUE(bundle.terrain.is_flat());
  EXPECT_FALSE(bundle.make_elevation().has_value())
      << "flat ground has no surface to sample, and generating one would cost a scan of the farm "
         "to learn that every slope is zero";
  EXPECT_FALSE(bundle.make_topography().has_value());
}

// A bundle that asks for a hill gets one, over the grid it actually runs on.
// Two extents would be two farms.
TEST(TerrainReachesTheModelTest, ASlopedBundleSamplesItsSurfaceOverTheGrid) {
  const ScenarioBundle bundle = on_a_slope(0.0, -0.10);
  ASSERT_TRUE(bundle.grid.has_value());

  const auto elevation = bundle.make_elevation();
  ASSERT_TRUE(elevation.has_value());
  EXPECT_EQ(elevation->cols(), bundle.grid->cols);
  EXPECT_EQ(elevation->rows(), bundle.grid->rows);
  EXPECT_DOUBLE_EQ(elevation->transform().cell_size, bundle.grid->cell_size_m);

  const auto ground = bundle.make_topography();
  ASSERT_TRUE(ground.has_value());
  // Verification, not a pin: a 10% grade is atan(0.10), which is 5.71 degrees.
  const double middle = ground->slope_degrees(bundle.grid->cols / 2, bundle.grid->rows / 2);
  EXPECT_NEAR(middle, std::atan(0.10) * 180.0 / 3.14159265358979323846, 0.01);
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
  const RunSummary flat =
      run_managed_scenario(load_scenario(bundle_path()), policy(), pasture_diet(), "flat");
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

  // The direction: a shaded slope grows less, so the farm needs more bought
  // feed to hold the same stock at the same weight.
  EXPECT_GT(steep.bought_feed_kg_dm(), flat.bought_feed_kg_dm())
      << "flat " << flat.bought_feed_kg_dm() << " kg DM, slope " << steep.bought_feed_kg_dm();

  GTEST_LOG_(INFO) << "bought feed: flat " << flat.bought_feed_kg_dm() << " kg DM, south slope "
                   << steep.bought_feed_kg_dm() << " kg DM";
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
