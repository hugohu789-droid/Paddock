// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <paddock/config/FarmDashboard.hpp>

namespace paddock::config {

namespace {

std::string fixed(double value, int places) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(places) << value;
  return out.str();
}

/// How many decimal places a figure deserves. Counts get none - "23.00 head" is
/// two ewes' worth of spurious precision on a number that cannot be fractional.
int places_for(const Indicator& indicator) {
  if (indicator.unit == "head" || indicator.unit == "days") {
    return 0;
  }
  return std::abs(indicator.value) >= 100.0 ? 0 : 2;
}

std::string right(const std::string& text, std::size_t width) {
  return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

std::string left(const std::string& text, std::size_t width) {
  return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

/// A CSV field, quoted when it has to be. Notes contain commas.
std::string csv(const std::string& text) {
  if (text.find_first_of(",\"\n") == std::string::npos) {
    return text;
  }
  std::string out = "\"";
  for (const char character : text) {
    if (character == '"') {
      out += '"';
    }
    out += character;
  }
  return out + "\"";
}

/// Where a value sits in a band. Absent ends mean unbounded on that side.
Standing standing_in(double value, std::optional<double> low, std::optional<double> high) {
  if (!low.has_value() && !high.has_value()) {
    return Standing::Unmeasured;
  }
  if ((low.has_value() && value < *low) || (high.has_value() && value > *high)) {
    return Standing::Over;
  }
  // Within a tenth of either edge is worth a second look: a year like this one
  // would put the farm outside.
  if (high.has_value() && value > *high * 0.9) {
    return Standing::Watch;
  }
  if (low.has_value() && *low > 0.0 && value < *low * 1.1) {
    return Standing::Watch;
  }
  return Standing::Good;
}

Indicator make(std::string name, double value, std::string unit, Provenance trust, std::string note,
               std::optional<double> low = {}, std::optional<double> high = {}) {
  Indicator indicator;
  indicator.name = std::move(name);
  indicator.value = value;
  indicator.unit = std::move(unit);
  indicator.trust = trust;
  indicator.note = std::move(note);
  indicator.low = low;
  indicator.high = high;
  indicator.standing = standing_in(value, low, high);
  return indicator;
}

double ledger_inflow(const RunSummary& run, core::Budget budget, const std::string& process) {
  for (const core::ProcessEntry& entry : run.ledger.entries(budget)) {
    if (entry.process == process) {
      return entry.inflow;
    }
  }
  return 0.0;
}

double ledger_outflow(const RunSummary& run, core::Budget budget, const std::string& process) {
  for (const core::ProcessEntry& entry : run.ledger.entries(budget)) {
    if (entry.process == process) {
      return entry.outflow;
    }
  }
  return 0.0;
}

/// Farm area, from the grid. `core::Farm` does not carry it, so every caller
/// that needs hectares works it out the same way.
double hectares_of(const ScenarioBundle& bundle) {
  if (!bundle.grid.has_value()) {
    return 0.0;
  }
  const double cells =
      static_cast<double>(bundle.grid->cols) * static_cast<double>(bundle.grid->rows);
  return cells * bundle.grid->cell_size_m * bundle.grid->cell_size_m / 10'000.0;
}

int flock_total(const RunSummary& run, int core::FlockDay::* field) {
  return std::accumulate(
      run.flock_days.begin(), run.flock_days.end(), 0,
      [field](int running, const core::FlockDay& day) { return running + (day.*field); });
}

}  // namespace

std::string to_string(Standing standing) {
  switch (standing) {
    case Standing::Unmeasured:
      return "-";
    case Standing::Good:
      return "ok";
    case Standing::Watch:
      return "watch";
    case Standing::Over:
      return "outside";
  }
  return "?";
}

bool Indicator::rests_on_evidence() const {
  return trust == Provenance::Direct || trust == Provenance::Derived || trust == Provenance::Fitted;
}

std::vector<Indicator> FarmDashboard::all_indicators() const {
  std::vector<Indicator> all;
  for (const DashboardPanel& panel : panels) {
    all.insert(all.end(), panel.indicators.begin(), panel.indicators.end());
  }
  return all;
}

int FarmDashboard::indicators_total() const {
  return static_cast<int>(all_indicators().size());
}

int FarmDashboard::indicators_on_evidence() const {
  const std::vector<Indicator> all = all_indicators();
  return static_cast<int>(std::count_if(all.begin(), all.end(), [](const Indicator& indicator) {
    return indicator.rests_on_evidence();
  }));
}

/// Which of "The year" panel's figures are what the stock did rather than what
/// the ground did. Pasture grown, and everything in Water and Environment, are
/// not on this list on purpose: an animal outside its domain does not make the
/// water balance wrong (E116).
[[nodiscard]] bool depends_on_the_animals(const std::string& name) {
  return name == "Utilisation" || name == "Eaten" || name == "Lowest cover" ||
         name == "Days short of feed" || name == "Stocking rate" || name == "Closing stock" ||
         name == "Lamb at weaning";
}

/// Two decimal places, without pulling in a stream for one number.
[[nodiscard]] std::string two_places(double value) {
  std::string text = std::to_string(value);
  const std::size_t point = text.find('.');
  if (point != std::string::npos && text.size() > point + 3) {
    text.erase(point + 3);
  }
  return text;
}

FarmDashboard build_dashboard(const ScenarioBundle& bundle, const RunSummary& run,
                              std::string label, const std::optional<NitrogenRegulation>& rule) {
  FarmDashboard board;
  board.farm = bundle.name;
  board.label = std::move(label);
  board.dates = run.dates;
  if (!run.dates.empty()) {
    board.range = core::DateRange{run.dates.front(), run.dates.back()};
  }

  const double hectares = hectares_of(bundle);
  const double grown = ledger_inflow(run, core::Budget::DryMatter, "pasture_growth");
  const double rain = ledger_inflow(run, core::Budget::Water, "rainfall");
  const double evapotranspiration = ledger_outflow(run, core::Budget::Water, "evapotranspiration");
  const double drainage = ledger_outflow(run, core::Budget::Water, "drainage");
  const double eaten_per_ha = hectares > 0.0 ? run.eaten_kg_dm / hectares : 0.0;

  // ---- The year -----------------------------------------------------------
  DashboardPanel year;
  year.title = "The year";
  year.question = "Did this farm grow and harvest what a Canterbury dryland farm should?";
  year.indicators.push_back(
      make("Pasture grown", grown, "kg DM/ha", Provenance::Fitted,
           "Fitted: radiation use efficiency was calibrated to Winchmore's dryland water use "
           "efficiency. The band is what that trial's dryland treatment implies at its 745 mm "
           "mean, so a drier year should sit under it.",
           4'000.0, 9'800.0));
  year.indicators.push_back(make(
      "Utilisation", grown > 0.0 ? 100.0 * eaten_per_ha / grown : 0.0, "%", Provenance::Verify,
      "**Exploratory while E95 is open**: this rests on the ewe's intake at peak lactation, and "
      "the shipped ewe carries OVERSEER's New Zealand reference weight beside GrazPlan's "
      "Australian wool-breed lactation peaks. The direction of the early-lactation deficit is "
      "ordinary physiology; its depth for this animal is not settled, and this figure moves with "
      "it. What the stock took off as a share of what grew. Derived, not asserted: this farm's own "
      "stocking rate at Parker's (1998) 550 kg DM a stock unit, over Winchmore's measured "
      "dryland production. Most of the distance to a Class 6 farm is stock this one does not "
      "carry, not feed its animals refuse - see data/calibration/stock-unit-intake.csv.",
      45.0, 90.0));

  // **The numerator on its own, because utilisation is a ratio and a ratio
  // hides which half is wrong.** A farm can miss a utilisation band by eating
  // too little or by growing too much, and this model does both - E40 has the
  // production half. Intake has a sourced target where utilisation does not:
  // Parker (1998) defines the stock unit as 550 kg DM of 10.5 MJ ME/kg DM a
  // year, which is the diet quality this model already assumes, so the two are
  // comparable without an adjustment.
  year.indicators.push_back(make(
      "Eaten", eaten_per_ha, "kg DM/ha", Provenance::Verify,
      "**Exploratory while E95 is open**: this rests on the ewe's intake at peak lactation, and "
      "the shipped ewe carries OVERSEER's New Zealand reference weight beside GrazPlan's "
      "Australian wool-breed lactation peaks. The direction of the early-lactation deficit is "
      "ordinary physiology; its depth for this animal is not settled, and this figure moves with "
      "it. What the stock actually removed. The band is this farm's own stocking rate and Beef + "
      "Lamb's Class 6 rate, each at 550 kg DM a stock unit - so a figure under it is animals "
      "eating less than their stock-unit rating implies, whatever the pasture did.",
      3850.0, 4257.0));
  year.indicators.push_back(
      // **Verify, not Fitted, and the difference is not pedantry.** Fitted means
      // calibrated so that published observations reproduce, and this is fitted
      // to nothing: no measured minimum-cover series has been found, and the
      // band below is the farm's own management floor rather than an
      // observation - so "outside" here means the farmer's floor was breached,
      // which is a statement about the run and not about the world. A third to
      // a half of what it counts is dead material whose disappearance rate has
      // no New Zealand measurement behind it either (E26). Reported as Fitted,
      // a customer-facing summary renders it "reproduces published
      // observations", which is false.
      make("Lowest cover", run.lowest_cover_kg_dm_per_ha(), "kg DM/ha", Provenance::Verify,
           "The bottom of the year, against the cover this farm holds itself to - so outside "
           "means the floor was breached, not that the model disagrees with a measurement. "
           "Cover includes dead standing material, which on this farm is a third to a half of "
           "it - see E26.",
           bundle.management.has_value()
               ? std::optional<double>(bundle.management->minimum_cover_kg_dm_per_ha)
               : std::nullopt,
           std::nullopt));
  year.indicators.push_back(
      make("Days short of feed", static_cast<double>(run.feed_supply_short_days), "days",
           Provenance::Verify,
           "**Exploratory while E95 is open**: this rests on the ewe's intake at peak lactation, "
           "and the shipped ewe carries OVERSEER's New Zealand reference weight beside GrazPlan's "
           "Australian wool-breed lactation peaks. The direction of the early-lactation deficit is "
           "ordinary physiology; its depth for this animal is not settled, and this figure moves "
           "with it. Days a mob was offered less than it asked for.",
           std::nullopt, 0.0));
  // **The number this model could not state, and the one that turned out to be
  // the answer.** Its utilisation looked bad and three explanations were tried
  // before the obvious one: the farm carries a third fewer stock units than the
  // class it is priced against. Nobody could see that, because a stocking rate
  // was something you worked out by hand from head and hectares.
  //
  // Zero means no class in the flock has a published conversion, which is not
  // the same as no stock, so the indicator is left out rather than reading nil.
  if (run.mean_stock_units > 0.0 && hectares > 0.0) {
    year.indicators.push_back(make(
        "Stocking rate", run.mean_stock_units / hectares, "SU/ha", Provenance::Verify,
        "Stock units carried, averaged over every day - lambing nearly doubles the head on the "
        "place and weaning takes it back, so a snapshot would not do. A ewe is 1.0 SU (Parker "
        "1998, Table 1). **Verify, because the comparison basis is not confirmed**: Beef + "
        "Lamb's Class 6 figure of 7.74 SU/ha does not state whether it is opening, wintered or "
        "mean stock, and this farm reads 5.2 on its opening count against 3.5 on the mean. Until "
        "that is settled the band says which order of magnitude, not which side of a line.",
        7.0, 8.5));
  }
  year.indicators.push_back(
      make("Closing stock", static_cast<double>(run.closing_head), "head", Provenance::Verify,
           "**Exploratory while E95 is open**: this rests on the ewe's intake at peak lactation, "
           "and the shipped ewe carries OVERSEER's New Zealand reference weight beside GrazPlan's "
           "Australian wool-breed lactation peaks. The direction of the early-lactation deficit is "
           "ordinary physiology; its depth for this animal is not settled, and this figure moves "
           "with it. The flock at the end of the farm year, after the culls and the "
           "weaning draft have gone."));
  if (run.lamb_weaning_weight_kg > 0.0) {
    year.indicators.push_back(make(
        "Lamb at weaning", run.lamb_weaning_weight_kg, "kg", Provenance::Verify,
        "**Exploratory while E95 is open**: this rests on the ewe's intake at peak lactation, and "
        "the shipped ewe carries OVERSEER's New Zealand reference weight beside GrazPlan's "
        "Australian wool-breed lactation peaks. The direction of the early-lactation deficit is "
        "ordinary physiology; its depth for this animal is not settled, and this figure moves with "
        "it. An outcome of the grass, not a target. OVERSEER assumes 20 kg for a sheep when "
        "none is supplied, which is what this can be checked against.",
        13.0, 27.0));
  }
  board.panels.push_back(std::move(year));

  // ---- Water --------------------------------------------------------------
  DashboardPanel water;
  water.title = "Water";
  water.question = "How much fell, how much left, and how much dry matter it bought";
  water.indicators.push_back(
      make("Rainfall", rain, "mm", Provenance::Direct, "Recorded weather, not a generator."));
  water.indicators.push_back(
      make("Evapotranspiration", evapotranspiration, "mm", Provenance::Direct, "FAO-56."));
  water.indicators.push_back(make("Drainage", drainage, "mm", Provenance::Derived,
                                  "Water past the root zone. It is what carries nitrate."));
  water.indicators.push_back(
      make("Days water-stressed", static_cast<double>(run.days_water_stressed()), "days",
           Provenance::Derived, "Days the sward grew less than the weather alone would allow."));
  water.indicators.push_back(
      make("Water use efficiency", evapotranspiration > 0.0 ? grown / evapotranspiration : 0.0,
           "kg DM/ha/mm", Provenance::Fitted,
           "Fitted to Martin et al. (2006): 12.3 for Canterbury dryland, 20 for irrigated "
           "ryegrass and clover. A rain-fed farm reading near 20 is converting water at a rate "
           "its rainfall does not buy.",
           8.0, 16.0));
  board.panels.push_back(std::move(water));

  // ---- Money --------------------------------------------------------------
  if (run.account.has_value()) {
    DashboardPanel money;
    money.title = "Money";
    money.question = "Did the farm pay for itself, and how close did it come to not?";
    money.indicators.push_back(
        make("Closing balance", run.account->balance(), "$", Provenance::Derived,
             "Opening cash, plus what was sold, less a year of costs.", 0.0, std::nullopt));
    money.indicators.push_back(
        make("Lowest balance", run.account->lowest_balance(), "$", Provenance::Derived,
             "The tightest the year got. A farm that dips near zero is one bad month from "
             "selling stock it did not want to sell.",
             0.0, std::nullopt));
    money.indicators.push_back(make("Sold stock",
                                    run.account->total_for(core::LedgerReason::SoldStock), "$",
                                    Provenance::Verify,
                                    "At schedule prices carried in the economics file, which are "
                                    "marked verify."));
    money.indicators.push_back(make("Sold wool",
                                    run.account->total_for(core::LedgerReason::SoldWool), "$",
                                    Provenance::Placeholder, "Rests on an unsourced 5 kg fleece."));
    money.indicators.push_back(
        make("Bought feed", -run.account->total_for(core::LedgerReason::BoughtFeed), "$",
             Provenance::Placeholder,
             "What the year cost in supplement. In a dry year this is the drought, expressed "
             "as money rather than as destocking."));
    board.panels.push_back(std::move(money));
  }

  // ---- Environment --------------------------------------------------------
  DashboardPanel environment;
  environment.title = "Environment";
  environment.question = "What left the farm for the water, and what does the rule say?";

  const double leached = run.nitrate_leached_total_kg_per_ha();
  const std::optional<double> trigger =
      rule.has_value() ? std::optional<double>(rule->leaching_trigger_kg_n_per_ha_per_year.value)
                       : std::nullopt;

  environment.indicators.push_back(make(
      "Nitrate leached", leached, "kg N/ha", Provenance::Placeholder,
      rule.has_value()
          ? "Against the " + rule->zone + " trigger of " +
                fixed(rule->leaching_trigger_kg_n_per_ha_per_year.value, 0) +
                " kg N/ha/yr. Counts urine-patch leaching only and reads about 15% low; the "
                "patch uptake it rests on is a placeholder."
          : "No zone rule was supplied, so there is nothing to be compliant against. New Zealand "
            "sets these catchment by catchment.",
      std::nullopt, trigger));
  environment.indicators.push_back(
      make("Leached per mm drained", drainage > 0.0 ? leached / drainage : 0.0, "kg N/ha/mm",
           Provenance::Placeholder,
           "Read this before the total. Leaching moves with drainage, and drainage is weather "
           "rather than management."));
  environment.indicators.push_back(
      make("Nitrogen fixed", ledger_inflow(run, core::Budget::Nitrogen, "legume_fixation"),
           "kg N/ha", Provenance::Derived,
           "Clover fixation, which with bought feed is this farm's only nitrogen income."));
  environment.indicators.push_back(
      make("Excreta returned", ledger_inflow(run, core::Budget::Nitrogen, "excreta_returned"),
           "kg N/ha", Provenance::Derived,
           "Dung and urine back onto the paddock. On a grazed farm this, not fertiliser, is what "
           "leaches."));
  environment.indicators.push_back(make(
      "Synthetic N applied", ledger_inflow(run, core::Budget::Nitrogen, "fertiliser"), "kg N/ha",
      Provenance::Direct,
      rule.has_value()
          ? "The national cap is " + fixed(rule->fertiliser_cap_kg_n_per_ha_per_year.value, 0) +
                " kg N/ha/yr. This farm applies none, so the cap is not what binds it."
          : "This farm applies none.",
      std::nullopt,
      rule.has_value() ? std::optional<double>(rule->fertiliser_cap_kg_n_per_ha_per_year.value)
                       : std::nullopt));
  board.panels.push_back(std::move(environment));

  // ---- The flock's year ---------------------------------------------------
  if (!run.flock_days.empty()) {
    DashboardPanel flock;
    flock.title = "The flock";
    flock.question = "What was born, what left, and how";
    flock.indicators.push_back(make("Lambs born", flock_total(run, &core::FlockDay::born), "head",
                                    Provenance::Direct,
                                    "At Beef + Lamb New Zealand's 132.3% lambing percentage."));
    flock.indicators.push_back(make("Ewes lost", flock_total(run, &core::FlockDay::died), "head",
                                    Provenance::Direct,
                                    "Ridler et al. (2025), from 34 measured New Zealand flocks."));
    flock.indicators.push_back(make("Culled", flock_total(run, &core::FlockDay::culled), "head",
                                    Provenance::Direct,
                                    "For age at the turn of the year, and at "
                                    "weaning for teeth and udders."));
    flock.indicators.push_back(
        make("Store lambs sold", flock_total(run, &core::FlockDay::sold_store), "head",
             Provenance::Derived, "The crop less the replacements kept back."));
    board.panels.push_back(std::move(flock));
  }

  // ---- Whether any of the above may be read as a prediction ---------------
  //
  // **The trajectory is untouched.** Every figure on this page is what the
  // model produced; what changes below the boundary is only what the page is
  // allowed to claim about them, and it changes through the provenance path
  // that already existed rather than a second one (E116).
  if (!run.animal_domain.inside()) {
    const core::Date& crossed = *run.animal_domain.first_crossing;
    board.animal_domain_warning =
        "Animal condition moved outside the model's currently supported range on " +
        crossed.to_iso_string() + ". " + run.animal_domain.cohort +
        " reached a relative condition of " +
        two_places(run.animal_domain.lowest_relative_condition) + " at " +
        two_places(run.animal_domain.cohort_liveweight_kg) +
        " kg, below the 0.625 that is condition score 0 on SCA (1990)'s 0-5 scale - for this "
        "animal, " +
        two_places(run.animal_domain.boundary_liveweight_kg) +
        " kg. This model carries no starvation mortality, so the run continues; "
        "animal-production and economic results at or after this point must not be read as "
        "credible farm predictions.";

    // Pasture, water and environment keep their standing. They are not being
    // vouched for by the animals, and downgrading them would say something this
    // finding does not support.
    for (DashboardPanel& panel : board.panels) {
      const bool animal_panel = panel.title == "The flock" || panel.title == "Money";
      for (Indicator& indicator : panel.indicators) {
        if (!animal_panel && !depends_on_the_animals(indicator.name)) {
          continue;
        }
        indicator.trust = Provenance::Placeholder;
        indicator.note = "**Outside the supported animal-production domain.** " + indicator.note;
      }
    }
  }

  if (run.supplement_market_is_finite) {
    board.supplement_market_warning =
        "Supplement available to buy was capped at " +
        two_places(run.supplement_purchased_kg_dm + run.supplement_unfilled_kg_dm > 0.0
                       ? run.supplement_purchased_kg_dm
                       : 0.0) +
        " kg DM bought against " + two_places(run.supplement_requested_kg_dm) +
        " kg DM asked for. Sensitivity assumption, not a New Zealand market figure: no source in "
        "this project states how much supplement a farm can buy, or when.";
  }

  // ---- The trends ---------------------------------------------------------
  const auto add_series = [&board](std::string name, std::string unit,
                                   const std::vector<double>& values,
                                   std::optional<double> reference = {}) {
    if (values.empty()) {
      return;
    }
    DashboardSeries series;
    series.name = std::move(name);
    series.unit = std::move(unit);
    series.values = values;
    series.reference = reference;
    board.series.push_back(std::move(series));
  };

  add_series("Cover", "kg DM/ha", run.cover_kg_dm_per_ha,
             bundle.management.has_value()
                 ? std::optional<double>(bundle.management->minimum_cover_kg_dm_per_ha)
                 : std::nullopt);
  add_series("Green", "kg DM/ha", run.green_kg_dm_per_ha);
  add_series("Water stress", "0-1", run.water_stress, 1.0);
  add_series("Liveweight", "kg", run.liveweight_kg);
  add_series("Nitrate leached", "kg N/ha/day", run.nitrate_leached_kg_per_ha);
  add_series("Irrigation", "mm", run.irrigation_mm);

  return board;
}

bool may_report_stocking_optimum(const std::vector<const RunSummary*>& candidates) {
  for (const RunSummary* candidate : candidates) {
    if (candidate != nullptr && !candidate->animal_domain.inside()) {
      return false;
    }
  }
  return true;
}

std::string as_text(const FarmDashboard& dashboard) {
  std::ostringstream out;
  out << dashboard.farm << " - " << dashboard.label << "\n"
      << "  " << dashboard.range.first.to_iso_string() << " to "
      << dashboard.range.last.to_iso_string() << "\n";

  // **First on the page, before any number.** A caveat printed under the
  // figures is a caveat most readers meet after they have already read them.
  if (!dashboard.animal_domain_warning.empty()) {
    out << "\n  !! Outside the supported animal-production domain\n"
        << "  " << std::string(46, '=') << "\n"
        << "  " << dashboard.animal_domain_warning << "\n";
  }
  if (!dashboard.supplement_market_warning.empty()) {
    out << "\n  !! Supplement market is an assumption\n"
        << "  " << std::string(38, '=') << "\n"
        << "  " << dashboard.supplement_market_warning << "\n";
  }

  for (const DashboardPanel& panel : dashboard.panels) {
    out << "\n  " << panel.title << "\n"
        << "  " << std::string(panel.title.size(), '=') << "\n"
        << "  " << panel.question << "\n\n";

    for (const Indicator& indicator : panel.indicators) {
      out << "    " << left(indicator.name, 24)
          << right(fixed(indicator.value, places_for(indicator)), 10) << " "
          << left(indicator.unit, 12) << left(to_string(indicator.standing), 9) << " "
          << to_string(indicator.trust) << "\n";
    }
  }

  // **The panel that says how much of the rest means anything.** It goes last
  // because it is a summary of the others, and it is not optional: a page of
  // numbers whose provenance is elsewhere is a page that will be quoted without
  // it.
  const int evidenced = dashboard.indicators_on_evidence();
  const int total = dashboard.indicators_total();
  out << "\n  How much of this can be trusted\n"
      << "  ===============================\n\n"
      << "    " << evidenced << " of " << total
      << " indicators rest on something published or on a stated fit.\n"
      << "    The rest are placeholders. They are the right order of magnitude and they are\n"
      << "    not measurements; docs/validation/verify.md says what each would take.\n";

  out << "\n  Notes\n  =====\n\n";
  for (const DashboardPanel& panel : dashboard.panels) {
    for (const Indicator& indicator : panel.indicators) {
      if (!indicator.note.empty()) {
        out << "    " << indicator.name << " - " << indicator.note << "\n";
      }
    }
  }
  return out.str();
}

std::string indicators_as_csv(const FarmDashboard& dashboard) {
  std::ostringstream out;
  out << "farm,year,panel,indicator,value,unit,standing,low,high,trust,note\n";
  for (const DashboardPanel& panel : dashboard.panels) {
    for (const Indicator& indicator : panel.indicators) {
      out << csv(dashboard.farm) << ',' << csv(dashboard.label) << ',' << csv(panel.title) << ','
          << csv(indicator.name) << ',' << fixed(indicator.value, 4) << ',' << csv(indicator.unit)
          << ',' << to_string(indicator.standing) << ','
          << (indicator.low.has_value() ? fixed(*indicator.low, 4) : "") << ','
          << (indicator.high.has_value() ? fixed(*indicator.high, 4) : "") << ','
          << to_string(indicator.trust) << ',' << csv(indicator.note) << '\n';
    }
  }
  return out.str();
}

std::string series_as_csv(const FarmDashboard& dashboard) {
  std::ostringstream out;
  // **The reference travels in the heading.** A cover column exported without
  // the level it is read against is a column somebody plots and then guesses
  // at: 1,400 kg DM/ha is a farm in trouble or a farm doing exactly what it
  // planned, and which of those depends on a number that would not be in the
  // file.
  out << "date";
  for (const DashboardSeries& series : dashboard.series) {
    std::string heading = series.name + " (" + series.unit + ")";
    if (series.reference.has_value()) {
      heading += " [held at " + fixed(*series.reference, 0) + "]";
    }
    out << ',' << csv(heading);
  }
  out << '\n';

  for (std::size_t day = 0; day < dashboard.dates.size(); ++day) {
    out << dashboard.dates[day].to_iso_string();
    for (const DashboardSeries& series : dashboard.series) {
      out << ',';
      if (day < series.values.size()) {
        out << fixed(series.values[day], 4);
      }
    }
    out << '\n';
  }
  return out.str();
}

std::string compare_dashboards_as_text(const std::vector<FarmDashboard>& boards) {
  if (boards.empty()) {
    return "no runs to compare\n";
  }

  // **The column is as wide as the widest thing going in it.** `right` pads to
  // a width and returns the text untouched when it is already that wide, so a
  // fixed 14 put two nineteen-character scenario names hard against each other
  // and the header read `demo_irrigation_offdemo_irrigation_on`. That only ever
  // showed up once bundles could be compared by name rather than by year, and a
  // year is four characters.
  std::size_t column = 14;
  for (const FarmDashboard& board : boards) {
    column = std::max(column, board.label.size() + 2);
  }

  std::ostringstream out;
  out << "Indicator, year by year\n\n";
  out << "  " << left("indicator", 24) << left("unit", 12);
  for (const FarmDashboard& board : boards) {
    out << right(board.label, column);
  }
  out << "\n  " << std::string(24 + 12 + (column * boards.size()), '-') << "\n";

  // Driven off the first board's shape, and any indicator the others do not
  // carry is left blank rather than dropped - a run without books has no money
  // panel, and hiding the row would hide that.
  for (const DashboardPanel& panel : boards.front().panels) {
    out << "  " << panel.title << "\n";
    for (const Indicator& indicator : panel.indicators) {
      out << "  " << left("  " + indicator.name, 24) << left(indicator.unit, 12);
      for (const FarmDashboard& board : boards) {
        std::string cell;
        for (const Indicator& theirs : board.all_indicators()) {
          if (theirs.name == indicator.name) {
            cell = fixed(theirs.value, places_for(theirs));
            if (theirs.standing == Standing::Over) {
              cell += "!";
            } else if (theirs.standing == Standing::Watch) {
              cell += "?";
            }
            break;
          }
        }
        out << right(cell, column);
      }
      out << "\n";
    }
  }
  out << "\n  ! outside its band, ? within a tenth of the edge.\n";
  return out.str();
}

std::string compare_dashboards_as_csv(const std::vector<FarmDashboard>& boards) {
  std::ostringstream out;
  out << "farm,year,panel,indicator,value,unit,standing,trust\n";
  for (const FarmDashboard& board : boards) {
    for (const DashboardPanel& panel : board.panels) {
      for (const Indicator& indicator : panel.indicators) {
        out << csv(board.farm) << ',' << csv(board.label) << ',' << csv(panel.title) << ','
            << csv(indicator.name) << ',' << fixed(indicator.value, 4) << ',' << csv(indicator.unit)
            << ',' << to_string(indicator.standing) << ',' << to_string(indicator.trust) << '\n';
      }
    }
  }
  return out.str();
}

}  // namespace paddock::config
