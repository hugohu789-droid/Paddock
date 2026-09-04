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

std::size_t Farm::add_mob(Mob mob, std::size_t paddock, bool grazes_ahead) {
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
  mobs_.back().grazes_ahead = grazes_ahead;
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
  // A mob with a target of its own keeps it: the farm policy speaks for the
  // mobs that have not been spoken for.
  //
  // Bound to a reference rather than subscripted three times, because the guard
  // and the read were separate expressions and nothing tied them to the same
  // object - which is a fair complaint from an analyser and reads better fixed.
  FarmMob& entry = mobs_[mob];
  entry.mob.target_gain_kg_per_day = entry.own_target_gain_kg_per_day.value_or(kg_per_day);
}

void Farm::set_own_target_gain(std::size_t mob, double kg_per_day) {
  if (mob >= mobs_.size()) {
    throw std::out_of_range("Farm::set_own_target_gain: no such mob");
  }
  mobs_[mob].own_target_gain_kg_per_day = kg_per_day;
  mobs_[mob].mob.target_gain_kg_per_day = kg_per_day;
}

void Farm::set_excreta(const ExcretaParameters& excreta) {
  excreta_ = excreta;
}

double Farm::cut_for_conservation(double leave_kg_dm_per_ha, BudgetLedger* ledger) {
  const double cut_per_hectare = grid_.cut_every_cell_to(leave_kg_dm_per_ha);
  const double hectares = mask_.cell_area_hectares() * static_cast<double>(grid_.cell_count());
  const double taken = cut_per_hectare * hectares;

  if (ledger != nullptr && cut_per_hectare > 0.0) {
    // Out of the standing crop, per hectare, the way everything else the grid
    // folds is measured. It comes back as bought_feed does when it is fed out.
    ledger->record_outflow(Budget::DryMatter, "conserved_cut", cut_per_hectare);
  }
  return taken;
}

void Farm::set_mob_head(std::size_t mob, int head) {
  if (mob >= mobs_.size()) {
    return;
  }
  mobs_[mob].mob.head = std::max(0, head);
}

void Farm::set_mob_reproduction(std::size_t mob, int days_pregnant, int days_lactating,
                                double young) {
  if (mob >= mobs_.size()) {
    return;
  }
  mobs_[mob].mob.state.days_pregnant = std::max(0, days_pregnant);
  mobs_[mob].mob.state.days_lactating = std::max(0, days_lactating);
  mobs_[mob].mob.state.young = std::max(0.0, young);
}

void Farm::set_mob_state(std::size_t mob, const AnimalState& state) {
  if (mob >= mobs_.size()) {
    return;
  }
  mobs_[mob].mob.state = state;
}

