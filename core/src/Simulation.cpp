// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <utility>

#include <paddock/core/Simulation.hpp>

namespace paddock::core {

Farmlet::Farmlet(SoilWaterParameters soil, SwardParameters sward,
                 const FarmletInitialState& initial, double latitude_degrees)
    : soil_(soil, initial.soil_water_mm),
      sward_(std::move(sward), initial.grass_kg_dm_per_ha, initial.legume_kg_dm_per_ha,
             initial.soil_mineral_nitrogen_kg_per_ha),
      latitude_degrees_(latitude_degrees) {}

void Farmlet::set_opening_stocks(BudgetLedger& ledger) const {
  ledger.set_opening_stock(Budget::Water, soil_.water_mm());
  ledger.set_opening_stock(Budget::DryMatter, sward_.cover_kg_dm());
  ledger.set_opening_stock(Budget::Nitrogen, sward_.total_nitrogen_kg());
}

DailyRecord Farmlet::step(const DailyWeather& weather, double radiation_ratio,
                          BudgetLedger* ledger) {
  const SoilWaterFluxes water = soil_.step(weather, latitude_degrees_, radiation_ratio, ledger);

  // The second place radiation enters: the light the sward intercepts. Scaling
  // the measured horizontal figure onto this cell's slope is the transfer Allen
  // et al. (2006) describe, and it is done here rather than inside the sward so
  // that PastureSward keeps knowing nothing about terrain.
  DailyWeather on_this_slope = weather;
  on_this_slope.solar_radiation_mj_per_m2 *= radiation_ratio;
  const PastureGrowth growth = sward_.step(on_this_slope, water.stress_coefficient, ledger);

  DailyRecord record;
  record.date = weather.date;
  record.rainfall_mm = water.rainfall_mm;
  record.evapotranspiration_mm = water.evapotranspiration_mm;
  record.drainage_mm = water.drainage_mm;
  record.soil_water_mm = soil_.water_mm();
  record.water_stress_coefficient = water.stress_coefficient;
  record.growth_kg_dm = growth.total_growth_kg_dm();
  record.cover_kg_dm = sward_.cover_kg_dm();
  record.legume_fraction = sward_.legume_fraction();
  record.soil_mineral_nitrogen_kg = sward_.soil_mineral_nitrogen_kg();
  return record;
}

bool RunResult::budgets_close(const Farmlet& farmlet) const {
  return ledger.closes(Budget::Water, farmlet.soil().water_mm()) &&
         ledger.closes(Budget::DryMatter, farmlet.sward().cover_kg_dm()) &&
         ledger.closes(Budget::Nitrogen, farmlet.sward().total_nitrogen_kg());
}

RunResult run(Farmlet& farmlet, const WeatherSource& weather, const DateRange& range) {
  const WeatherSeries series = weather.fetch(range);

  RunResult result;
  result.weather_provenance = series.provenance;
  result.daily.reserve(series.records.size());
  farmlet.set_opening_stocks(result.ledger);

  for (const DailyWeather& day : series.records) {
    // A single farmlet has no terrain: run() models one representative
    // hectare, and slope belongs to a grid of them. Level ground, explicitly.
    const DailyRecord record = farmlet.step(day, 1.0, &result.ledger);

    result.summary.total_rainfall_mm += record.rainfall_mm;
    result.summary.total_evapotranspiration_mm += record.evapotranspiration_mm;
    result.summary.total_drainage_mm += record.drainage_mm;
    result.summary.total_growth_kg_dm += record.growth_kg_dm;
    result.daily.push_back(record);
  }

  // Fixation is the only way nitrogen enters, so the ledger's nitrogen inflow
  // is exactly what the clover fixed. Taking it from there rather than
  // accumulating a second copy keeps one number that cannot drift from the
  // other.
  result.summary.total_nitrogen_fixed_kg = result.ledger.total_inflow(Budget::Nitrogen);

  result.summary.days = static_cast<std::int64_t>(result.daily.size());
  result.summary.closing_soil_water_mm = farmlet.soil().water_mm();
  result.summary.closing_cover_kg_dm = farmlet.sward().cover_kg_dm();
  result.summary.closing_legume_fraction = farmlet.sward().legume_fraction();
  return result;
}

}  // namespace paddock::core
