// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <paddock/core/AnimalEnergy.hpp>
#include <paddock/core/BudgetLedger.hpp>
#include <paddock/core/FarmletGrid.hpp>
#include <paddock/core/Grazing.hpp>
#include <paddock/core/PaddockMask.hpp>
#include <paddock/core/Raster.hpp>
#include <paddock/core/Terrain.hpp>
#include <paddock/core/Weather.hpp>

/// The whole farm: ground, the paddocks that divide it, and the stock on it.
///
/// This is the layer that had been missing. The grid grew pasture, the mask knew
/// which cell belonged to which paddock, and the animal model turned a mob into
/// an intake - and none of them had ever met. A farm is what holds all four
/// together without any of them learning about the others: the grid still knows
/// nothing about stock, the mask is still pure geometry, and the energy model
/// still takes an animal rather than a farm.
namespace paddock::core {

/// **The ground stays per cell, not per paddock.** Each cell keeps its own soil
/// bucket and its own sward, so a shallow corner of a paddock dries out and
/// stops growing while the rest carries on, and the map view can show it. A
/// paddock is a set of cells, not a unit of pasture.
///
/// What a mob eats is therefore spread over the cells its paddock owns, **in
/// proportion to what each cell has above its residual**. That is an
/// assumption, and it is worth naming: it says stock take more from the parts
/// of a paddock that have more feed, which is closer to the truth than eating
/// every cell down equally, and it is not the same as knowing where stock
/// actually walk. Slope-driven preference is open item 9 in docs/verify.md and
/// would refine this; until it has a source, proportional allocation is the
/// honest default because it follows from the feed being there rather than from
/// a guess about the animals.
struct FarmMob {
  Mob mob;

  /// The paddocks this mob has the run of - **one under rotation, all of them
  /// under set stocking**.
  ///
  /// A list rather than an index, because set stocking is not "stay put". Smith
  /// and Dawson (1976) are explicit that over lambing "the whole of the farm
  /// area should be used for grazing", and a mob modelled as confined to the
  /// one paddock it happened to be on would starve there while the rest of the
  /// farm grew. It did, in the first run of the year-long scenario: 55 kg to
  /// 40 kg over seventy days on two hectares, with cover above 2000 kg DM/ha
  /// everywhere else.
  std::vector<std::size_t> paddocks{0};

  /// Days this mob has been where it is. Zero on the day it arrives, and the
  /// number a graze-length rule is checked against.
  int days_on_paddock = 0;

  /// Convenience for the common case of a mob on one paddock.
  [[nodiscard]] std::size_t paddock() const { return paddocks.front(); }
};

/// What one day did to one mob.
struct MobDay {
  std::string mob_name;
  std::size_t paddock = 0;
  GrazingDay grazing;
  LiveweightResponse response;

  /// Bought feed fed out to this mob today, kg of dry matter. It comes from
  /// off the farm, so it enters the dry matter budget as an inflow rather than
  /// appearing from nowhere - and a run that needed a lot of it is a run whose
  /// pasture did not carry the stock, which is the thing worth reporting.
  double supplement_kg_dm = 0.0;
};

/// What one day did to the farm.
struct FarmDay {
  Date date{};
  std::vector<MobDay> mobs;

  double total_eaten_kg_dm = 0.0;
  double total_nitrogen_removed_kg = 0.0;

  /// Bought feed across all mobs today.
  double total_supplement_kg_dm = 0.0;

  /// True when any mob went short. The signal a feed budget is not working.
  bool any_mob_short = false;
};

class Farm {
 public:
  /// `paddocks` must be the same list the mask was built from, in the same
  /// order, because the mask stores indices into it.
  Farm(FarmletGrid grid, PaddockMask mask, std::vector<Paddock> paddocks);

  /// Gives the farm its slopes, so the cost of walking a paddock reflects the
  /// ground rather than assuming a terrace. Optional: a farm modelled flat is
  /// still a valid farm. Must match the grid's shape.
  void set_slopes(const Raster<double>& slope_degrees);

  /// Puts a mob on a paddock, and returns its index.
  std::size_t add_mob(Mob mob, std::size_t paddock);

