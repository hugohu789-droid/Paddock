// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

// The whole farm over a year: ground, paddocks and stock at once.
//
// The conservation gate has until now covered one hectare with nothing eating
// it. Adding animals adds an outflow, and an outflow is exactly where a model
// loses track of things: a kilogram removed from a cell has to be the same
// kilogram the ledger records, on the same per-hectare terms the grid folds
// everything else into, or the budget closes by accident and stops meaning
// anything.
//
// The second thing tested here is that the choice made about shape held. Each
// cell keeps its own sward, so a paddock is a set of cells rather than a unit
// of pasture, and grazing one paddock must leave the others alone.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <paddock/core/Farm.hpp>
#include <paddock/core/SyntheticTerrain.hpp>
#include <paddock/core/SyntheticWeather.hpp>

namespace paddock::core {
namespace {

constexpr double kConservationTolerance = 1e-9;
constexpr double kWest = 1570000.0;
constexpr double kSouth = 5179000.0;
constexpr double kFarmWidth = 400.0;
constexpr double kFarmHeight = 300.0;
constexpr double kCellSize = 25.0;

BoundingBox farm_area() {
  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{kWest, kSouth});
  area.expand_to_include(Point2D{kWest + kFarmWidth, kSouth + kFarmHeight});
  return area;
}

SoilWaterParameters soil() {
  SoilWaterParameters parameters;
  parameters.total_available_water_mm = 120.0;
  parameters.depletion_fraction = 0.6;
  parameters.crop_coefficient = 0.95;
  parameters.runoff_fraction = 0.05;
  return parameters;
}

SwardParameters sward() {
  SwardParameters parameters;
  parameters.par_fraction = 0.5;
  parameters.decomposition_rate_per_day = 0.02;

  parameters.grass.species_id = "ryegrass_perennial";
  parameters.grass.specific_leaf_area_m2_per_kg = 20.0;
  parameters.grass.extinction_coefficient = 0.5;
  parameters.grass.radiation_use_efficiency_g_per_mj = 1.5;
  parameters.grass.base_temperature_c = 4.0;
  parameters.grass.optimum_temperature_c = 20.0;
  parameters.grass.maximum_temperature_c = 35.0;
  parameters.grass.senescence_rate_per_day = 0.02;
  parameters.grass.residual_kg_dm_per_ha = 1200.0;
  parameters.grass.nitrogen_content_fraction = 0.035;
  parameters.grass.nitrogen_fixation_kg_per_t_dm = 0.0;

  parameters.legume = parameters.grass;
  parameters.legume.species_id = "clover_white";
  parameters.legume.residual_kg_dm_per_ha = 400.0;
  parameters.legume.nitrogen_content_fraction = 0.045;
  parameters.legume.nitrogen_fixation_kg_per_t_dm = 25.0;

  return parameters;
}

FarmletInitialState initial_state() {
  FarmletInitialState state;
  state.soil_water_mm = 90.0;
  state.grass_kg_dm_per_ha = 2400.0;
  state.legume_kg_dm_per_ha = 700.0;
  state.soil_mineral_nitrogen_kg_per_ha = 60.0;
  return state;
}

DietQuality pasture_diet() {
  DietQuality diet;
  diet.metabolisable_energy_mj_per_kg_dm = 10.5;
  diet.digestibility_percent = 75.0;
  return diet;
}

Mob ewes(int head) {
  Mob mob;
  mob.name = "ewes";
  mob.head = head;
  mob.animal.class_id = "sheep_ewe";
  mob.animal.species_factor = 1.0;
  mob.animal.sex_factor = 1.0;
  mob.animal.standard_reference_weight_kg = 65.0;
  mob.animal.grazing_coefficient = 0.0025;
  mob.animal.gain_energy_ceiling_mj_per_kg = 20.3;
  mob.state.liveweight_kg = 60.0;
  mob.state.age_days = 1200.0;
  return mob;
}

/// A farm of twelve hectares divided into 2 ha paddocks, on a 25 m grid.
Farm build_farm() {
  const Raster<double> elevation = SyntheticElevationSource().fetch(farm_area(), kCellSize);
  const Raster<SoilWaterParameters> soils(elevation.cols(), elevation.rows(), elevation.transform(),
                                          soil());

  FarmletGrid grid(soils, sward(), initial_state(), -43.6);

  std::vector<Paddock> paddocks = SyntheticParcelSource(2.0).fetch(farm_area());
  PaddockMask mask(elevation, paddocks);

  return {std::move(grid), std::move(mask), std::move(paddocks)};
}

struct YearResult {
  BudgetLedger ledger;
  int days_short = 0;
  double eaten_kg_dm = 0.0;
};

YearResult run_year(Farm& farm, int days) {
  SyntheticWeatherParameters site;
  site.site_name = "conservation_site";
  site.latitude_degrees = -43.6;
  for (std::size_t month = 0; month < 12; ++month) {
    site.months[month].mean_daily_max_c = 18.0;
    site.months[month].mean_daily_min_c = 8.0;
    site.months[month].wet_day_probability = 0.3;
    site.months[month].mean_wet_day_rainfall_mm = 6.0;
    site.months[month].rainfall_shape = 1.0;
    site.months[month].mean_solar_radiation_mj = 14.0;
    site.months[month].mean_wind_speed_m_per_s = 3.0;
  }

  const SyntheticWeatherSource weather(site, 20240701);
  YearResult result;
  farm.set_opening_stocks(result.ledger);

  Date date{2023, 7, 1};
  for (int day = 0; day < days; ++day) {
    const WeatherSeries series = weather.fetch(DateRange{date, date});
    const FarmDay farm_day = farm.step(series.records.front(), pasture_diet(), &result.ledger);
    if (farm_day.any_mob_short) {
      ++result.days_short;
    }
    result.eaten_kg_dm += farm_day.total_eaten_kg_dm;
    date = Date::from_days_since_epoch(date.days_since_epoch() + 1);
  }
  return result;
}

// The gate. Adding animals adds an outflow, and the two budgets they touch have
// to close against the same closing stocks they did before there were any.
TEST(FarmConservationTest, DryMatterAndNitrogenCloseOverAGrazedYear) {
  Farm farm = build_farm();
  farm.add_mob(ewes(40), 0);
  farm.add_mob(ewes(30), 3);

  const YearResult year = run_year(farm, 365);

  ASSERT_GT(year.eaten_kg_dm, 0.0) << "nothing was eaten, so nothing was tested";

  EXPECT_TRUE(
      year.ledger.closes(Budget::DryMatter, farm.grid().mean_cover_kg_dm(), kConservationTolerance))
      << year.ledger.report(Budget::DryMatter, farm.grid().mean_cover_kg_dm());

  EXPECT_TRUE(year.ledger.closes(Budget::Nitrogen, farm.grid().mean_total_nitrogen_kg(),
                                 kConservationTolerance))
      << year.ledger.report(Budget::Nitrogen, farm.grid().mean_total_nitrogen_kg());

  EXPECT_TRUE(
      year.ledger.closes(Budget::Water, farm.grid().mean_soil_water_mm(), kConservationTolerance))
      << year.ledger.report(Budget::Water, farm.grid().mean_soil_water_mm());
}

// The negative control. If grazing offtake went unrecorded the dry matter
// budget would not close, so the test above is checking something rather than
// passing because both sides are zero.
TEST(FarmConservationTest, OfftakeThatWentUnrecordedWouldBeCaught) {
  Farm farm = build_farm();
  farm.add_mob(ewes(40), 0);

  const YearResult year = run_year(farm, 60);

  // Compare the honest closing stock against one inflated by a day's grazing:
  // that is what the ledger would be asked to accept if an offtake were missed.
  const double honest = farm.grid().mean_cover_kg_dm();
  const double missed_a_day = honest + (year.eaten_kg_dm / 100.0);

  EXPECT_TRUE(year.ledger.closes(Budget::DryMatter, honest, kConservationTolerance));
  EXPECT_FALSE(year.ledger.closes(Budget::DryMatter, missed_a_day, kConservationTolerance))
      << "an unrecorded offtake would slip through";
}

// The shape decision, asserted. A paddock is a set of cells; grazing one must
// leave the cells of another untouched, or the map view would show a farm
// grazed evenly by stock standing in one corner.
TEST(FarmConservationTest, GrazingOnePaddockLeavesTheOthersAlone) {
  Farm farm = build_farm();
  farm.add_mob(ewes(50), 0);

  const double grazed_before = farm.paddock_cover_kg_dm_per_ha(0);
  const double ungrazed_before = farm.paddock_cover_kg_dm_per_ha(1);

  SyntheticWeatherParameters site;
  site.site_name = "one_day";
  site.latitude_degrees = -43.6;
  for (std::size_t month = 0; month < 12; ++month) {
    site.months[month].mean_daily_max_c = 12.0;
    site.months[month].mean_daily_min_c = 6.0;
    site.months[month].wet_day_probability = 0.0;
    site.months[month].mean_wet_day_rainfall_mm = 0.0;
    site.months[month].rainfall_shape = 1.0;
    site.months[month].mean_solar_radiation_mj = 10.0;
    site.months[month].mean_wind_speed_m_per_s = 2.0;
  }
  const SyntheticWeatherSource weather(site, 7);
  const Date date{2023, 7, 1};
  const WeatherSeries series = weather.fetch(DateRange{date, date});

  const FarmDay day = farm.step(series.records.front(), pasture_diet(), nullptr);
  ASSERT_GT(day.total_eaten_kg_dm, 0.0);

  const double grazed_after = farm.paddock_cover_kg_dm_per_ha(0);
  const double ungrazed_after = farm.paddock_cover_kg_dm_per_ha(1);

  EXPECT_LT(grazed_after - grazed_before, ungrazed_after - ungrazed_before)
      << "the grazed paddock has to end up behind the one nobody was on";
  EXPECT_EQ(farm.days_since_grazed()[0], 0) << "paddock 0 was grazed today";
  EXPECT_GT(farm.days_since_grazed()[1], 0) << "paddock 1 was not";
}

// Cells inside a paddock are not interchangeable: the ones carrying more feed
// give up more. This is the allocation rule stated on FarmMob, and it is what
// makes a paddock with a bare corner behave differently from an even one.
TEST(FarmConservationTest, CellsWithMoreFeedGiveUpMoreOfIt) {
  Farm farm = build_farm();
  farm.add_mob(ewes(60), 0);

  // Every cell starts identical, so after grazing they should still be
  // identical - proportional allocation of an even offer takes evenly.
  SyntheticWeatherParameters site;
  site.site_name = "even";
  site.latitude_degrees = -43.6;
  for (std::size_t month = 0; month < 12; ++month) {
    site.months[month].mean_daily_max_c = 10.0;
    site.months[month].mean_daily_min_c = 5.0;
    site.months[month].wet_day_probability = 0.0;
    site.months[month].mean_wet_day_rainfall_mm = 0.0;
    site.months[month].rainfall_shape = 1.0;
    site.months[month].mean_solar_radiation_mj = 10.0;
    site.months[month].mean_wind_speed_m_per_s = 2.0;
  }
  const SyntheticWeatherSource weather(site, 11);
  const Date date{2023, 7, 1};

  const FarmDay day =
      farm.step(weather.fetch(DateRange{date, date}).records.front(), pasture_diet(), nullptr);
  ASSERT_GT(day.total_eaten_kg_dm, 0.0);

  // Collect the cells of paddock 0 and check they are still equal to each
  // other: an even offer taken proportionally stays even.
  const Raster<double> cover = farm.grid().cover_kg_dm();
  double first = -1.0;
  for (std::size_t row = 0; row < cover.rows(); ++row) {
    for (std::size_t col = 0; col < cover.cols(); ++col) {
      if (farm.mask().owner(col, row) != 0) {
        continue;
      }
      if (first < 0.0) {
        first = cover(col, row);
        continue;
      }
      EXPECT_NEAR(cover(col, row), first, 1e-9)
          << "cell (" << col << ", " << row << ") diverged from its paddock";
    }
  }
  ASSERT_GT(first, 0.0) << "paddock 0 owns no cells";
}

TEST(FarmConservationTest, AMismatchedMaskOrPaddockListIsRefused) {
  const Raster<double> elevation = SyntheticElevationSource().fetch(farm_area(), kCellSize);
  const Raster<SoilWaterParameters> soils(elevation.cols(), elevation.rows(), elevation.transform(),
                                          soil());
  FarmletGrid grid(soils, sward(), initial_state(), -43.6);

  const std::vector<Paddock> paddocks = SyntheticParcelSource(2.0).fetch(farm_area());
  const PaddockMask mask(elevation, paddocks);

  // A shorter paddock list than the mask was built from.
  std::vector<Paddock> fewer{paddocks.front()};
  EXPECT_THROW(Farm(std::move(grid), mask, std::move(fewer)), std::invalid_argument);
}

}  // namespace
}  // namespace paddock::core
