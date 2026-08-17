// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// Terrain changes what the farm does, and it must not change what the farm can
// account for.
//
// Two things are being asserted here and they pull in opposite directions. The
// first is that slope and aspect actually reach the model - a wiring that
// silently did nothing would pass every conservation test ever written. The
// second is that once they do, the budgets still close to 1e-9, because a
// process that moves water or dry matter without recording it is exactly what
// the conservation suite exists to catch.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/FarmletGrid.hpp>
#include <paddock/core/SyntheticTerrain.hpp>
#include <paddock/core/SyntheticWeather.hpp>
#include <paddock/core/Topography.hpp>

#include "support/TestPasture.hpp"
#include "support/TestWeather.hpp"

namespace paddock::core {
namespace {

using test_support::test_site_parameters;
using test_support::test_soil_parameters;
using test_support::test_sward_parameters;

constexpr double kCanterburyLatitude = -43.6;
constexpr double kWest = 1570000.0;
constexpr double kSouth = 5179000.0;
constexpr std::uint64_t kSeed = 20240701;

BoundingBox farm_area() {
  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{kWest, kSouth});
  area.expand_to_include(Point2D{kWest + 300.0, kSouth + 200.0});
  return area;
}

Topography sloping_ground(double rise_east, double rise_north) {
  SyntheticSurface surface;
  surface.reference_easting = kWest;
  surface.reference_northing = kSouth;
  surface.gradient_east = rise_east;
  surface.gradient_north = rise_north;
  surface.undulation_amplitude_m = 0.0;
  return topography_of(SyntheticElevationSource(surface).fetch(farm_area(), 25.0));
}

FarmletInitialState initial_state() {
  FarmletInitialState state;
  state.soil_water_mm = 90.0;
  state.grass_kg_dm_per_ha = 1800.0;
  state.legume_kg_dm_per_ha = 400.0;
  state.soil_mineral_nitrogen_kg_per_ha = 60.0;
  return state;
}

FarmletGrid make_grid() {
  const Raster<double> shape = SyntheticElevationSource().fetch(farm_area(), 25.0);
  const Raster<SoilWaterParameters> soils(shape.cols(), shape.rows(), shape.transform(),
                                          test_soil_parameters());
  return FarmletGrid(soils, test_sward_parameters(), initial_state(), kCanterburyLatitude);
}

/// Steps a year and returns the mean pasture cover it ends on.
double run_year(FarmletGrid& grid, BudgetLedger* ledger) {
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);
  for (const DailyWeather& day : weather.fetch(DateRange::calendar_year(2023)).records) {
    grid.step(day, ledger);
  }
  return grid.mean_cover_kg_dm();
}

// The wiring check. A north-facing hillside and a south-facing one, same soil,
// same weather, same seed - and they must not end the year in the same place.
// If they do, slope and aspect are not reaching the model at all, and every
// other assertion about them is vacuous.
TEST(TerrainBalanceTest, TwoSidesOfAHillDoNotGrowTheSame) {
  FarmletGrid level = make_grid();
  FarmletGrid northerly = make_grid();
  FarmletGrid southerly = make_grid();

  northerly.set_terrain(sloping_ground(0.0, -0.36));  // about 20 degrees, facing north
  southerly.set_terrain(sloping_ground(0.0, 0.36));   // the same hill, other side

  const double level_cover = run_year(level, nullptr);
  const double northerly_cover = run_year(northerly, nullptr);
  const double southerly_cover = run_year(southerly, nullptr);

  EXPECT_NE(northerly_cover, level_cover) << "terrain made no difference at all";
  EXPECT_NE(southerly_cover, level_cover) << "terrain made no difference at all";
  EXPECT_NE(northerly_cover, southerly_cover)
      << "both sides of the hill grew identically, so aspect is not reaching growth";
}

// A grid told nothing about terrain must behave exactly as it did before
// terrain existed - not nearly, exactly. Every scenario written against the flat
// model keeps its results, and any drift here would be a silent rewrite of them.
TEST(TerrainBalanceTest, WithoutTerrainNothingChanges) {
  FarmletGrid before = make_grid();
  FarmletGrid after = make_grid();
  after.set_terrain(sloping_ground(0.0, 0.0));  // level ground, which is a ratio of one

  EXPECT_FALSE(before.has_terrain());
  EXPECT_TRUE(after.has_terrain());
  EXPECT_DOUBLE_EQ(run_year(before, nullptr), run_year(after, nullptr));
}

// The one that matters. Slope and aspect move water and light around, and the
// budgets still have to close: 365 days, three budgets, 1e-9.
TEST(TerrainBalanceTest, BudgetsCloseOnSlopingGround) {
  struct Case {
    const char* description;
    double rise_east;
    double rise_north;
  };

  const Case cases[] = {
      {"facing north", 0.0, -0.36},
      {"facing south", 0.0, 0.36},
      {"facing east", -0.36, 0.0},
      {"steep, facing north-west", 0.5, -0.5},
  };

  for (const Case& scenario : cases) {
    FarmletGrid grid = make_grid();
    grid.set_terrain(sloping_ground(scenario.rise_east, scenario.rise_north));

    BudgetLedger ledger;
    grid.set_opening_stocks(ledger);
    run_year(grid, &ledger);

    EXPECT_TRUE(ledger.closes(Budget::Water, grid.mean_soil_water_mm()))
        << scenario.description << '\n'
        << ledger.report(Budget::Water, grid.mean_soil_water_mm());
    EXPECT_TRUE(ledger.closes(Budget::DryMatter, grid.mean_cover_kg_dm()))
        << scenario.description << '\n'
        << ledger.report(Budget::DryMatter, grid.mean_cover_kg_dm());
  }
}

}  // namespace
}  // namespace paddock::core
