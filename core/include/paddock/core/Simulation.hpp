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
  DailyRecord step(const DailyWeather& weather, BudgetLedger* ledger = nullptr);

  [[nodiscard]] const SoilWaterBucket& soil() const noexcept { return soil_; }

  [[nodiscard]] const PastureSward& sward() const noexcept { return sward_; }

  [[nodiscard]] double latitude_degrees() const noexcept { return latitude_degrees_; }

  /// Opening stocks for the three budget lines, in ledger order.
  void set_opening_stocks(BudgetLedger& ledger) const;

 private:
  SoilWaterBucket soil_;
  PastureSward sward_;
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
