#pragma once

#include <cstddef>
#include <vector>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/Simulation.hpp>

namespace paddock::core {

/// A raster of farmlets: one soil water bucket and one sward per cell.
///
/// This is where "spatially explicit" stops being a description and starts
/// being code. Every cell carries its own soil, so a shallow corner of a farm
/// dries out first and stops growing while a deeper one carries on - which is
/// the thing a map view exists to show, and which no single-paddock average can
/// express.
///
/// Weather is the same across the grid for now. Terrain-driven variation in
/// radiation and temperature arrives with the DEM in M3; the shape of this
/// class does not have to change for it, because each cell is already stepped
/// with its own weather argument.
class FarmletGrid {
 public:
  /// One soil per cell. The raster carries the georeferencing, so a grid knows
  /// where it is even before `gis/` exists to tell it.
  FarmletGrid(const Raster<SoilWaterParameters>& soils, const SwardParameters& sward,
              const FarmletInitialState& initial, double latitude_degrees);

  /// Steps every cell, row-major.
  ///
  /// Cells do not interact, so the order cannot change the result; row-major is
  /// chosen because it is the order everything else in `Raster<T>` uses.
  ///
  /// With a ledger attached, each cell reports into a scratch ledger which is
  /// folded in once per day, scaled by one over the number of cells. The farm
  /// ledger therefore holds per-hectare means, which is the only unit that
  /// makes sense for a depth in millimetres and a cover in kg DM/ha at once.
  void step(const DailyWeather& weather, BudgetLedger* ledger = nullptr);

  /// Opening stocks for the three budget lines, as per-hectare means.
  void set_opening_stocks(BudgetLedger& ledger) const;

  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }

  [[nodiscard]] std::size_t cell_count() const noexcept { return cells_.size(); }

  [[nodiscard]] const GeoTransform& transform() const noexcept { return transform_; }

  [[nodiscard]] const Farmlet& cell(std::size_t col, std::size_t row) const;

  /// Snapshots for the map view. Each returns a raster sharing this grid's
  /// georeferencing, so the view never has to reconstruct it.
  [[nodiscard]] Raster<double> cover_kg_dm() const;
  [[nodiscard]] Raster<double> soil_water_mm() const;
  [[nodiscard]] Raster<double> water_stress() const;
  [[nodiscard]] Raster<double> legume_fraction() const;

  /// Per-hectare means, compensated: the closing stocks the conservation tests
  /// compare the ledger against.
  [[nodiscard]] double mean_cover_kg_dm() const;
  [[nodiscard]] double mean_soil_water_mm() const;
  [[nodiscard]] double mean_total_nitrogen_kg() const;

 private:
  template <typename Fn>
  [[nodiscard]] Raster<double> snapshot(Fn&& value_of) const;

  std::size_t cols_ = 0;
  std::size_t rows_ = 0;
  GeoTransform transform_{};
  std::vector<Farmlet> cells_;
  /// Reused every day so that a year of stepping does not allocate a ledger a
  /// day. Cleared at the start of each step.
  BudgetLedger scratch_;
};

}  // namespace paddock::core
