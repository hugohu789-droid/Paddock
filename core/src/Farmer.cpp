// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <paddock/core/Farmer.hpp>

namespace paddock::core {

Farmer::Farmer(GrazingCalendar calendar) : calendar_(std::move(calendar)) {}

std::string to_string(FeedPurchase::Reason reason) {
  switch (reason) {
    case FeedPurchase::Reason::PaddockShort:
      return "the paddock could not meet demand";
    case FeedPurchase::Reason::ProtectingCover:
      return "cover was at the floor";
  }
  return "unknown";
}

std::string ManagementPolicy::validation_error() const {
  if (minimum_cover_kg_dm_per_ha <= 0.0) {
    return "the cover floor must be positive";
  }
  if (rotation_cover_threshold_kg_dm_per_ha < minimum_cover_kg_dm_per_ha) {
    return "the cover at which rotation becomes affordable cannot be below the floor the "
           "farmer is protecting";
  }
  if (supplement_me_mj_per_kg_dm <= 0.0) {
    return "bought feed with no energy in it would need an infinite amount of it";
  }
  if (maximum_graze_days <= 0 || minimum_spell_days <= maximum_graze_days) {
    return "the rotation parameters do not describe a rotation";
  }
  return {};
}

void Farmer::set_policy(ManagementPolicy policy) {
  const std::string error = policy.validation_error();
  if (!error.empty()) {
    throw std::invalid_argument("Farmer::set_policy: " + error);
  }
  policy_ = policy;
}

GrazingSystem Farmer::system_for(const Farm& farm) const {
  // Rotation concentrates stock onto one paddock, so it needs a farm carrying
  // enough to make that paddock worth standing on. When the farm is short, a
  // farmer spreads out instead - which is set stocking, and is why the source
  // names it for lambing, when demand is at its highest.
  double total = 0.0;
  for (std::size_t paddock = 0; paddock < farm.paddocks().size(); ++paddock) {
    total += farm.paddock_cover_kg_dm_per_ha(paddock);
  }
  const double mean =
      farm.paddocks().empty() ? 0.0 : total / static_cast<double>(farm.paddocks().size());

  return mean >= policy_.rotation_cover_threshold_kg_dm_per_ha ? GrazingSystem::Rotational
                                                               : GrazingSystem::SetStocking;
}

Farmer::Day Farmer::manage(Farm& farm, const Date& date, const DietQuality& diet,
                           const std::vector<bool>& went_short,
                           std::vector<double>& supplement_kg_dm) {
  Day day;
  day.chosen_system = system_for(farm);

  // Put the stock where the chosen system wants them. Under set stocking that
  // is the whole farm; under rotation it is one paddock each, moved on the
  // graze length or early if they ran out.
  if (day.chosen_system == GrazingSystem::SetStocking) {
    for (std::size_t index = 0; index < farm.mobs().size(); ++index) {
      farm.spread_mob(index);
    }
    day.system = GrazingSystem::SetStocking;
  } else {
    GrazingRule rule;
    rule.system = GrazingSystem::Rotational;
    rule.maximum_graze_days = policy_.maximum_graze_days;
    rule.minimum_spell_days = policy_.minimum_spell_days;

    const GrazingCalendar one_day(
        std::vector<GrazingPeriod>{GrazingPeriod{"chosen", DateRange{date, date}, rule}});
    Farmer follower(one_day);
    day = follower.decide(farm, date, went_short);
    day.chosen_system = GrazingSystem::Rotational;
  }

  // Then work out what the pasture cannot supply, and buy it.
  supplement_kg_dm.assign(farm.mobs().size(), 0.0);
  if (!policy_.may_buy_feed) {
    return day;
  }

  double farm_cover = 0.0;
  for (std::size_t paddock = 0; paddock < farm.paddocks().size(); ++paddock) {
    farm_cover += farm.paddock_cover_kg_dm_per_ha(paddock);
  }
  farm_cover =
      farm.paddocks().empty() ? 0.0 : farm_cover / static_cast<double>(farm.paddocks().size());
  const bool at_the_floor = farm_cover <= policy_.minimum_cover_kg_dm_per_ha;

  for (std::size_t index = 0; index < farm.mobs().size(); ++index) {
    const FarmMob& farm_mob = farm.mobs()[index];

    // What the mob wants, at the gain the farmer is aiming for. Stock are sold
    // by the kilogram, so this is a target rather than survival.
    AnimalState wanted = farm_mob.mob.state;
    wanted.liveweight_change_kg_per_day = std::max(0.0, policy_.target_liveweight_gain_kg_per_day);

    GrazingConditions ground;
    ground.pasture_mass_t_dm_per_ha = farm_cover / 1000.0;

    const EnergyRequirement need =
        daily_energy_requirement(farm_mob.mob.animal, wanted, diet, ground);
    const double demand_kg_dm = need.intake_kg_dm * static_cast<double>(farm_mob.mob.head);

    // What the ground the mob is standing on can give it without taking the
    // farm below the floor the farmer is protecting.
    double available_kg_dm = 0.0;
    for (const std::size_t paddock : farm_mob.paddocks) {
      available_kg_dm += farm.paddock_offer_kg_dm(paddock);
    }

    // At the floor the farmer stops asking the pasture for anything and feeds
    // the mob entirely. Because bought feed substitutes for grazing rather than
    // adding to it, buying the whole demand is what takes the grazing pressure
    // off - which is the point of doing it.
    if (at_the_floor) {
      available_kg_dm = 0.0;
    }

    const double shortfall_kg_dm = demand_kg_dm - available_kg_dm;
    if (shortfall_kg_dm <= 0.0) {
      continue;
    }

    // Bought feed carries less energy than pasture, so it takes more of it.
    const double as_supplement_kg_dm = shortfall_kg_dm * diet.metabolisable_energy_mj_per_kg_dm /
                                       policy_.supplement_me_mj_per_kg_dm;

    supplement_kg_dm[index] = as_supplement_kg_dm;

    FeedPurchase purchase;
    purchase.date = date;
    purchase.mob = index;
    purchase.mob_name = farm_mob.mob.name;
    purchase.kg_dm = as_supplement_kg_dm;
    purchase.reason =
        at_the_floor ? FeedPurchase::Reason::ProtectingCover : FeedPurchase::Reason::PaddockShort;
    day.purchases.push_back(std::move(purchase));
  }

  return day;
}

Farmer::Day Farmer::decide(Farm& farm, const Date& date) {
  return decide(farm, date, {});
}

Farmer::Day Farmer::decide(Farm& farm, const Date& date, const std::vector<bool>& went_short) {
  Day day;

  const GrazingRule& rule = calendar_.rule_on(date);
  day.system = rule.system;

  // Under set stocking the stock get the run of the whole farm. That is what
  // the system is - Smith and Dawson (1976) say of lambing that "the whole of
  // the farm area should be used for grazing" - and it is *not* the same as
  // leaving a mob where it stands. A mob confined to the paddock it happened to
  // be on would starve there while the rest of the farm grew, which is exactly
  // what the first run of the year-long scenario did.
  if (rule.system == GrazingSystem::SetStocking) {
    for (std::size_t index = 0; index < farm.mobs().size(); ++index) {
      farm.spread_mob(index);
    }
    return day;
  }

  // Coming out of set stocking, a mob has the run of everything. It has to be
  // put somewhere before a rotation can start.
  for (std::size_t index = 0; index < farm.mobs().size(); ++index) {
    if (farm.mobs()[index].paddocks.size() > 1) {
      farm.move_mob(index, farm.mobs()[index].paddocks.front());
    }
  }

  const std::vector<FarmMob>& mobs = farm.mobs();
  const std::vector<int>& rest = farm.days_since_grazed();

  for (std::size_t index = 0; index < mobs.size(); ++index) {
    // The graze length is a maximum, not a period to serve out. A mob that
    // emptied its paddock yesterday moves today, whatever the calendar says.
    const bool hungry = index < went_short.size() && went_short[index];
    const bool grazed_long_enough = mobs[index].days_on_paddock >= rule.maximum_graze_days;
    if (!hungry && !grazed_long_enough) {
      continue;
    }

    // The best free paddock is the one that has rested longest. Occupied ones
    // are out: a paddock carries at most one mob here, because two mobs sharing
    // ground needs a rule for how they divide the feed and there is no source
    // for one.
    std::size_t best = Farm::kNobody;
    int best_rest = -1;
    for (std::size_t paddock = 0; paddock < rest.size(); ++paddock) {
      if (paddock == mobs[index].paddock()) {
        continue;
      }
      if (farm.mob_on(paddock) != Farm::kNobody) {
        continue;
      }
      if (rest[paddock] > best_rest) {
        best_rest = rest[paddock];
        best = paddock;
      }
    }

    if (best == Farm::kNobody) {
      // Nowhere to go: every other paddock is occupied. The mob stays and the
      // grazing lengthens, which is the first of the two things Smith and
      // Dawson say happens when a farm has too few paddocks for its mobs.
      ++day.grazings_extended;
      continue;
    }

    MobMove move;
    move.mob = index;
    move.from = mobs[index].paddock();
    move.to = best;
    move.days_grazed = mobs[index].days_on_paddock;
    move.rest_days = best_rest;
    move.moved_early = hungry && !grazed_long_enough;

    // The mob moves whether or not the paddock has had its spell, because the
    // stock have to eat. Going anyway with the shortfall recorded is what makes
    // the shuffle measurable rather than hidden.
    move.spell_was_short = best_rest < rule.minimum_spell_days;
    if (move.spell_was_short) {
      ++day.short_spells;
    }

    farm.move_mob(index, best);
    day.moves.push_back(move);
  }

  return day;
}

}  // namespace paddock::core
