// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <numeric>
#include <optional>
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
  return static_cast<int>(std::count_if(water_stress.begin(), water_stress.end(),
                                        [](double coefficient) { return coefficient < 1.0; }));
}

double RunSummary::nitrate_leached_total_kg_per_ha() const {
  // Kahan, because a year of small daily numbers summed naively drifts, and
  // this one gets compared against a regulatory limit.
  core::KahanSum total;
  for (const double day : nitrate_leached_kg_per_ha) {
    total.add(day);
  }
  return total.value();
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
  // **A bundle that names an irrigation rule is run under it.** This overload is
  // the one that means "run this scenario as it describes itself", so leaving
  // the rule behind here would make an irrigated bundle quietly rain-fed - the
  // exact failure [management] was added to stop.
  return run_managed_scenario(bundle, *bundle.management, diet, std::move(label), DayObserver{},
                              bundle.irrigation.value_or(core::IrrigationPolicy{}),
                              bundle.irrigation_system);
}

RunSummary run_managed_scenario(const ScenarioBundle& bundle, const core::ManagementPolicy& policy,
                                const core::DietQuality& diet, std::string label) {
  return run_managed_scenario(bundle, policy, diet, std::move(label), DayObserver{},
                              bundle.irrigation.value_or(core::IrrigationPolicy{}),
                              bundle.irrigation_system);
}

