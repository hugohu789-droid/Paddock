// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <stdexcept>
#include <string>

#include <paddock/core/Grazing.hpp>

namespace paddock::core {

std::string Mob::validation_error() const {
  if (name.empty()) {
    return "a mob needs a name";
  }
  if (head <= 0) {
    return name + ": a mob needs at least one animal";
  }
  const std::string animal_error = animal.validation_error();
  if (!animal_error.empty()) {
    return name + ": " + animal_error;
  }
  if (state.liveweight_kg <= 0.0) {
    return name + ": liveweight must be positive";
  }
  return {};
}

double GrazingDay::satisfaction() const noexcept {
  if (demand_kg_dm <= 0.0) {
    return 1.0;
  }
  return eaten_kg_dm / demand_kg_dm;
}

GrazingDay graze(PastureSward& sward, double paddock_hectares, const Mob& mob,
                 const DietQuality& diet, const GrazingConditions& ground, BudgetLedger* ledger) {
  const std::string mob_error = mob.validation_error();
  if (!mob_error.empty()) {
    throw std::invalid_argument("graze: " + mob_error);
  }
  if (paddock_hectares <= 0.0) {
    throw std::invalid_argument("graze: a paddock needs a positive area");
  }

  // Throws for an impossible diet, which is where that check belongs.
  //
  // Fed for the target, not for yesterday's outcome. The same reasoning as
  // Farm::step, and the two paths have to agree: a single paddock and a whole
  // farm must not offer the same mob different amounts of the same feed.
  AnimalState wanting = mob.state;
  wanting.liveweight_change_kg_per_day = std::max(0.0, mob.target_gain_kg_per_day);
  const EnergyRequirement need = daily_energy_requirement(mob.animal, wanting, diet, ground);

  GrazingDay day;
  day.demand_kg_dm = need.intake_kg_dm * static_cast<double>(mob.head);

  // The sward is one hectare; the mob's demand is spread over the paddock.
  const double demand_per_hectare = day.demand_kg_dm / paddock_hectares;

  // Measured before the bite is taken, and it is not the same as what gets
  // eaten: on a paddock with feed to spare the mob leaves most of it standing.
  const SwardParameters& sward_parameters = sward.parameters();
  const double offered_per_hectare =
      std::max(0.0, sward.grass_kg_dm() - sward_parameters.grass.residual_kg_dm_per_ha) +
      std::max(0.0, sward.legume_kg_dm() - sward_parameters.legume.residual_kg_dm_per_ha);
  day.offered_kg_dm = offered_per_hectare * paddock_hectares;

  const PastureSward::Defoliation taken = sward.remove_green_dry_matter(demand_per_hectare);

  day.grass_eaten_kg_dm = taken.grass_kg_dm * paddock_hectares;
  day.legume_eaten_kg_dm = taken.legume_kg_dm * paddock_hectares;
  day.eaten_kg_dm = taken.total_kg_dm() * paddock_hectares;
  day.nitrogen_removed_kg = taken.nitrogen_kg * paddock_hectares;
  day.intake_per_head_kg_dm = day.eaten_kg_dm / static_cast<double>(mob.head);

  day.feed_limited = day.eaten_kg_dm < (day.demand_kg_dm - 1e-9);

  if (ledger != nullptr) {
    // Outflows rather than transfers: both leave the pools this model tracks.
    // Dung and urine are not modelled, so the nitrogen does not come back - see
    // the note on graze() and docs/validation/verify.md.
    ledger->record_outflow(Budget::DryMatter, "grazing_offtake", day.eaten_kg_dm);
    ledger->record_outflow(Budget::Nitrogen, "grazing_offtake", day.nitrogen_removed_kg);
  }

  return day;
}

LiveweightResponse advance_one_day(Mob& mob, const GrazingDay& day, const DietQuality& diet,
                                   const GrazingConditions& ground) {
  const std::string mob_error = mob.validation_error();
  if (!mob_error.empty()) {
    throw std::invalid_argument("advance_one_day: " + mob_error);
  }

  const LiveweightResponse response =
      liveweight_response(mob.animal, mob.state, diet, ground, day.intake_per_head_kg_dm);

  mob.state.liveweight_kg += response.liveweight_change_kg;
  mob.state.liveweight_change_kg_per_day = response.liveweight_change_kg;
  mob.state.age_days += 1.0;

  // An animal cannot weigh nothing. Starvation and mortality are not modelled -
  // see liveweight_response - so this floor exists to keep the arithmetic
  // meaningful rather than to represent a biological limit, and a run that
  // reaches it is reporting a feed budget that does not work.
  if (mob.state.liveweight_kg <= 0.0) {
    throw std::runtime_error("advance_one_day: mob '" + mob.name +
                             "' has been starved to zero liveweight; the feed budget for this "
                             "run does not work, and mortality is not modelled");
  }

  return response;
}

}  // namespace paddock::core
