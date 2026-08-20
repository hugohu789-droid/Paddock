// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

#include <paddock/core/FarmletGrid.hpp>
#include <paddock/core/SyntheticWeather.hpp>

#include "support/BitPattern.hpp"
#include "support/TestPasture.hpp"
#include "support/TestWeather.hpp"

namespace paddock::core {
namespace {

using test_support::bit_patterns;
using test_support::test_site_parameters;
using test_support::test_soil_parameters;
using test_support::test_sward_parameters;

constexpr std::uint64_t kSeed = 20240701;
constexpr double kLatitude = -43.5;
constexpr std::size_t kCols = 8;
constexpr std::size_t kRows = 6;

GeoTransform test_transform() {
  GeoTransform transform;
  transform.origin_easting = 1570000.0;
  transform.origin_northing = 5180000.0;
  transform.cell_size = 10.0;
  return transform;
}

FarmletInitialState initial_state() {
  FarmletInitialState state;
  state.soil_water_mm = 90.0;
  state.grass_kg_dm_per_ha = 1800.0;
  state.legume_kg_dm_per_ha = 400.0;
  state.soil_mineral_nitrogen_kg_per_ha = 60.0;
  return state;
}

/// Every cell the same soil.
Raster<SoilWaterParameters> uniform_soils() {
  return {kCols, kRows, test_transform(), test_soil_parameters()};
}

/// Available water rising from west to east: a shallow edge and a deep one, the
/// simplest thing that makes a map worth looking at.
Raster<SoilWaterParameters> graded_soils() {
  Raster<SoilWaterParameters> soils = uniform_soils();
  for (std::size_t row = 0; row < kRows; ++row) {
    for (std::size_t col = 0; col < kCols; ++col) {
      SoilWaterParameters soil = test_soil_parameters();
      soil.total_available_water_mm =
          40.0 + (120.0 * static_cast<double>(col) / static_cast<double>(kCols - 1));
      soils(col, row) = soil;
    }
  }
  return soils;
}

std::vector<double> values_of(const Raster<double>& raster) {
  return raster.values();
}

TEST(FarmletGridTest, TakesItsShapeAndGeoreferencingFromTheSoilRaster) {
  const FarmletGrid grid(uniform_soils(), test_sward_parameters(), initial_state(), kLatitude);

  EXPECT_EQ(grid.cols(), kCols);
  EXPECT_EQ(grid.rows(), kRows);
  EXPECT_EQ(grid.cell_count(), kCols * kRows);
  EXPECT_EQ(grid.transform().epsg, kNztm2000Epsg);
  EXPECT_DOUBLE_EQ(grid.cover_kg_dm().transform().origin_easting, 1570000.0);
  EXPECT_THROW(static_cast<void>(grid.cell(kCols, 0)), std::out_of_range);
}

TEST(FarmletGridTest, AnEmptyRasterIsRejected) {
  const Raster<SoilWaterParameters> nothing;

  EXPECT_THROW(FarmletGrid(nothing, test_sward_parameters(), initial_state(), kLatitude),
               std::invalid_argument);
}

// The property that makes the grid trustworthy: on uniform soil it is the
// single-hectare model, cell for cell, bit for bit. A grid that drifted from
// the farmlet it is made of would make every map a separate model.
TEST(FarmletGridTest, AUniformGridMatchesTheSingleFarmletExactly) {
  FarmletGrid grid(uniform_soils(), test_sward_parameters(), initial_state(), kLatitude);
  Farmlet single(test_soil_parameters(), test_sward_parameters(), initial_state(), kLatitude);
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  for (const DailyWeather& day :
       weather.fetch(DateRange{Date{2023, 7, 1}, Date{2023, 10, 31}}).records) {
    grid.step(day);
    single.step(day);
  }

  const std::vector<double> cover = values_of(grid.cover_kg_dm());
  ASSERT_EQ(cover.size(), kCols * kRows);
  for (const double value : cover) {
    EXPECT_EQ(bit_patterns(std::vector<double>{value}),
              bit_patterns(std::vector<double>{single.sward().cover_kg_dm()}));
  }
  EXPECT_EQ(bit_patterns(std::vector<double>{grid.mean_cover_kg_dm()}),
            bit_patterns(std::vector<double>{single.sward().cover_kg_dm()}));
}

// What the map is for: a shallow soil runs out of water first.
TEST(FarmletGridTest, ShallowSoilDriesFirstAndGrowsLess) {
  FarmletGrid grid(graded_soils(), test_sward_parameters(), initial_state(), kLatitude);
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  // A summer quarter, when the profile is drawn down.
  for (const DailyWeather& day :
       weather.fetch(DateRange{Date{2024, 1, 1}, Date{2024, 3, 31}}).records) {
    grid.step(day);
  }

  const Raster<double> stress = grid.water_stress();
  const Raster<double> cover = grid.cover_kg_dm();
  const std::size_t east = kCols - 1;

  EXPECT_LT(stress(0, 0), stress(east, 0)) << "the shallow western edge should be more stressed";
  EXPECT_LT(cover(0, 0), cover(east, 0)) << "and should therefore carry less cover";
}

// The growth map is what the model said, not a second calculation of it.
//
// A uniform grid must report exactly what one farmlet's own DailyRecord
// returned, bit for bit. Working growth out here from cover differences would
// pass a looser test and still be a second answer that could drift from the
// first - and it would be wrong the moment anything grazes, because cover falls
// for a reason that has nothing to do with growing.
TEST(FarmletGridTest, TheGrowthMapIsTheRecordTheModelReturned) {
  FarmletGrid grid(uniform_soils(), test_sward_parameters(), initial_state(), kLatitude);
  Farmlet single(test_soil_parameters(), test_sward_parameters(), initial_state(), kLatitude);
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  double grew = 0.0;
  for (const DailyWeather& day :
       weather.fetch(DateRange{Date{2023, 9, 1}, Date{2023, 11, 30}}).records) {
    grid.step(day);
    grew = single.step(day).growth_kg_dm;
  }

  // Without this the comparison below passes on two zeroes, which is exactly
  // what a growth map that was never filled in would report.
  ASSERT_GT(grew, 0.0) << "spring should have grown something to compare";

  const std::vector<double> grown = values_of(grid.last_growth_kg_dm());
  ASSERT_EQ(grown.size(), kCols * kRows);
  for (const double value : grown) {
    EXPECT_EQ(bit_patterns(std::vector<double>{value}), bit_patterns(std::vector<double>{grew}));
  }
}

// **Growth is the production side on its own, and that is why it earns a map.**
// The shallow western soil runs short of water and grows less for it. Cover
// shows the same thing here only because nothing is grazing; the moment stock
// are on the farm, cover moves for two reasons at once and this does not.
TEST(FarmletGridTest, TheGrowthMapShowsWhichGroundIsProducing) {
  FarmletGrid grid(graded_soils(), test_sward_parameters(), initial_state(), kLatitude);
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  for (const DailyWeather& day :
       weather.fetch(DateRange{Date{2024, 1, 1}, Date{2024, 3, 31}}).records) {
    grid.step(day);
  }

  const Raster<double> grown = grid.last_growth_kg_dm();
  const std::size_t east = kCols - 1;
  EXPECT_LT(grown(0, 0), grown(east, 0)) << "the shallow western edge should be growing less";
  EXPECT_GE(grown(0, 0), 0.0) << "and growth is never negative - that would be decay";
}

// Water put on today shows up as growth in the days after it, which is the
// chain the irrigation maps exist to make visible. Two identical farms, one
// watered, and the watered one is growing more by the end.
TEST(FarmletGridTest, WateredGroundGrowsMoreThanGroundLeftDry) {
  FarmletGrid dry(uniform_soils(), test_sward_parameters(), initial_state(), kLatitude);
  FarmletGrid watered(uniform_soils(), test_sward_parameters(), initial_state(), kLatitude);
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  const std::vector<double> ten_mm(kCols * kRows, 10.0);
  for (const DailyWeather& day :
       weather.fetch(DateRange{Date{2024, 1, 1}, Date{2024, 2, 29}}).records) {
    dry.step(day);
    watered.step(day, nullptr, ten_mm);
  }

  EXPECT_GT(watered.last_growth_kg_dm()(0, 0), dry.last_growth_kg_dm()(0, 0));
}

TEST(FarmletGridTest, TheBudgetsCloseAcrossTheGrid) {
  FarmletGrid grid(graded_soils(), test_sward_parameters(), initial_state(), kLatitude);
  BudgetLedger ledger;
  grid.set_opening_stocks(ledger);
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);