namespace {

/// The money side of a day, when a run keeps books.
///
/// **Money observes and decides; it does not feed the grass.** Everything this
/// touches is the account and the flock, so a priced run and an unpriced one of
/// the same scenario grow identical pasture - which is what makes the optional
/// argument safe, and is asserted by a test rather than assumed.
void keep_the_books(FarmBusiness& business, core::FarmAccount& account, core::FarmManager& manager,
                    core::Farm& farm, double hectares, const core::ManagementPolicy& policy,
                    const core::Date& today, RunSummary& summary) {
  account.charge_day(today);

  // **What the lambs did on the paddock yesterday comes back first.** The farm
  // grows them - that is the whole point of putting them on grass - so their
  // liveweight lives there and the flock has to read it before it steps, or
  // every lamb stays at its birth weight forever and the flock's own copy
  // silently overwrites the growth.
  if (farm.mobs().size() > 1 && farm.mobs()[1].mob.head > 0) {
    for (core::AgeCohort& cohort : business.flock.cohorts_for_update()) {
      if (cohort.is_finishing) {
        cohort.mob.state.liveweight_kg = farm.mobs()[1].mob.state.liveweight_kg;
        cohort.mob.state.liveweight_change_kg_per_day =
            farm.mobs()[1].mob.state.liveweight_change_kg_per_day;
        break;
      }
    }
  }

  const core::FlockDay flock_day = business.flock.step(today, business.calendar, business.rates);
  summary.flock_days.push_back(flock_day);

  // Caught on the day of the draft, because the cohort that was sold is gone
  // by the next one.
  if ((flock_day.sold_store > 0 || flock_day.kept_to_finish > 0) && farm.mobs().size() > 1 &&
      summary.lamb_weaning_weight_kg <= 0.0) {
    // The weaning weight, whichever way the crop went - it used to be recorded
    // only when stores were sold, so a farm that finished its lambs reported
    // nothing.
    summary.lamb_weaning_weight_kg = farm.mobs()[1].mob.state.liveweight_kg;
  }

  if (flock_day.sold_store > 0) {
    // **A store lamb is priced on what it weighs, not on what a finished one
    // weighs.** This used the draft weight - 38 kg - for every store sold,
    // which paid a finished lamb's cheque for an animal that had not been
    // finished. The lambs leaving this farm weigh about 17 at weaning and
    // rather more if they have been carried to autumn.
    //
    // **Still a carcass price on a live animal**, which a saleyard is not: a
    // store is sold per head on its own market, and the schedule this quotes is
    // for meat. See docs/validation/verify.md, E28.
    double liveweight = business.decisions.draft_liveweight_kg;
    if (farm.mobs().size() > 1 && farm.mobs()[1].mob.state.liveweight_kg > 0.0) {
      liveweight = farm.mobs()[1].mob.state.liveweight_kg;
    }
    const double carcass_kg = liveweight * business.decisions.dressing_out_fraction;
    account.record(today, core::LedgerReason::SoldStock,
                   static_cast<double>(flock_day.sold_store) * carcass_kg *
                       business.prices.lamb_dollars_per_kg_carcass,
                   "sold " + std::to_string(flock_day.sold_store) + " store lambs at " +
                       std::to_string(static_cast<int>(liveweight)) + " kg");
  }
  if (flock_day.culled > 0) {
    account.record(
        today, core::LedgerReason::SoldStock,
        static_cast<double>(flock_day.culled) * business.prices.cull_ewe_dollars_per_head,
        "sold " + std::to_string(flock_day.culled) + " cull ewes");
  }

  core::FarmOutlook outlook;
  outlook.today = today;
  outlook.head = business.flock.head();
  // **Whether there is a finishing class, and if so what it weighs.**
  //
  // This read a flat `false` because nothing drove a lamb's liveweight: a lamb
  // was born holding its mother's 55 kg and never changed, so drafting on that
  // weight sold animals whose weight the model had never earned - and did, at
  // 74 head a day for three hundred days. Lambs graze now (E22) and their
  // weight is an outcome of the grass, so the rule can have it.
  //
  // **Setting it to a flat `true` put the same bug back**, in an hour, by a
  // different door: `liveweight_kg` was being read off the front cohort, which
  // is the oldest, which is a 55 kg ewe. The rule saw a finished animal, drafted
  // the flock, and closed the year with nothing on the farm. So the two have to
  // be set together from the same cohort, and there is no finishing class when
  // there is no finishing stock.
  outlook.is_finishing_class = business.flock.finishing_head() > 0;
  // **Cover, not green, and that is a decision rather than an oversight.** Cover
  // is green plus the dead standing above it, and on this farm the two part
  // company by up to 45% in late summer - a paddock reporting 1,640 kg DM/ha of
  // cover in September has 968 of grass on it. Switching this to green was
  // tried and reverted: the 1,600 kg DM/ha floor a policy states is a rising
  // plate meter figure, and a plate meter reads dead material too, so measuring
  // green against a threshold set in cover silently raises the bar by a third
  // and had the farmer buying feed all year.
  //
  // The real defect is upstream: a third to a half of this model's standing dry
  // matter is dead, where a grazed New Zealand pasture runs 10 to 20%. See
  // docs/validation/verify.md, E26. `green_kg_dm_per_ha` is reported alongside
  // cover so the gap is visible rather than implied.
  outlook.cover_kg_dm_per_ha = farm.grid().mean_cover_kg_dm();
  outlook.minimum_cover_kg_dm_per_ha = policy.minimum_cover_kg_dm_per_ha;
  outlook.days_short = summary.days_short;
  outlook.hectares = hectares;
  outlook.balance_dollars = account.balance();
  outlook.daily_operating_cost_dollars = business.costs.annual_per_hectare() * hectares / 365.0;
  outlook.stored_feed_kg_dm = summary.feed_store.held_kg_dm();
  // The weight the drafting rule reads: the finishing cohort's when there is
  // one, and the oldest breeding cohort's otherwise - never the two mixed.
  for (const core::AgeCohort& cohort : business.flock.cohorts()) {
    if (cohort.is_finishing == outlook.is_finishing_class) {
      outlook.liveweight_kg = cohort.mob.state.liveweight_kg;
      break;
    }
  }

  // **Every sale takes the animals with it.** Applying a proposal to the
  // account and not to the flock is how the same lambs came to be sold three
  // hundred times: the money arrived and the stock never left. Anything that
  // sells head, sells them.
  for (const core::Proposal& done : manager.decide(outlook, account)) {
    if (done.head <= 0) {
      continue;
    }

    // **Which animals leave depends on why they are leaving**, and getting it
    // wrong sells the farm's mothers to fill a lamb order. A draft takes the
    // lambs that have come to weight; a cull or a destocking takes the oldest
    // ewes, which is what a farmer sells when they have to sell.
    switch (done.kind) {
      case core::ActionKind::SellFinishedStock:
        business.flock.sell_finishing(done.head);
        break;
      case core::ActionKind::Destock:
      case core::ActionKind::SellCullStock:
        business.flock.sell_oldest(done.head);
        break;
      case core::ActionKind::SellWool:
      case core::ActionKind::BuyFeed:
        break;
    }
  }

  // **What the farmer sells stops eating.** The flock and the mob on the
  // paddock used to be separate populations, and the whole year of grazing was
  // the same however the flock went: 1,305 kg DM/ha eaten in the driest year in
  // ten and 1,297 in the wettest, on a farm whose flock doubled at lambing and
  // halved at weaning. Nothing a farmer did could change the feed pressure, so
  // destocking relieved nothing and a drought had no way to reach the stock.
  //
  // **Ewes only, and that is a shortfall rather than a choice.** The mob's
  // liveweight is a ewe's and the energy model feeds the mob's representative
  // animal, so counting the lamb crop here would feed 529 lambs as 529 fully
  // grown ewes - intake this model never earned, the same error as drafting on
  // a liveweight nothing drives. The lambs graze in reality and do not here;
  // see docs/validation/verify.md (E21), whose fix is grazing per cohort.
  farm.set_mob_head(0, business.flock.breeding_head());

  // **And what they are carrying, not only how many there are.** The flock's
  // calendar now says whether a ewe is pregnant or milking, and OVERSEER's
  // equations 26 and 35 turn that into feed demand - which is where the missing
  // two-thirds of a stock unit was. A ewe fed maintenance all year eats 0.52 kg
  // DM a day; the New Zealand stock unit is 1.51.
  //
  // Taken from the oldest breeding cohort because the mob carries one animal's
  // state, and every breeding cohort is on the same calendar anyway.
  for (const core::AgeCohort& cohort : business.flock.cohorts()) {
    if (cohort.age_years >= 2 && !cohort.is_finishing) {
      farm.set_mob_reproduction(0, cohort.mob.state.days_pregnant, cohort.mob.state.days_lactating,
                                cohort.mob.state.young);
      break;
    }
  }

  // **And the lambs, if the bundle gave them a mob to graze with.** A scenario
  // that declares a second mob gets the season's lamb crop put on it: nothing
  // before lambing, the whole crop through spring, nothing after the weaning
  // draft. A bundle with one mob is unaffected - set_mob_head on an index that
  // is not there does nothing - so the older scenarios still describe ewes
  // alone.
  farm.set_mob_head(1, business.flock.finishing_head());

  // **Fed for what the farmer wants of them.** A finishing mob left at
  // maintenance holds its weaning weight and is never drafted, which is a store
  // system wearing a finishing system's costs. What the grass actually delivers
  // is the model's answer; this is only what the mob is fed for.
  if (farm.mobs().size() > 1) {
    // **Two phases, two targets.** A lamb on its mother is grown at one rate and
    // a lamb being finished at another, and feeding both at the finishing rate
    // weaned them at 23 kg where Beef + Lamb put a top flock at 30.
    bool suckling = false;
    for (const core::AgeCohort& cohort : business.flock.cohorts()) {
      if (cohort.is_finishing) {
        suckling = cohort.mob.state.on_the_mother;
        break;
      }
    }
    farm.set_own_target_gain(1, suckling ? business.decisions.suckling_gain_kg_per_day
                                         : business.decisions.finishing_gain_kg_per_day);
  }
  for (const core::AgeCohort& cohort : business.flock.cohorts()) {
    if (cohort.is_finishing) {
      core::AnimalState lamb = cohort.mob.state;

      // **The udder, as an energy transfer between two mobs.** The ewes were
      // charged for the milk they made; the lambs are credited with it, at the
      // energy TMC Eq. 46 puts in a kilogram and shared out among the lambs a
      // ewe is rearing. A ewe on a bare paddock makes less (Eq. 35 has pasture
      // mass in it), so her lambs go to the grass sooner - which is the whole
      // reason this is a supply and not an appetite.
      lamb.milk_me_mj_per_day = 0.0;
      if (!farm.mobs().empty() && lamb.on_the_mother) {
        const core::Mob& ewes = farm.mobs()[0].mob;
        const double lambs_per_ewe = ewes.state.young;
        if (lambs_per_ewe > 0.0) {
          const core::GrazingConditions ground = farm.conditions_for(0);
          const double yield_kg = core::daily_milk_yield_kg(ewes.animal, ewes.state, ground);
          lamb.milk_me_mj_per_day =
              yield_kg * core::milk_net_energy_mj_per_kg(ewes.animal) / lambs_per_ewe;
        }
      }

      farm.set_mob_state(1, lamb);
      break;
    }
  }
}

}  // namespace

