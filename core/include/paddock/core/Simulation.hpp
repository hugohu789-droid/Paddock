// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Pasture.hpp>
#include <paddock/core/SimulationClock.hpp>
#include <paddock/core/SoilWater.hpp>
#include <paddock/core/Weather.hpp>

namespace paddock::core {

/// Where a farmlet starts. Every one of these is part of the scenario bundle:
/// a run that began with a full profile is not the same run as one that began
/// with an empty one, and a result that does not say which is not reproducible.
struct FarmletInitialState {
  double soil_water_mm = 0.0;
  double grass_kg_dm_per_ha = 0.0;
  double legume_kg_dm_per_ha = 0.0;
  double soil_mineral_nitrogen_kg_per_ha = 0.0;
};

/// One simulated day, in the terms a farmer or a plot would recognise.
struct DailyRecord {
  Date date;
  double rainfall_mm = 0.0;
  double evapotranspiration_mm = 0.0;
  double drainage_mm = 0.0;
  double soil_water_mm = 0.0;
  double water_stress_coefficient = 1.0;
  double growth_kg_dm = 0.0;
  double cover_kg_dm = 0.0;
  double legume_fraction = 0.0;
  double soil_mineral_nitrogen_kg = 0.0;

  /// Nitrate that left the root zone today, kg N/ha. **The number a regional
  /// council asks for**, summed over a year and compared against a limit.
  double nitrate_leached_kg = 0.0;
};

/// A hectare of pasture on one soil: the smallest thing that is a farm.
///
/// M2 simulates one of these. M3 gives each paddock its own and drives them
/// from a raster, which is why the coupling lives here rather than in the
/// application: the order the processes run in, and what each reports to the
/// ledger, is part of the model, not part of the user interface.
class Farmlet {
 public:
  Farmlet(SoilWaterParameters soil, SwardParameters sward, const FarmletInitialState& initial,
          double latitude_degrees);

  /// Water first, then growth on the stress the water balance reports.
  ///
  /// That stress is FAO-56's: computed from the depletion the *previous* day
  /// left behind (Eq. 84), so today's rain lifts tomorrow's growth rather than
  /// today's. A farm that was dry this morning was dry all day.
  /// `radiation_ratio` is how much radiation this cell's ground receives
  /// relative to level ground - one on the flat, more on a sunny slope, less on
  /// a shaded one. It enters in both the places radiation matters: the
  /// evapotranspiration that dries the soil, and the light the sward
  /// intercepts. Aspect is not a growth coefficient here; it is radiation, and
  /// the seasonal reversal Ballantrae measured is left to emerge from the two.
  /// `irrigation_mm` is water put on deliberately, and it is a number handed
  /// in rather than a decision made here. Nothing in this class knows what an
  /// irrigation policy is: the soil reports how dry it is, somebody else
  /// decides, and the water arrives as a quantity - which is what keeps a
  /// management rule out of the biology.
  DailyRecord step(const DailyWeather& weather, double radiation_ratio = 1.0,
                   BudgetLedger* ledger = nullptr, double irrigation_mm = 0.0);

  /// Returns a day's dung and urine to this cell, kg N per hectare.
  void return_excreta(double urine_kg_n_per_ha, double dung_kg_n_per_ha,
                      const ExcretaParameters& excreta) {
    // Kept, because leaching happens in `step` and needs the same parameters
    // the excreta arrived under - a cell whose leaching used different figures
    // from its excreta would be two farms.
    excreta_ = excreta;
    sward_.return_excreta(urine_kg_n_per_ha, dung_kg_n_per_ha, excreta);
  }

  /// Cuts this cell down to `leave_kg_dm_per_ha` and returns what came off.
  double cut_to(double leave_kg_dm_per_ha) { return sward_.cut_to(leave_kg_dm_per_ha); }

  [[nodiscard]] const SoilWaterBucket& soil() const noexcept { return soil_; }

  [[nodiscard]] const PastureSward& sward() const noexcept { return sward_; }

  /// Removes green dry matter from this cell, in kg per hectare of it.
  ///
  /// A narrow entry rather than a mutable reference to the sward: defoliation
  /// has to respect the residual, and handing out the sward would let a caller
  /// reach past that. What comes back is what was actually taken, which is less
  /// than was asked for when the cell is grazed down.
  PastureSward::Defoliation graze(double requested_kg_dm_per_ha);

  [[nodiscard]] double latitude_degrees() const noexcept { return latitude_degrees_; }

  /// Opening stocks for the three budget lines, in ledger order.
  void set_opening_stocks(BudgetLedger& ledger) const;

 private:
  SoilWaterBucket soil_;
  PastureSward sward_;
  ExcretaParameters excreta_;
  double latitude_degrees_ = 0.0;
};

/// Totals a reader can check against a farm's own records.
struct RunSummary {
  std::int64_t days = 0;
  double total_rainfall_mm = 0.0;
  double total_evapotranspiration_mm = 0.0;
  double total_drainage_mm = 0.0;
  double total_growth_kg_dm = 0.0;
  double total_nitrogen_fixed_kg = 0.0;
  double closing_soil_water_mm = 0.0;
  double closing_cover_kg_dm = 0.0;
  double closing_legume_fraction = 0.0;
};

struct RunResult {
  std::vector<DailyRecord> daily;
  RunSummary summary;
  BudgetLedger ledger;
  Provenance weather_provenance;

  /// True when water, dry matter and nitrogen all close to 1e-9 against the
  /// state the farmlet actually holds. A run that reports otherwise has lost
  /// something, and no result from it should be used.
  [[nodiscard]] bool budgets_close(const Farmlet& farmlet) const;
};

/// Runs a farmlet over a date range against a weather source.
///
/// The weather is fetched once, up front: a run that pulled data day by day
/// could see a source change under it, and reproducibility starts with the
/// inputs being fixed before the first step.
[[nodiscard]] RunResult run(Farmlet& farmlet, const WeatherSource& weather, const DateRange& range);

}  // namespace paddock::core