GrazingConditions Farm::conditions_for(std::size_t mob) const {
  if (mob >= mobs_.size()) {
    return {};
  }
  return conditions_on(mobs_[mob].paddocks, mobs_[mob].mob);
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

  // **Leaders before followers.** Mob order in the vector is the order they
  // were declared, which is not the order they walk into a paddock: young stock
  // lead and take the leaf, the ewes follow and clean up. Grazing them in
  // declaration order put the lambs behind the ewes and left them the residual.
  std::vector<std::size_t> order(mobs_.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(), [this](std::size_t lhs, std::size_t rhs) {
    return static_cast<int>(mobs_[lhs].grazes_ahead) > static_cast<int>(mobs_[rhs].grazes_ahead);
  });

  for (const std::size_t mob_position : order) {
    FarmMob& farm_mob = mobs_[mob_position];
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
    // **An empty mob is skipped, not fed.** A mob stocked by a flock is empty
    // out of season, and dividing the day's intake by no animals hands the
    // liveweight model a NaN that never washes out - the mob comes back next
    // season weighing not-a-number.
    if (farm_mob.mob.head <= 0) {
      day.mobs.push_back(mob_day);
      continue;
    }

    AnimalState wanting = farm_mob.mob.state;
    wanting.liveweight_change_kg_per_day = std::max(0.0, farm_mob.mob.target_gain_kg_per_day);

    const EnergyRequirement need =
        daily_energy_requirement(farm_mob.mob.animal, wanting, diet, ground);

    const std::size_t mob_index = mob_position;
    const double supplement =
        mob_index < supplement_kg_dm.size() ? std::max(0.0, supplement_kg_dm[mob_index]) : 0.0;

    // **What the mob is being fed FOR.** This carries the target gain, and that
    // is all it is: an intent that sizes the day's demand and tells the farmer
    // whether to put feed out. It is not a ceiling on anything and it cannot
    // make an animal eat - the ceiling below is what the animal is.
    const double demand_kg_dm = need.intake_kg_dm * static_cast<double>(farm_mob.mob.head);
    mob_day.supplement_offered_kg_dm = supplement;

    // **Step one: what this animal can physiologically eat today**, before
    // anything about what is in front of it. GrazPlan Eq. 2 through
    // `potential_intake_kg_dm`, carrying size, condition and lactation.
    //
    // **It does not depend on the target gain**, which is the whole point: it
    // is what the animal is, not what the farmer wants. A species with no
    // appetite parameters falls back to its own requirement, so an
    // unparameterised animal still eats rather than starving on a missing
    // number.
    const double appetite_per_head =
        potential_intake_kg_dm(farm_mob.mob.animal, farm_mob.mob.state);
    const double capacity_kg_dm = appetite_per_head > 0.0
                                      ? appetite_per_head * static_cast<double>(farm_mob.mob.head)
                                      : demand_kg_dm;
    mob_day.intake_capacity_kg_dm = capacity_kg_dm;

    // **Step two: the trough, bounded by that capacity.**
    //
    // Feeding out happens in the morning and a mob that has eaten grazes less,
    // which is why bought feed substitutes for pasture rather than adding to
    // it - adding the two sent a ewe from 55 kg to 101 over a year. So the
    // trough is served first, and two things bound it: what the mob is being
    // fed **for**, and what it can physically hold.
    //
    // **The second of those is the fix.** Until it was written, bought feed was
    // the one intake nothing bounded: capped only at the demand, and the demand
    // carries the target gain, so a farmer who asked for half a kilogram a day
    // bought his way there - E77 measured 182.49 kg over a year against a
    // target of 182.5, to the decimal, which is arithmetic and not an animal.
    // Whatever is offered beyond this is refused and never reaches a budget.
    mob_day.supplement_kg_dm = std::min({supplement, demand_kg_dm, capacity_kg_dm});
    mob_day.grazing.demand_kg_dm = demand_kg_dm - mob_day.supplement_kg_dm;

    // What each cell has to give, and what the paddock has in total. The mean
    // standing cover comes with it, because how much of what it wants a mob can
    // physically harvest depends on how tall the sward is and not only on
    // whether there is any of it.
    const double cell_hectares = mask_.cell_area_hectares();
    double total_offer_kg = 0.0;
    double standing_kg_dm_per_ha = 0.0;
    for (const std::size_t index : cells) {
      const Farmlet& cell = grid_.cell(index % grid_.cols(), index / grid_.cols());
      total_offer_kg += offer_per_hectare(cell) * cell_hectares;
      standing_kg_dm_per_ha += cell.sward().cover_kg_dm();
    }
    if (!cells.empty()) {
      standing_kg_dm_per_ha /= static_cast<double>(cells.size());
    }
    mob_day.grazing.offered_kg_dm = total_offer_kg;

    // **Intake capacity** (E71): what the mob wants, cut by what a sward this
    // short will actually yield to it. A ewe on a bare paddock takes smaller
    // bites and grazes longer to make them up, and past a point cannot - so her
    // intake falls away well before the grass runs out, which is a curve where
    // this model used to have a cliff.
    //
    // The appetite carries GrazPlan's lactation and condition factors, and it
    // has to: the availability term sits below one at any cover a real farm
    // carries, so against a bare requirement it would put every mob permanently
    // short. A milking ewe wants half again what a dry one does, which is the
    // headroom this needs to work at all.
    mob_day.grazing.relative_intake = relative_intake(farm_mob.mob.animal, standing_kg_dm_per_ha);

    // **Step three: what is left of the appetite, and step four: pasture into
    // it.**
    //
    // Two different limits, composed rather than conflated. The room left after
    // the trough is `capacity - supplement`, which is a stomach. The most she
    // can harvest is `capacity * relative_intake`, which is a mouth: on a short
    // sward she takes smaller bites and grazes longer to make them up, and past
    // a point cannot. Grazing is under both, and then under what is actually
    // growing there.
    mob_day.grazing.capacity_kg_dm =
        appetite_per_head > 0.0
            ? std::max(0.0, std::min(capacity_kg_dm - mob_day.supplement_kg_dm,
                                     capacity_kg_dm * mob_day.grazing.relative_intake))
            : mob_day.grazing.demand_kg_dm;

    const double to_eat_kg =
        std::min({mob_day.grazing.demand_kg_dm, mob_day.grazing.capacity_kg_dm, total_offer_kg});

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

    // Short when what it ate - grass and trough together - did not reach what it
    // was being fed for.
    mob_day.grazing.feed_limited = mob_day.total_intake_kg_dm() < (demand_kg_dm - 1e-9);

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

    // **What the stock ate comes back, less what they kept.** Nitrogen used to
    // leave with the grazed dry matter and never return, so a grazed farm ran
    // its soil down and the budget closed anyway - which is what an outflow
    // with no matching inflow does. A grazing animal keeps about a tenth and
    // gives the rest back within days, and in New Zealand that excreta, not
    // fertiliser, is the primary source of nitrate leaching.
    //
    // **Bought feed's nitrogen counts too.** It comes through the gate, the
    // stock eat it and excrete it onto the paddock like anything else; on this
    // farm in a dry year it is half of what they eat.
    const double supplement_nitrogen =
        mob_day.supplement_kg_dm * excreta_.supplement_nitrogen_fraction;
    const double nitrogen_eaten = mob_day.grazing.nitrogen_removed_kg + supplement_nitrogen;

    const Excreta given_back = excreta_from_intake(
        nitrogen_eaten, mob_day.grazing.eaten_kg_dm + mob_day.supplement_kg_dm,
        mob_day.response.liveweight_change_kg, static_cast<double>(farm_mob.mob.head), excreta_);

    if (given_back.total_kg() > 0.0 && !cells.empty()) {
      // Spread evenly over the ground the mob had the run of. Real stock camp,
      // and a camped paddock leaches more from less of itself - modelling that
      // needs a source for where they camp, which this project does not have.
      const auto share = static_cast<double>(cells.size());
      const double per_cell_urine = given_back.urine_nitrogen_kg / cell_hectares / share;
      const double per_cell_dung = given_back.dung_nitrogen_kg / cell_hectares / share;

      for (const std::size_t index : cells) {
        grid_.return_excreta_to_cell(index % grid_.cols(), index / grid_.cols(), per_cell_urine,
                                     per_cell_dung, excreta_);

        if (ledger != nullptr) {
          // **One inflow, not two.** Grazing books the nitrogen out of these
          // pools and into an animal this model does not carry; excreta books
          // what the animal gives back in. The difference between them is
          // exactly right without any further entries: what the stock kept
          // stays out, and the nitrogen that came in on a truck as supplement
          // arrives here, because that is where it physically arrives.
          grazing_scratch_.record_inflow(Budget::Nitrogen, "excreta_returned",
                                         per_cell_urine + per_cell_dung);
        }
      }
    }

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
