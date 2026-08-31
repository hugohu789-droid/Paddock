// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <paddock/core/Farm.hpp>

namespace paddock::core {

namespace {

/// A cell's offer: what stands above each species' residual, kg per hectare.
double offer_per_hectare(const Farmlet& cell) {
  const SwardParameters& parameters = cell.sward().parameters();
  return std::max(0.0, cell.sward().grass_kg_dm() - parameters.grass.residual_kg_dm_per_ha) +
         std::max(0.0, cell.sward().legume_kg_dm() - parameters.legume.residual_kg_dm_per_ha);
}

}  // namespace

Farm::Farm(FarmletGrid grid, PaddockMask mask, std::vector<Paddock> paddocks)
    : grid_(std::move(grid)), mask_(std::move(mask)), paddocks_(std::move(paddocks)) {
  if (paddocks_.empty()) {
    throw std::invalid_argument("Farm: a farm needs at least one paddock");
  }
  if (mask_.cell_counts().size() != paddocks_.size()) {
    throw std::invalid_argument(
        "Farm: the mask was built from a different paddock list from the one given; it holds " +
        std::to_string(mask_.cell_counts().size()) + " paddocks against " +
        std::to_string(paddocks_.size()));
  }
  if (mask_.cols() != grid_.cols() || mask_.rows() != grid_.rows()) {
    throw std::invalid_argument("Farm: the mask and the grid are different shapes");
  }

  days_since_grazed_.assign(paddocks_.size(), 0);
  index_cells_by_paddock();
}

void Farm::index_cells_by_paddock() {
  cells_of_paddock_.assign(paddocks_.size(), {});
  for (std::size_t row = 0; row < grid_.rows(); ++row) {
    for (std::size_t col = 0; col < grid_.cols(); ++col) {
      const std::size_t owner = mask_.owner(col, row);
      if (owner != PaddockMask::kUnowned) {
        cells_of_paddock_[owner].push_back((row * grid_.cols()) + col);
      }
    }
  }
}

void Farm::set_slopes(const Raster<double>& slope_degrees) {
  if (slope_degrees.cols() != grid_.cols() || slope_degrees.rows() != grid_.rows()) {
    throw std::invalid_argument("Farm::set_slopes: the slope raster is a different shape");
  }
  slope_degrees_.assign(slope_degrees.size(), 0.0);
  for (std::size_t row = 0; row < grid_.rows(); ++row) {
    for (std::size_t col = 0; col < grid_.cols(); ++col) {
      slope_degrees_[(row * grid_.cols()) + col] = slope_degrees(col, row);
    }
  }
}

std::size_t Farm::add_mob(Mob mob, std::size_t paddock) {
  const std::string error = mob.validation_error();
  if (!error.empty()) {
    throw std::invalid_argument("Farm::add_mob: " + error);
  }
  if (paddock >= paddocks_.size()) {
    throw std::out_of_range("Farm::add_mob: no such paddock");
  }
  FarmMob placed;
  placed.mob = std::move(mob);
  placed.paddocks = {paddock};
  mobs_.push_back(std::move(placed));
  return mobs_.size() - 1;
}

void Farm::move_mob(std::size_t mob, std::size_t paddock) {
  if (mob >= mobs_.size()) {
    throw std::out_of_range("Farm::move_mob: no such mob");
  }
  if (paddock >= paddocks_.size()) {
    throw std::out_of_range("Farm::move_mob: no such paddock");
  }
  mobs_[mob].paddocks = {paddock};
  mobs_[mob].days_on_paddock = 0;
}

void Farm::set_target_gain(std::size_t mob, double kg_per_day) {
  if (mob >= mobs_.size()) {
    throw std::out_of_range("Farm::set_target_gain: no such mob");
  }
  mobs_[mob].mob.target_gain_kg_per_day = kg_per_day;
}

void Farm::set_mob_head(std::size_t mob, int head) {
  if (mob >= mobs_.size()) {
    return;
  }
  mobs_[mob].mob.head = std::max(0, head);
}

void Farm::spread_mob(std::size_t mob) {
  if (mob >= mobs_.size()) {
    throw std::out_of_range("Farm::spread_mob: no such mob");
  }
  if (mobs_[mob].paddocks.size() == paddocks_.size()) {
    return;  // already has the run of the farm; do not reset its day count
  }
  mobs_[mob].paddocks.resize(paddocks_.size());
  for (std::size_t paddock = 0; paddock < paddocks_.size(); ++paddock) {
    mobs_[mob].paddocks[paddock] = paddock;
  }
  mobs_[mob].days_on_paddock = 0;
}

std::size_t Farm::mob_on(std::size_t paddock) const {
  if (paddock >= paddocks_.size()) {
    throw std::out_of_range("Farm::mob_on: no such paddock");
  }
  for (std::size_t i = 0; i < mobs_.size(); ++i) {
    const std::vector<std::size_t>& held = mobs_[i].paddocks;
    if (std::find(held.begin(), held.end(), paddock) != held.end()) {
      return i;
    }
  }
  return kNobody;
}

double Farm::paddock_cover_kg_dm_per_ha(std::size_t paddock) const {
  if (paddock >= paddocks_.size()) {
    throw std::out_of_range("Farm::paddock_cover_kg_dm_per_ha: no such paddock");
  }
  const std::vector<std::size_t>& cells = cells_of_paddock_[paddock];
  if (cells.empty()) {
    return 0.0;
  }
  double total = 0.0;
  for (const std::size_t index : cells) {
    total += grid_.cell(index % grid_.cols(), index / grid_.cols()).sward().cover_kg_dm();
  }
  return total / static_cast<double>(cells.size());
}

double Farm::paddock_offer_kg_dm(std::size_t paddock) const {
  if (paddock >= paddocks_.size()) {
    throw std::out_of_range("Farm::paddock_offer_kg_dm: no such paddock");
  }
  double total = 0.0;
  for (const std::size_t index : cells_of_paddock_[paddock]) {
    total += offer_per_hectare(grid_.cell(index % grid_.cols(), index / grid_.cols()));
  }
  return total * mask_.cell_area_hectares();
}

GrazingConditions Farm::conditions_on(const std::vector<std::size_t>& held, const Mob& mob) const {
  GrazingConditions ground;

  std::vector<std::size_t> cells;
  for (const std::size_t paddock : held) {
    const std::vector<std::size_t>& own = cells_of_paddock_[paddock];
    cells.insert(cells.end(), own.begin(), own.end());
  }
  const double hectares = static_cast<double>(cells.size()) * mask_.cell_area_hectares();

  // The movement equations take one figure for the paddock, so the mob walks on
  // its mean cover and mean slope rather than on each cell in turn. Averaging a
  // cost that is non-linear in slope understates a paddock with both flat and
  // steep ground; that is a simplification of the movement term only, and the
  // pasture underneath it is still modelled cell by cell.
  double cover_total = 0.0;
  for (const std::size_t index : cells) {
    cover_total += grid_.cell(index % grid_.cols(), index / grid_.cols()).sward().cover_kg_dm();
  }
  ground.pasture_mass_t_dm_per_ha =
      cells.empty() ? 0.0 : (cover_total / static_cast<double>(cells.size())) / 1000.0;
  ground.area_per_animal_ha = mob.head > 0 ? hectares / static_cast<double>(mob.head) : 0.0;

  if (!slope_degrees_.empty() && !cells.empty()) {
    double slope_total = 0.0;
    for (const std::size_t index : cells) {
      slope_total += slope_degrees_[index];
    }
    ground.slope_degrees = slope_total / static_cast<double>(cells.size());
  }

  // **The mean slope is classified once, rather than every cell being classified
  // and the answers averaged.** That is what OVERSEER does - its Eq. 54 averages
  // the topography integer over a block and looks the distances up from the
  // result - and it is the same simplification the cover and slope above already
  // make. On a paddock that is half terrace and half face it charges the walking
  // of neither half.
  const WalkingDistance walk = walking_distance_on(ground.slope_degrees);
  ground.horizontal_km_per_day = walk.horizontal_km_per_day;
  ground.vertical_km_per_day = walk.vertical_km_per_day;

  return ground;
}

FarmDay Farm::step(const DailyWeather& weather, const DietQuality& diet, BudgetLedger* ledger) {
  return step(weather, diet, {}, ledger);
}

FarmDay Farm::step(const DailyWeather& weather, const DietQuality& diet,
                   const std::vector<double>& supplement_kg_dm, BudgetLedger* ledger,
                   const std::vector<double>& irrigation_mm) {
  // Growth first: a mob eats what is standing when it walks in, which includes
  // what grew that morning.
  grid_.step(weather, ledger, irrigation_mm);

  FarmDay day;
  day.date = weather.date;
  day.mobs.reserve(mobs_.size());

  for (int& days : days_since_grazed_) {
    ++days;
  }
  for (FarmMob& farm_mob : mobs_) {
    ++farm_mob.days_on_paddock;
  }

  if (ledger != nullptr) {
    grazing_scratch_.reset();
  }

  for (FarmMob& farm_mob : mobs_) {
    // A mob eats over everything it has the run of: one paddock under rotation,
    // the whole farm under set stocking.
    std::vector<std::size_t> cells;
    for (const std::size_t paddock : farm_mob.paddocks) {
      const std::vector<std::size_t>& own = cells_of_paddock_[paddock];
      cells.insert(cells.end(), own.begin(), own.end());
    }

    MobDay mob_day;
    mob_day.mob_name = farm_mob.mob.name;
    mob_day.paddock = farm_mob.paddocks.front();

    const GrazingConditions ground = conditions_on(farm_mob.paddocks, farm_mob.mob);

    // **Demand is what the mob wants, not what it did yesterday.**
    //
    // daily_energy_requirement answers "to change weight at this rate, what
    // must it eat", so feeding it a *realised* rate inverts the meaning. A mob
    // that lost weight overnight would come back with a negative production
    // term, which reads as an energy credit and shrinks today's requirement -
    // and then it eats less, loses more, and asks for less again. A run of a
    // year took a mob from 55 kg to two grams that way, on a farm whose cover
    // never fell below 2000 kg DM/ha and which reported feed-limited on two
    // days out of 366. Well fed, and starved by arithmetic.
    //
    // So demand is computed against holding weight. Appetite does not fall
    // because an animal went short; if anything it rises, and modelling that
    // properly means intake capacity, which is a piece of work with its own
    // literature. Holding weight is the honest floor in the meantime.
    // Demand is computed at what the manager is FEEDING FOR, not at what the
    // mob happened to do yesterday. Yesterday's number is an outcome, and
    // feeding to it is circular - see Mob::target_gain_kg_per_day. Holding
    // weight remains the floor, for the reason above.
    AnimalState wanting = farm_mob.mob.state;
    wanting.liveweight_change_kg_per_day = std::max(0.0, farm_mob.mob.target_gain_kg_per_day);

    const EnergyRequirement need =
        daily_energy_requirement(farm_mob.mob.animal, wanting, diet, ground);

    const auto mob_index = static_cast<std::size_t>(&farm_mob - mobs_.data());
    const double supplement =
        mob_index < supplement_kg_dm.size() ? std::max(0.0, supplement_kg_dm[mob_index]) : 0.0;

    const double appetite = need.intake_kg_dm * static_cast<double>(farm_mob.mob.head);

    // **Bought feed substitutes for pasture; it does not add to it.** A mob fed
    // out in the morning grazes less, which is the whole point of feeding out -
    // it spares the sward. Adding the two instead sent a ewe from 55 kg to 101
    // over a year, which is not a thing sheep do.
    mob_day.supplement_kg_dm = std::min(supplement, appetite);
    mob_day.grazing.demand_kg_dm = appetite - mob_day.supplement_kg_dm;

    // What each cell has to give, and what the paddock has in total.
    const double cell_hectares = mask_.cell_area_hectares();
    double total_offer_kg = 0.0;
    for (const std::size_t index : cells) {
      total_offer_kg +=
          offer_per_hectare(grid_.cell(index % grid_.cols(), index / grid_.cols())) * cell_hectares;
    }
    mob_day.grazing.offered_kg_dm = total_offer_kg;

    const double to_eat_kg = std::min(mob_day.grazing.demand_kg_dm, total_offer_kg);

    // Spread over the cells in proportion to what each has above its residual:
    // stock take more from the parts of a paddock that carry more feed. See the
    // note on FarmMob - this is an assumption about feed, not a claim about
    // where animals walk.
    if (to_eat_kg > 0.0 && total_offer_kg > 0.0) {
      for (const std::size_t index : cells) {
        const std::size_t col = index % grid_.cols();
        const std::size_t row = index / grid_.cols();
        const double cell_offer_kg = offer_per_hectare(grid_.cell(col, row)) * cell_hectares;
        if (cell_offer_kg <= 0.0) {
          continue;
        }
        const double share_kg = to_eat_kg * (cell_offer_kg / total_offer_kg);
        const PastureSward::Defoliation taken =
            grid_.graze_cell(col, row, share_kg / cell_hectares);

        mob_day.grazing.grass_eaten_kg_dm += taken.grass_kg_dm * cell_hectares;
        mob_day.grazing.legume_eaten_kg_dm += taken.legume_kg_dm * cell_hectares;
        mob_day.grazing.nitrogen_removed_kg += taken.nitrogen_kg * cell_hectares;

        if (ledger != nullptr) {
          // Per hectare of the cell, matching what FarmletGrid folds: the grid
          // scales by one over the cell count, so everything reaching a farm
          // ledger has to be in per-hectare terms first.
          grazing_scratch_.record_outflow(Budget::DryMatter, "grazing_offtake",
                                          taken.total_kg_dm());
          grazing_scratch_.record_outflow(Budget::Nitrogen, "grazing_offtake", taken.nitrogen_kg);
        }
      }
    }

    mob_day.grazing.eaten_kg_dm =
        mob_day.grazing.grass_eaten_kg_dm + mob_day.grazing.legume_eaten_kg_dm;
    mob_day.grazing.intake_per_head_kg_dm =
        mob_day.grazing.eaten_kg_dm / static_cast<double>(farm_mob.mob.head);
    mob_day.grazing.feed_limited =
        mob_day.grazing.eaten_kg_dm < (mob_day.grazing.demand_kg_dm - 1e-9);

    // Every paddock the mob had the run of has been grazed, which is why set
    // stocking gives no rest: under it this resets all of them, every day.
    if (mob_day.grazing.eaten_kg_dm > 0.0) {
      for (const std::size_t paddock : farm_mob.paddocks) {
        days_since_grazed_[paddock] = 0;
      }
    }

    if (mob_day.supplement_kg_dm > 0.0) {
      mob_day.grazing.intake_per_head_kg_dm +=
          mob_day.supplement_kg_dm / static_cast<double>(farm_mob.mob.head);

      if (ledger != nullptr) {
        // Bought feed arrives from off the farm and is eaten the same day, so
        // it is both an inflow and an outflow. Recording only the first would
        // have dry matter accumulating in a sward it never touched; recording
        // neither would hide how much of this farm came out of a truck.
        //
        // The scratch ledger is folded by one over the cell count, and the farm
        // mean of S kilograms is S / (cells * cell area), so what goes in is
        // S / cell area.
        const double per_hectare = mob_day.supplement_kg_dm / mask_.cell_area_hectares();
        grazing_scratch_.record_inflow(Budget::DryMatter, "bought_feed", per_hectare);
        grazing_scratch_.record_outflow(Budget::DryMatter, "bought_feed_eaten", per_hectare);
      }
    }

    mob_day.response = advance_one_day(farm_mob.mob, mob_day.grazing, diet, ground);

    day.total_eaten_kg_dm += mob_day.grazing.eaten_kg_dm;
    day.total_supplement_kg_dm += mob_day.supplement_kg_dm;
    day.total_nitrogen_removed_kg += mob_day.grazing.nitrogen_removed_kg;
    day.any_mob_short = day.any_mob_short || mob_day.grazing.feed_limited;

    day.mobs.push_back(std::move(mob_day));
  }

  if (ledger != nullptr) {
    ledger->add_scaled(grazing_scratch_, 1.0 / static_cast<double>(grid_.cell_count()));
  }

  return day;
}

void Farm::set_opening_stocks(BudgetLedger& ledger) const {
  grid_.set_opening_stocks(ledger);
}

}  // namespace paddock::core