RunSummary run_managed_scenario(const ScenarioBundle& bundle, const core::ManagementPolicy& policy,
                                const core::DietQuality& diet, std::string label,
                                FarmBusiness business) {
  // The bundle's own irrigation rule, for the reason the overload above gives:
  // a priced run of an irrigated bundle that came back rain-fed would be the
  // same silent failure, and this is the path the dashboard takes.
  return run_managed_scenario(bundle, policy, diet, std::move(label), DayObserver{},
                              bundle.irrigation.value_or(core::IrrigationPolicy{}),
                              bundle.irrigation_system, &business);
}

RunSummary run_managed_scenario(const ScenarioBundle& bundle, const core::ManagementPolicy& policy,
                                const core::DietQuality& diet, std::string label,
                                const DayObserver& each_day,
                                const core::IrrigationPolicy& irrigation,
                                const core::IrrigationSystem& system, FarmBusiness* business) {
  core::Farm farm = bundle.make_farm();

  // The farm's area, derived the way the report derives it: from the grid the
  // bundle describes rather than from the farm, which does not carry one.
  const double farm_hectares =
      bundle.grid.has_value() ? static_cast<double>(bundle.grid->cols * bundle.grid->rows) *
                                    bundle.grid->cell_size_m * bundle.grid->cell_size_m / 10000.0
                              : 0.0;

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

  // Stock-unit-days, divided by the run length at the end. Declared here rather
  // than inside the loop for the obvious reason.
  double stock_unit_days = 0.0;
  summary.label = std::move(label);
  farm.set_opening_stocks(summary.ledger);

  const core::WeatherSeries weather = bundle.weather->fetch(bundle.range);
  std::vector<bool> went_short(farm.mobs().size(), false);
  std::vector<double> supplement;

  // The schedule reads how dry the ground is and decides; the farm applies
  // what it is handed. Neither knows about the other's job.
  core::IrrigationSchedule schedule(irrigation, system, farm.grid().cell_count());
  summary.irrigation_mm.reserve(weather.records.size());

  std::optional<core::FarmManager> manager;
  if (business != nullptr) {
    summary.account.emplace(business->opening_balance_dollars, business->costs, business->prices,
                            farm_hectares);
    manager.emplace(business->decisions, core::standard_rules(business->decisions));

    // **Lambs are built from the bundle's lamb mob, if it has one.** A scenario
    // that declares a second mob is saying what a lamb on this farm is - its
    // species file, its sex factor, how long it suckles - and the flock builds
    // each season's crop from that rather than from a copy of its mothers.
    if (farm.mobs().size() > 1) {
      business->flock.set_lamb_template(farm.mobs()[1].mob);
    }
    summary.flock_days.reserve(weather.records.size());
  }

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

    // **Cut the surplus, and feed the stack before buying anything.**
    //
    // A real Canterbury farm shuts paddocks up in a wet spring and feeds the
    // result back in a dry summer, which is what turns a good year's growth
    // into a bad year's feed. Without it this farm's intake ran flat across a
    // decade while its growth nearly doubled, so a wet spring simply grew grass
    // that died where it stood.
    if (business != nullptr) {
      const core::ConservationPolicy& policy_for_cutting = business->conservation;
      const double cover = farm.grid().mean_cover_kg_dm();

      if (policy_for_cutting.may_cut_on(day.date.month, day.date.day) &&
          cover > policy_for_cutting.surplus_cover_kg_dm_per_ha) {
        // Taken off the paddock the way a mower takes it: everything above what
        // the cut leaves behind, over the whole farm.
        const double standing =
            (cover - policy_for_cutting.cut_to_cover_kg_dm_per_ha) * farm_hectares;
        const double cut = farm.cut_for_conservation(policy_for_cutting.cut_to_cover_kg_dm_per_ha,
                                                     &summary.ledger);
        static_cast<void>(standing);
        summary.feed_store.add(cut, business->conservation_losses);
      }

      // **Fed before anything is bought**, because a farm with a stack does not
      // ring a merchant.
      //
      // The stack supplies part of the same feed the mobs are already being
      // given - it changes where the day's supplement came from, not how much
      // of it there is. Reducing what the mobs were handed would have fed them
      // less for having made silage, which is the opposite of the point.
      if (summary.feed_store.held_kg_dm() > 0.0) {
        double wanted = 0.0;
        for (const double each : supplement) {
          wanted += each;
        }
        if (wanted > 0.0) {
          summary.conserved_fed_kg_dm +=
              summary.feed_store.take(wanted, business->conservation_losses);
        }
      }
    }

    const core::FarmDay farm_day = farm.step(day, diet, supplement, &summary.ledger, water);
    if (farm_day.any_mob_short) {
      ++summary.days_short;
    }
    for (std::size_t i = 0; i < farm_day.mobs.size() && i < went_short.size(); ++i) {
      went_short[i] = farm_day.mobs[i].grazing.feed_limited;
    }
    summary.eaten_kg_dm += farm_day.total_eaten_kg_dm;

    if (business != nullptr && manager.has_value() && summary.account.has_value()) {
      keep_the_books(*business, *summary.account, *manager, farm, farm_hectares, policy, day.date,
                     summary);
    }

    summary.dates.push_back(day.date);
    summary.weather.push_back(day);
    summary.cover_kg_dm_per_ha.push_back(farm.grid().mean_cover_kg_dm());
    summary.green_kg_dm_per_ha.push_back(farm.grid().mean_green_kg_dm());
    summary.growth_kg_dm_per_ha.push_back(farm.grid().mean_growth_kg_dm());
    summary.nitrate_leached_kg_per_ha.push_back(farm.grid().mean_nitrate_leached_kg_per_ha());
    // **Only where there is stock to record it from.** A farm can be run
    // carrying none - a pasture-only scenario is a legitimate thing to ask the
    // model for - and these two series were read off the first mob whether or
    // not one existed, which on an empty herd is a crash rather than a missing
    // number. Left out, the run ends with an empty liveweight series instead of
    // a year-long one, which is what RunSummary's own accessors and the report
    // already expect of a farm with nothing on it.
    if (!farm.mobs().empty()) {
      summary.liveweight_kg.push_back(farm.mobs().front().mob.state.liveweight_kg);
      summary.paddock_of_first_mob.push_back(
          static_cast<int>(farm.mobs().front().paddocks.front()));
    }

    // **Stock-unit-days, so the year's stocking rate is a mean and not a
    // snapshot.** Taken from the farm rather than from the flock because the
    // farm is what holds the mobs actually on the ground - a flock out of
    // season has head on paper that are not grazing anything.
    for (const core::FarmMob& carried : farm.mobs()) {
      stock_unit_days += static_cast<double>(carried.mob.head) * carried.mob.animal.stock_units;
    }

    if (each_day) {
      each_day(farm, farm_day, schedule);
    }
  }

  if (!summary.dates.empty()) {
    summary.mean_stock_units = stock_unit_days / static_cast<double>(summary.dates.size());
  }

  if (business != nullptr) {
    summary.closing_head = business->flock.head();
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
    summary.green_kg_dm_per_ha.push_back(farm.grid().mean_green_kg_dm());
    summary.growth_kg_dm_per_ha.push_back(farm.grid().mean_growth_kg_dm());
    summary.nitrate_leached_kg_per_ha.push_back(farm.grid().mean_nitrate_leached_kg_per_ha());
    // A farm with no stock on it, as above: nothing to read a liveweight from.
    if (!farm.mobs().empty()) {
      summary.liveweight_kg.push_back(farm.mobs().front().mob.state.liveweight_kg);
      summary.paddock_of_first_mob.push_back(
          static_cast<int>(farm.mobs().front().paddocks.front()));
    }
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
