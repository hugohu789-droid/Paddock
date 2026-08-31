// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/Simulation.hpp>
#include <paddock/core/SlopeRadiation.hpp>

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

  /// Gives every cell the radiation of its own slope instead of level ground's.
  ///
  /// Optional, and absent by default: a farm modelled as flat is still a valid
  /// farm, and every scenario written before terrain existed keeps its results
  /// unchanged. Once set, each cell's evapotranspiration and intercepted light
  /// are scaled by what its slope and aspect actually receive.
  ///
  /// `ground` must match the grid's shape. Building the table costs about
  /// 0.7 s for 1536 cells - see SlopeRadiationTable - and is done once here
  /// rather than per day.
  void set_terrain(const Topography& ground);

  /// Whether terrain has been supplied. Without it every cell is level ground.
  [[nodiscard]] bool has_terrain() const noexcept { return radiation_ != nullptr; }

  /// Steps every cell, row-major.
  ///
  /// Cells do not interact, so the order cannot change the result; row-major is
  /// chosen because it is the order everything else in `Raster<T>` uses.
  ///
  /// With a ledger attached, each cell reports into a scratch ledger which is
  /// folded in once per day, scaled by one over the number of cells. The farm
  /// ledger therefore holds per-hectare means, which is the only unit that
  /// makes sense for a depth in millimetres and a cover in kg DM/ha at once.
  /// `irrigation_mm` is water put on deliberately, one entry per cell in row
  /// order, or empty for none.
  ///
  /// Handed in rather than worked out here, and that is the whole arrangement:
  /// the grid reports how dry each cell is, an irrigation rule reads that and
  /// decides, and the water comes back as a quantity. A grid that decided for
  /// itself would be a management policy hiding inside the biology, and there
  /// would be no way to run the same farm under a different rule.
  void step(const DailyWeather& weather, BudgetLedger* ledger = nullptr,
            const std::vector<double>& irrigation_mm = {});

  /// The root zone depletion of every cell, mm. What an irrigation rule reads.
  [[nodiscard]] Raster<double> depletion_mm() const;

  /// The total available water each cell holds, mm. The other half of what a
  /// depletion means.
  [[nodiscard]] double total_available_water_mm() const noexcept;

  /// Opening stocks for the three budget lines, as per-hectare means.
  void set_opening_stocks(BudgetLedger& ledger) const;

  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }

  [[nodiscard]] std::size_t cell_count() const noexcept { return cells_.size(); }

  [[nodiscard]] const GeoTransform& transform() const noexcept { return transform_; }

  [[nodiscard]] const Farmlet& cell(std::size_t col, std::size_t row) const;

  /// Grazes one cell, in kg of dry matter per hectare of that cell.
  ///
  /// The grid does not decide how much: it does not know about mobs, and a
  /// class that knew both the soil physics and the stocking rate would be two
  /// models in one file. Deciding is Farm's job; this is how the decision
  /// reaches the ground.
  PastureSward::Defoliation graze_cell(std::size_t col, std::size_t row,
                                       double requested_kg_dm_per_ha);

  /// Returns a day's dung and urine to one cell, in kg N per hectare.
  void return_excreta_to_cell(std::size_t col, std::size_t row, double urine_kg_n_per_ha,
                              double dung_kg_n_per_ha, const ExcretaParameters& excreta);

  /// Nitrate leached past the root zone across the whole grid today, kg N/ha as
  /// a mean of the cells. Driven by each cell's own drainage, so a wet corner
  /// leaches and a dry one does not.
  [[nodiscard]] double mean_nitrate_leached_kg_per_ha() const noexcept {
    return leached_today_kg_per_ha_;
  }

  /// Nitrate waiting under urine patches, kg N/ha, mean over the grid.
  [[nodiscard]] double mean_patch_nitrate_kg_per_ha() const;

  /// Snapshots for the map view. Each returns a raster sharing this grid's
  /// georeferencing, so the view never has to reconstruct it.
  [[nodiscard]] Raster<double> cover_kg_dm() const;
  [[nodiscard]] Raster<double> soil_water_mm() const;
  [[nodiscard]] Raster<double> water_stress() const;

  /// How much of the water each cell can hold is still there, 0 to 1.
  ///
  /// The same fact as `depletion_mm` read from the other end, and the end a
  /// person uses: a paddock is at 40% rather than 60% depleted.
  [[nodiscard]] Raster<double> available_water_fraction() const;

  /// The water put on each cell on the last day stepped, mm.
  ///
  /// **What the grid was handed, not what it decided** - it decides nothing.
  /// Recorded so a map can show where the water went, which is the one thing
  /// an irrigation rule does that a mean over the farm cannot show.
  [[nodiscard]] Raster<double> last_irrigation_mm() const;

  /// What the pasture grew on the last day stepped, kg DM/ha.
  ///
  /// **Growth, not cover.** Cover is the standing stock and moves for two
  /// reasons at once - it falls where stock have been and rises where the grass
  /// grew - so a cover map cannot answer which part of the farm is actually
  /// producing. This is the production side on its own, before any grazing, and
  /// it is the map that shows irrigation paying for itself: the watered ground
  /// grows more the following days, and on a cover map that is hidden under
  /// whatever the mob did.
  ///
  /// Recorded rather than recomputed. `Farmlet::step` already returns it in its
  /// `DailyRecord`; working it out again from cover differences here would give
  /// a second answer that could disagree with the first.
  [[nodiscard]] Raster<double> last_growth_kg_dm() const;
  [[nodiscard]] Raster<double> legume_fraction() const;

  /// Per-hectare means, compensated: the closing stocks the conservation tests
  /// compare the ledger against.
  [[nodiscard]] double mean_cover_kg_dm() const;

  /// Mean GREEN dry matter, kg DM/ha - the part an animal can eat.
  ///
  /// **Not the same as cover, and the difference is the point.** `cover` is
  /// green plus the dead standing material above it, which is what a plate
  /// meter sweeps up and what a total-dry-matter budget has to account for.
  /// What a mob can actually put in its mouth is the green, and a farm that
  /// manages to a cover figure padded with thatch is managing to a number no
  /// animal can eat.
  [[nodiscard]] double mean_green_kg_dm() const;
  [[nodiscard]] double mean_soil_water_mm() const;
  [[nodiscard]] double mean_total_nitrogen_kg() const;

 private:
  template <typename Fn>
  [[nodiscard]] Raster<double> snapshot(Fn&& value_of) const;

  /// One where no terrain has been supplied, so that a grid without it steps
  /// exactly as it did before terrain existed.
  [[nodiscard]] double radiation_ratio(std::size_t col, std::size_t row, int day_of_year) const;

  std::size_t cols_ = 0;
  std::size_t rows_ = 0;
  GeoTransform transform_{};
  std::vector<Farmlet> cells_;

  /// What today's drainage carried past the root zone, kg N/ha, grid mean.
  double leached_today_kg_per_ha_ = 0.0;

  /// The water applied on the last day stepped, one entry per cell.
  std::vector<double> last_irrigation_mm_;
  std::vector<double> last_growth_kg_dm_;
  double latitude_degrees_ = 0.0;
  /// Null until set_terrain: every cell is level ground.
  std::unique_ptr<SlopeRadiationTable> radiation_;
  /// Reused every day so that a year of stepping does not allocate a ledger a
  /// day. Cleared at the start of each step.
  BudgetLedger scratch_;
};

}  // namespace paddock::core