  for (const DailyWeather& day :
       weather.fetch(DateRange{Date{2023, 7, 1}, Date{2024, 6, 30}}).records) {
    grid.step(day, &ledger);
  }

  EXPECT_TRUE(ledger.closes(Budget::Water, grid.mean_soil_water_mm()))
      << ledger.report(Budget::Water, grid.mean_soil_water_mm());
  EXPECT_TRUE(ledger.closes(Budget::DryMatter, grid.mean_cover_kg_dm()))
      << ledger.report(Budget::DryMatter, grid.mean_cover_kg_dm());
  EXPECT_TRUE(ledger.closes(Budget::Nitrogen, grid.mean_total_nitrogen_kg()))
      << ledger.report(Budget::Nitrogen, grid.mean_total_nitrogen_kg());
}

TEST(FarmletGridTest, SnapshotsAreWithinTheirPhysicalBounds) {
  FarmletGrid grid(graded_soils(), test_sward_parameters(), initial_state(), kLatitude);
  const SyntheticWeatherSource weather(test_site_parameters(), kSeed);
  for (const DailyWeather& day :
       weather.fetch(DateRange{Date{2023, 7, 1}, Date{2023, 12, 31}}).records) {
    grid.step(day);
  }

  for (const double stress : values_of(grid.water_stress())) {
    ASSERT_GE(stress, 0.0);
    ASSERT_LE(stress, 1.0);
  }
  for (const double fraction : values_of(grid.legume_fraction())) {
    ASSERT_GE(fraction, 0.0);
    ASSERT_LE(fraction, 1.0);
  }
  for (const double cover : values_of(grid.cover_kg_dm())) {
    ASSERT_GT(cover, 0.0);
  }
}

}  // namespace
}  // namespace paddock::core