  /// Moves a mob onto one paddock, and resets its count of days where it
  /// stands. This is all a farmer agent needs from the farm; deciding *when* to
  /// move belongs to the grazing calendar and to whatever reads it.
  void move_mob(std::size_t mob, std::size_t paddock);

  /// Gives a mob the run of the whole farm, which is what set stocking is.
  /// Every paddock is then being grazed, so none of them rests - which is the
  /// agronomic point of the system and the reason it grows less.
  void spread_mob(std::size_t mob);

  /// Which mob has a paddock, or kNobody. A paddock carries at most one mob
  /// here: two mobs sharing ground is a real practice, but it needs a rule for
  /// how they divide the feed, and inventing one would be inventing a result.
  static constexpr std::size_t kNobody = static_cast<std::size_t>(-1);
  [[nodiscard]] std::size_t mob_on(std::size_t paddock) const;

  /// Days since each paddock was last grazed, which is what a rotation is
  /// judged on. A paddock never grazed reports the days since the farm started.
  [[nodiscard]] const std::vector<int>& days_since_grazed() const noexcept {
    return days_since_grazed_;
  }

  /// One day: pasture grows on every cell, then each mob eats where it stands
  /// and gains or loses accordingly.
  ///
  /// Growth first, then grazing, and the order is not arbitrary - a mob eats
  /// what is standing when it walks in, which includes what grew that morning.
  ///
  /// With a ledger attached, grazing offtake is recorded on the same terms the
  /// grid uses: per-hectare means over the whole farm, so the dry matter and
  /// nitrogen budgets close against `mean_cover_kg_dm` and
  /// `mean_total_nitrogen_kg` exactly as they did before there were animals.
  /// `supplement_kg_dm` is bought feed to hand out, one entry per mob, in the
  /// order mobs were added. Empty means none. It **substitutes** for grazing
  /// rather than adding to it: a mob has an appetite, what it is handed comes
  /// out of that appetite, and it grazes for the rest. Fed on top instead, a
  /// 55 kg ewe reached 101 kg in a year, because supplement was buying gain
  /// the animal had no appetite to eat.
  FarmDay step(const DailyWeather& weather, const DietQuality& diet,
               const std::vector<double>& supplement_kg_dm, BudgetLedger* ledger = nullptr);

  FarmDay step(const DailyWeather& weather, const DietQuality& diet,
               BudgetLedger* ledger = nullptr);

  [[nodiscard]] const FarmletGrid& grid() const noexcept { return grid_; }

  [[nodiscard]] const PaddockMask& mask() const noexcept { return mask_; }

  [[nodiscard]] const std::vector<Paddock>& paddocks() const noexcept { return paddocks_; }

  [[nodiscard]] const std::vector<FarmMob>& mobs() const noexcept { return mobs_; }

  /// Cover on one paddock, kg DM per hectare, averaged over its cells.
  [[nodiscard]] double paddock_cover_kg_dm_per_ha(std::size_t paddock) const;

  /// Total stock units of dry matter standing above the residual on a paddock,
  /// which is what is actually available to eat.
  [[nodiscard]] double paddock_offer_kg_dm(std::size_t paddock) const;

  /// Opening stocks for the conservation gate, on the grid's per-hectare terms.
  void set_opening_stocks(BudgetLedger& ledger) const;

 private:
  /// The cells each paddock owns, worked out once from the mask rather than by
  /// scanning the grid every day.
  void index_cells_by_paddock();

  [[nodiscard]] GrazingConditions conditions_on(const std::vector<std::size_t>& held,
                                                const Mob& mob) const;

  FarmletGrid grid_;
  PaddockMask mask_;
  std::vector<Paddock> paddocks_;
  std::vector<FarmMob> mobs_;

  /// Column-major-free: (col, row) pairs, flattened, one list per paddock.
  std::vector<std::vector<std::size_t>> cells_of_paddock_;

  std::vector<int> days_since_grazed_;

  /// Empty when the farm is modelled flat.
  std::vector<double> slope_degrees_;

  /// Reused so a year of stepping does not allocate a ledger a day.
  BudgetLedger grazing_scratch_;
};

}  // namespace paddock::core
