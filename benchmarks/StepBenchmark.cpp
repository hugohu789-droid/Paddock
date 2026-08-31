// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

/// T4's performance baselines: raster size against herd size.
///
/// **What these are for.** `CLAUDE.md` rules out parallelism inside a run and
/// justifies it with a number - single-threaded stepping is about a second per
/// simulated year at target scale. That claim is load-bearing: it is why the
/// conservation assertions can be exact and why the golden baselines hold. A
/// claim nothing measures is a claim that quietly stops being true.
///
/// The two axes are the ones that grow. A 200 ha farm at 10 m is about 20,000
/// cells, and the raster axis runs either side of that. Herd size is the other,
/// because the grazing step spreads a mob's offtake over the cells it has the
/// run of - so the cost is not simply the product of the two, which is why both
/// are here rather than one.
///
/// **What the first run of these said**, RelWithDebInfo on an 8-thread 2.9 GHz
/// machine, one simulated year:
///
///   |   cells | grid only | with stock |
///   |--------:|----------:|-----------:|
///   |     400 |     31 ms |          - |
///   |   2 500 |    206 ms |     206 ms |
///   |  22 500 |  1 925 ms |   1 868 ms |
///   |  90 000 |  7 907 ms |          - |
///
/// Two things worth keeping. **Cost is linear in cells** - nine times the cells
/// costs 9.3 times the time, four times costs 4.1 - so nothing in the step is
/// quietly quadratic. And **herd size is nearly free**: 500 head against 5 000
/// on the same ground differ by four per cent, because the work is per cell and
/// not per animal. That is a result rather than an assumption, and it is why
/// both axes are measured instead of one being reasoned about.
///
/// It also puts a number on the claim that justifies staying single-threaded.
/// At target scale a year costs about two seconds on this machine, not the
/// "~1s per simulated year" CLAUDE.md states. Same order, and the conclusion it
/// supports is unchanged - but the figure in the roadmap was never measured
/// until now, and these numbers are what it should be reconciled against.

#include <benchmark/benchmark.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <paddock/core/DataSource.hpp>
#include <paddock/core/Farm.hpp>
#include <paddock/core/FarmletGrid.hpp>
#include <paddock/core/Geometry.hpp>
#include <paddock/core/Grazing.hpp>
#include <paddock/core/PaddockMask.hpp>
#include <paddock/core/Pasture.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/SoilWater.hpp>
#include <paddock/core/SyntheticTerrain.hpp>
#include <paddock/core/SyntheticWeather.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {
namespace {

constexpr double kWest = 1'570'000.0;
constexpr double kSouth = 5'176'000.0;
constexpr double kCellSize = 10.0;
constexpr double kLatitude = -43.6;

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

/// A square farm of `cells` cells at 10 m.
BoundingBox area_of(std::size_t cells) {
  auto side = static_cast<std::size_t>(std::sqrt(static_cast<double>(cells)));
  side = side < 2 ? 2 : side;
  const double metres = static_cast<double>(side) * kCellSize;

  BoundingBox area = BoundingBox::empty();
  area.expand_to_include(Point2D{kWest, kSouth});
  area.expand_to_include(Point2D{kWest + metres, kSouth + metres});
  return area;
}

FarmletGrid grid_of(const BoundingBox& area) {
  const Raster<double> elevation = SyntheticElevationSource().fetch(area, kCellSize);
  const Raster<SoilWaterParameters> soils(elevation.cols(), elevation.rows(), elevation.transform(),
                                          soil());
  return FarmletGrid(soils, sward(), initial_state(), kLatitude);
}

/// A flat climate: the same month twelve times. A benchmark wants a workload it
/// can repeat, not a realistic season - the seasonal shape belongs to the
/// validation suite, and varying it here would only make the timings noisier.
WeatherSeries a_year() {
  SyntheticWeatherParameters site;
  site.site_name = "benchmark_site";
  site.latitude_degrees = kLatitude;
  for (std::size_t month = 0; month < 12; ++month) {
    site.months[month].mean_daily_max_c = 18.0;
    site.months[month].mean_daily_min_c = 8.0;
    site.months[month].wet_day_probability = 0.3;
    site.months[month].mean_wet_day_rainfall_mm = 6.0;
    site.months[month].rainfall_shape = 1.0;
    site.months[month].mean_solar_radiation_mj = 14.0;
    site.months[month].mean_wind_speed_m_per_s = 3.0;
  }

  const SyntheticWeatherSource weather(site, 20240702);
  return weather.fetch(DateRange{Date{2023, 7, 1}, Date{2024, 6, 30}});
}

/// The pasture and water step with no stock on it: growth, soil water, nitrogen.
/// The cost here is what every run pays before a single animal is added.
void GridStepsAYear(benchmark::State& state) {
  const auto cells = static_cast<std::size_t>(state.range(0));
  const BoundingBox area = area_of(cells);
  const WeatherSeries weather = a_year();

  for (auto _ : state) {
    try {
      state.PauseTiming();
      FarmletGrid grid = grid_of(area);
      state.ResumeTiming();

      for (const DailyWeather& day : weather.records) {
        grid.step(day);
      }
      benchmark::DoNotOptimize(grid.mean_cover_kg_dm());
    } catch (const std::exception& error) {
      state.SkipWithError(error.what());
      break;
    }
  }

  state.SetLabel(std::to_string(cells) + " cells, 366 days");
}

/// The whole farm step: the grid above plus intake, liveweight and offtake
/// spread over the cells each mob has the run of.
void FarmStepsAYear(benchmark::State& state) {
  const auto cells = static_cast<std::size_t>(state.range(0));
  const auto head = static_cast<int>(state.range(1));
  const BoundingBox area = area_of(cells);
  const WeatherSeries weather = a_year();

  for (auto _ : state) {
    state.PauseTiming();
    const Raster<double> elevation = SyntheticElevationSource().fetch(area, kCellSize);
    std::vector<Paddock> paddocks = SyntheticParcelSource(2.0).fetch(area);
    PaddockMask mask(elevation, paddocks);
    Farm farm(grid_of(area), std::move(mask), std::move(paddocks));
    farm.add_mob(ewes(head), 0);
    state.ResumeTiming();

    for (const DailyWeather& day : weather.records) {
      benchmark::DoNotOptimize(farm.step(day, pasture_diet(), nullptr));
    }
  }

  state.SetLabel(std::to_string(cells) + " cells, " + std::to_string(head) + " head, 366 days");
}

// 400 cells is a small paddock, 22,500 is about a 200 ha farm at 10 m, and
// 90,000 is four times that - enough to show whether the cost is linear or
// whether something in the step is quietly quadratic.
BENCHMARK(GridStepsAYear)
    ->Arg(400)
    ->Arg(2500)
    ->Arg(22500)
    ->Arg(90000)
    ->Unit(benchmark::kMillisecond);

// Cells against head. The stocking rates are realistic at the larger sizes and
// deliberately absurd at the small ones: a mob that cannot be fed is still a
// mob the step has to cost.
BENCHMARK(FarmStepsAYear)
    ->Args({2500, 50})
    ->Args({2500, 500})
    ->Args({22500, 500})
    ->Args({22500, 5000})
    ->Unit(benchmark::kMillisecond);

}  // namespace
}  // namespace paddock::core

BENCHMARK_MAIN();
