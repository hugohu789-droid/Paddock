// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <paddock/config/ScenarioRun.hpp>
#include <paddock/core/Farm.hpp>
#include <paddock/core/Farmer.hpp>

namespace paddock::config {

namespace {

/// The mean of a raster, or zero when it is empty.
double mean_of(const core::Raster<double>& raster) {
  if (raster.empty()) {
    return 0.0;
  }
  double total = 0.0;
  for (const double value : raster.values()) {
    total += value;
  }
  return total / static_cast<double>(raster.size());
}

}  // namespace

int RunSummary::days_water_stressed() const {
  // **Below one, not below some threshold somebody picked.** FAO-56 Eq. 84 puts
  // the coefficient at exactly one while the root zone still holds readily
  // available water, so anything under it is the model saying growth was held
  // back that day. A margin here would be a second opinion about when a farm is
  // dry, and the model has already given the first.
  return static_cast<int>(
      std::count_if(water_stress.begin(), water_stress.end(),
                    [](double coefficient) { return coefficient < 1.0; }));
}

double RunSummary::mean_cover_kg_dm_per_ha() const {
  if (cover_kg_dm_per_ha.empty()) {
    return 0.0;
  }
  const double total = std::accumulate(cover_kg_dm_per_ha.begin(), cover_kg_dm_per_ha.end(), 0.0);
  return total / static_cast<double>(cover_kg_dm_per_ha.size());
}

double RunSummary::lowest_cover_kg_dm_per_ha() const {
  if (cover_kg_dm_per_ha.empty()) {
    return 0.0;
  }
  return *std::min_element(cover_kg_dm_per_ha.begin(), cover_kg_dm_per_ha.end());
}

double RunSummary::highest_cover_kg_dm_per_ha() const {
  if (cover_kg_dm_per_ha.empty()) {
    return 0.0;
  }
  return *std::max_element(cover_kg_dm_per_ha.begin(), cover_kg_dm_per_ha.end());
}

double RunSummary::bought_feed_kg_dm() const {
  double total = 0.0;
  for (const core::FeedPurchase& purchase : purchases) {
    total += purchase.kg_dm;
  }
  return total;
}

int RunSummary::days_feed_was_bought() const {
  int days = 0;
  core::Date previous{};
  for (const core::FeedPurchase& purchase : purchases) {
    if (!(purchase.date == previous)) {
      ++days;
      previous = purchase.date;
    }
  }
  return days;
}

RunSummary run_managed_scenario(const ScenarioBundle& bundle, const core::DietQuality& diet,
                                std::string label) {
  if (!bundle.management.has_value()) {
    throw std::runtime_error(
        "scenario '" + bundle.name +
        "' names no [management], so there is nothing to say what its farmer would not allow. "
        "Add the section, or run it with a policy of your own.");
  }
  return run_managed_scenario(bundle, *bundle.management, diet, std::move(label));
}

RunSummary run_managed_scenario(const ScenarioBundle& bundle, const core::ManagementPolicy& policy,
                                const core::DietQuality& diet, std::string label) {
  return run_managed_scenario(bundle, policy, diet, std::move(label), DayObserver{});
}

RunSummary run_managed_scenario(const ScenarioBundle& bundle, const core::ManagementPolicy& policy,
                                const core::DietQuality& diet, std::string label,
                                const DayObserver& each_day,
                                const core::IrrigationPolicy& irrigation,
                                const core::IrrigationSystem& system) {
  core::Farm farm = bundle.make_farm();

  // The bundle's own calendar, when it has one.
  //
  // This used to be a harmless placeholder, on the grounds that a managing
  // farmer decides from cover and never reads a calendar. That stopped being
  // true when the farmer gained a preference: `FollowCalendar` reads exactly
  // this, and given the placeholder it would have followed a plan nobody wrote
  // - set stocking for the whole year - while the manifest's own
  // [[grazing_period]] sat unread beside it.
  //
  // Inert for every other preference, which is why the change costs nothing:
  // manage() builds its own one-day calendar when it rotates and does not touch
  // this one otherwise.
  core::Farmer farmer(bundle.grazing.empty()
                          ? whole_run_calendar(bundle.range, core::GrazingSystem::SetStocking, 0, 0)
                          : bundle.grazing);
  farmer.set_policy(policy);

  RunSummary summary;
  summary.label = std::move(label);
  farm.set_opening_stocks(summary.ledger);

  const core::WeatherSeries weather = bundle.weather->fetch(bundle.range);
  std::vector<bool> went_short(farm.mobs().size(), false);
  std::vector<double> supplement;

  // The schedule reads how dry the ground is and decides; the farm applies
  // what it is handed. Neither knows about the other's job.
  core::IrrigationSchedule schedule(irrigation, system, farm.grid().cell_count());
  summary.irrigation_mm.reserve(weather.records.size());

  for (const core::DailyWeather& day : weather.records) {
    const core::Farmer::Day decisions = farmer.manage(farm, day.date, diet, went_short, supplement);

    summary.moves += static_cast<int>(decisions.moves.size());
    summary.short_spells += decisions.short_spells;
    summary.grazings_extended += decisions.grazings_extended;
    summary.system_each_day.push_back(decisions.chosen_system);
    summary.purchases.insert(summary.purchases.end(), decisions.purchases.begin(),
                             decisions.purchases.end());

    const core::Raster<double> dryness = farm.grid().depletion_mm();
    const std::vector<double>& water =
        schedule.decide(dryness.values(), farm.grid().total_available_water_mm());
    summary.irrigation_mm.push_back(schedule.last_mean_mm());
    // Averaged over the farm, like the cover beside it. One number for a day is
    // what a season-long comparison reads; where the stress fell is the map's
    // job.
    summary.water_stress.push_back(mean_of(farm.grid().water_stress()));

    const core::FarmDay farm_day = farm.step(day, diet, supplement, &summary.ledger, water);
    if (farm_day.any_mob_short) {
      ++summary.days_short;
    }
    for (std::size_t i = 0; i < farm_day.mobs.size() && i < went_short.size(); ++i) {
      went_short[i] = farm_day.mobs[i].grazing.feed_limited;
    }
    summary.eaten_kg_dm += farm_day.total_eaten_kg_dm;

    summary.dates.push_back(day.date);
    summary.weather.push_back(day);
    summary.cover_kg_dm_per_ha.push_back(farm.grid().mean_cover_kg_dm());
    summary.liveweight_kg.push_back(farm.mobs().front().mob.state.liveweight_kg);
    summary.paddock_of_first_mob.push_back(static_cast<int>(farm.mobs().front().paddocks.front()));

    if (each_day) {
      each_day(farm, farm_day);
    }
  }

  summary.irrigation = schedule.tally();
  summary.closing_cover_kg_dm = farm.grid().mean_cover_kg_dm();
  summary.closing_nitrogen_kg = farm.grid().mean_total_nitrogen_kg();
  summary.closing_water_mm = farm.grid().mean_soil_water_mm();
  return summary;
}

core::GrazingCalendar whole_run_calendar(const core::DateRange& run, core::GrazingSystem system,
                                         int maximum_graze_days, int minimum_spell_days) {
  core::GrazingRule rule;
  rule.system = system;
  rule.maximum_graze_days = maximum_graze_days;
  rule.minimum_spell_days = minimum_spell_days;
  return core::GrazingCalendar(
      std::vector<core::GrazingPeriod>{core::GrazingPeriod{"whole run", run, rule}});
}

RunSummary run_scenario(const ScenarioBundle& bundle, const core::GrazingCalendar& calendar,
                        const core::DietQuality& diet, std::string label) {
  const std::string calendar_error = calendar.validation_error(bundle.range);
  if (!calendar_error.empty()) {
    throw std::runtime_error("run_scenario: the calendar does not cover the run: " +
                             calendar_error);
  }

  core::Farm farm = bundle.make_farm();
  core::Farmer farmer(calendar);

  RunSummary summary;
  summary.label = std::move(label);
  farm.set_opening_stocks(summary.ledger);

  const core::WeatherSeries weather = bundle.weather->fetch(bundle.range);
  summary.dates.reserve(weather.records.size());
  summary.cover_kg_dm_per_ha.reserve(weather.records.size());
  summary.liveweight_kg.reserve(weather.records.size());
  summary.paddock_of_first_mob.reserve(weather.records.size());

  std::vector<bool> went_short(farm.mobs().size(), false);
  for (const core::DailyWeather& day : weather.records) {
    const core::Farmer::Day decisions = farmer.decide(farm, day.date, went_short);
    summary.moves += static_cast<int>(decisions.moves.size());
    summary.short_spells += decisions.short_spells;
    summary.grazings_extended += decisions.grazings_extended;

    const core::FarmDay farm_day = farm.step(day, diet, &summary.ledger);
    if (farm_day.any_mob_short) {
      ++summary.days_short;
    }
    for (std::size_t i = 0; i < farm_day.mobs.size() && i < went_short.size(); ++i) {
      went_short[i] = farm_day.mobs[i].grazing.feed_limited;
    }
    summary.eaten_kg_dm += farm_day.total_eaten_kg_dm;

    summary.dates.push_back(day.date);
    summary.weather.push_back(day);
    summary.cover_kg_dm_per_ha.push_back(farm.grid().mean_cover_kg_dm());
    summary.liveweight_kg.push_back(farm.mobs().front().mob.state.liveweight_kg);
    summary.paddock_of_first_mob.push_back(static_cast<int>(farm.mobs().front().paddocks.front()));
  }

  summary.closing_cover_kg_dm = farm.grid().mean_cover_kg_dm();
  summary.closing_nitrogen_kg = farm.grid().mean_total_nitrogen_kg();
  summary.closing_water_mm = farm.grid().mean_soil_water_mm();
  return summary;
}

RunSummary run_scenario(const ScenarioBundle& bundle, const core::DietQuality& diet,
                        std::string label) {
  return run_scenario(bundle, bundle.grazing, diet, std::move(label));
}

}  // namespace paddock::config
