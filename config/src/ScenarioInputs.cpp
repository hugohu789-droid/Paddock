// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <paddock/config/ScenarioInputs.hpp>

namespace paddock::config {

namespace {

// The categories, in the order a person reads them: what the run is, then the
// ground it is on, then the weather over it, then what grows, then what eats
// it, then what the farmer does about all of that.
//
// **Irrigation is last on purpose.** It is the thing most often varied, and a
// difference at the bottom of a short list is where the eye stops.
/// What the block is written to fit. A terminal is eighty columns until
/// somebody widens it, and a view meant to be read beside a table on a
/// projector should assume nobody has.
constexpr std::size_t kLineWidth = 80;

const std::vector<std::string>& categories() {
  static const std::vector<std::string> in_display_order = {
      "Run",     "Farm",  "Ground",         "Weather",    "Soil",
      "Pasture", "Stock", "Grazing policy", "Irrigation", "Money and rules",
  };
  return in_display_order;
}

/// A number with its trailing zeros taken off: 25.0 reads as "25", 2.5 as
/// "2.5". Values here are read aloud, and "25.00 mm" is a machine talking.
std::string tidy(double value, int decimals = 2) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(decimals) << value;
  std::string text = out.str();
  if (text.find('.') != std::string::npos) {
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.') {
      text.pop_back();
    }
  }
  return text.empty() ? "0" : text;
}

std::string with_unit(double value, const std::string& unit, int decimals = 2) {
  return tidy(value, decimals) + " " + unit;
}

/// A depletion fraction as the share of water still in the soil.
///
/// **The end a person uses.** FAO-56 writes the trigger as depletion - how much
/// of the profile has gone - and `FarmletGrid::available_water_fraction` already
/// records that a paddock is read as "at 40%" rather than "60% depleted". A
/// comparison that said "trigger 0.6" would be asking the reader to do that
/// subtraction in their head while somebody talks at them.
std::string as_available_water(double depletion_fraction) {
  return tidy((1.0 - depletion_fraction) * 100.0, 1) + "% available water";
}

/// The first twelve characters of a hash, which is what the run report shows.
///
/// A whole SHA-256 in a comparison is forty pointless characters that push the
/// value off the line; twelve is plenty to tell two files apart and it matches
/// what `paddock scenario run` already prints.
std::string short_hash(const std::string& hex) {
  return hex.empty() ? "-" : hex.substr(0, 12);
}

/// The one input a bundle names for `section`, or nothing.
const BundleInput* input_for(const ScenarioBundle& bundle, const std::string& section) {
  const auto found =
      std::find_if(bundle.inputs.begin(), bundle.inputs.end(),
                   [&section](const BundleInput& input) { return input.section == section; });
  return found == bundle.inputs.end() ? nullptr : &*found;
}

/// A comma-separated list of names, wrapped to fit a terminal.
///
/// **Because the list is as long as the comparison is bad.** Two unrelated
/// bundles differ in seven categories, and seven names plus a lead-in runs well
/// past eighty columns - so the line that says the comparison is not controlled
/// would be the one line that wraps badly and is hardest to read.
std::string listed(const std::string& lead, const std::vector<std::string>& names,
                   const std::string& indent, std::size_t width = kLineWidth) {
  std::ostringstream out;
  out << lead;
  std::size_t column = lead.size();
  for (std::size_t i = 0; i < names.size(); ++i) {
    const std::string piece = (i == 0 ? std::string(" ") : std::string(", ")) + names[i] +
                              (i + 1 == names.size() ? std::string(".") : std::string());
    if (column + piece.size() > width && i > 0) {
      out << ",\n" << indent;
      column = indent.size();
      out << names[i] << (i + 1 == names.size() ? "." : "");
      column += names[i].size() + 1;
      continue;
    }
    out << piece;
    column += piece.size();
  }
  out << "\n";
  return out.str();
}

std::string system_name(core::GrazingSystem system) {
  return system == core::GrazingSystem::Rotational ? "rotational" : "set stocking";
}

}  // namespace

const std::vector<std::string>& input_categories() {
  return categories();
}

std::vector<InputSetting> scenario_inputs(const ScenarioBundle& bundle) {
  std::vector<InputSetting> settings;
  const auto add = [&settings](const std::string& category, const std::string& label,
                               std::string value) {
    settings.push_back(InputSetting{category, label, std::move(value)});
  };

  // ---------------------------------------------------------------- Run
  add("Run", "from", bundle.range.first.to_iso_string());
  add("Run", "to", bundle.range.last.to_iso_string());
  add("Run", "latitude", with_unit(bundle.latitude_degrees, "degrees", 3));
  // **The seed belongs here.** Two scenarios drawn from different seeds are not
  // the same experiment run twice, and a comparison that did not say so would
  // let a difference in the noise be read as a difference in the management.
  add("Run", "seed", std::to_string(bundle.master_seed));

  // ---------------------------------------------------------------- Farm
  if (bundle.grid.has_value()) {
    const GridSpec& grid = *bundle.grid;
    const double hectares = static_cast<double>(grid.cols) * static_cast<double>(grid.rows) *
                            grid.cell_size_m * grid.cell_size_m / 10000.0;
    add("Farm", "area", with_unit(hectares, "ha", 1));
    add("Farm", "resolution",
        std::to_string(grid.cols) + " x " + std::to_string(grid.rows) + " cells at " +
            with_unit(grid.cell_size_m, "m", 1));
    add("Farm", "paddock size", with_unit(grid.paddock_hectares, "ha", 2));
    // West and east together, because the gradient is the fact and either
    // number on its own is half of it.
    add("Farm", "available water",
        tidy(grid.available_water_west_mm, 1) + " to " +
            with_unit(grid.available_water_east_mm, "mm", 1) + ", west to east");
    add("Farm", "north-west corner",
        tidy(grid.origin_easting, 1) + " E, " + tidy(grid.origin_northing, 1) + " N (NZTM2000)");
  } else {
    add("Farm", "extent", "one hectare, no grid");
  }
  add("Farm", "opening cover",
      with_unit(bundle.initial_state.grass_kg_dm_per_ha + bundle.initial_state.legume_kg_dm_per_ha,
                "kg DM/ha", 0));
  add("Farm", "opening soil water", with_unit(bundle.initial_state.soil_water_mm, "mm", 1));
  add("Farm", "opening mineral nitrogen",
      with_unit(bundle.initial_state.soil_mineral_nitrogen_kg_per_ha, "kg N/ha", 1));

  // ---------------------------------------------------------------- Ground
  switch (bundle.terrain.kind) {
    case TerrainSpec::Kind::Flat:
      add("Ground", "surface", "flat");
      break;
    case TerrainSpec::Kind::Synthetic:
      add("Ground", "surface", "synthetic (invented ground)");
      break;
    case TerrainSpec::Kind::Snapshot:
      add("Ground", "surface", "measured elevation");
      add("Ground", "elevation file", short_hash(bundle.terrain.elevation_sha256));
      break;
  }

  // ------------------------------------------------- Weather, soil, pasture
  //
  // By hash rather than by parameter. A sward definition is forty numbers and
  // listing them would bury the comparison; the hash says whether it is the
  // same file, which is the question, and `paddock scenario run` prints the
  // same twelve characters so the two can be matched up by eye.
  if (const BundleInput* weather = input_for(bundle, "weather"); weather != nullptr) {
    add("Weather", "series", short_hash(weather->recorded_sha256));
  } else {
    add("Weather", "series", "generated, not recorded");
  }
  if (const BundleInput* soil = input_for(bundle, "soil"); soil != nullptr) {
    add("Soil", "definition", short_hash(soil->recorded_sha256));
  }
  add("Soil", "available water", with_unit(bundle.soil.total_available_water_mm, "mm", 1));
  if (const BundleInput* sward = input_for(bundle, "sward"); sward != nullptr) {
    add("Pasture", "definition", short_hash(sward->recorded_sha256));
  }

  // ---------------------------------------------------------------- Stock
  if (bundle.mobs.empty()) {
    add("Stock", "mobs", "none, this is pasture alone");
  }
  for (const MobSpec& mob : bundle.mobs) {
    // Leader-follower is a management decision and not a detail: young stock
    // taking the leaf ahead of the ewes is a different farm from one mob
    // grazing behind another, so it is said where it differs.
    add("Stock", mob.name,
        std::to_string(mob.head) + " head at " + with_unit(mob.liveweight_kg, "kg", 1) + ", " +
            with_unit(mob.age_days / 365.25, "years old", 1) + ", starting in paddock " +
            std::to_string(mob.paddock + 1) + (mob.grazes_ahead ? ", grazes ahead" : ""));
  }
  for (const BundleInput& input : bundle.inputs) {
    if (input.section == "mob") {
      add("Stock", "species definition", short_hash(input.recorded_sha256));
    }
  }
  add("Stock", "feed quality",
      with_unit(bundle.diet.metabolisable_energy_mj_per_kg_dm, "MJ ME/kg DM", 1) + " at " +
          with_unit(bundle.diet.digestibility_percent, "% digestible", 0));

  // ---------------------------------------------------------- Grazing policy
  if (bundle.management.has_value()) {
    const core::ManagementPolicy& policy = *bundle.management;
    add("Grazing policy", "do not graze below",
        with_unit(policy.minimum_cover_kg_dm_per_ha, "kg DM/ha", 0));
    add("Grazing policy", "rotate above",
        with_unit(policy.rotation_cover_threshold_kg_dm_per_ha, "kg DM/ha", 0));
    add("Grazing policy", "graze for", with_unit(policy.maximum_graze_days, "days", 0));
    add("Grazing policy", "rest for", with_unit(policy.minimum_spell_days, "days", 0));
    add("Grazing policy", "target gain",
        with_unit(policy.target_liveweight_gain_kg_per_day, "kg/day", 2));
    add("Grazing policy", "buys feed", policy.may_buy_feed ? "yes" : "no");
    add("Grazing policy", "bought feed energy",
        with_unit(policy.supplement_me_mj_per_kg_dm, "MJ ME/kg DM", 1));
  } else {
    add("Grazing policy", "rules", "none in the bundle");
  }
  for (const core::GrazingPeriod& period : bundle.grazing.periods()) {
    // The rotation's own numbers, not just the calendar's dates. Smith and
    // Dawson shorten the spell to 21 days after weaning and lengthen it to 35
    // as summer dries, and a comparison that showed only "rotational" would
    // call those two the same management.
    std::string described = system_name(period.rule.system) + ", " +
                            period.dates.first.to_iso_string() + " to " +
                            period.dates.last.to_iso_string();
    if (period.rule.system == core::GrazingSystem::Rotational) {
      described += ", " + with_unit(period.rule.maximum_graze_days, "days on", 0) + " and " +
                   with_unit(period.rule.minimum_spell_days, "off", 0);
    }
    add("Grazing policy", period.name, described);
  }

  // ------------------------------------------------------- Money and rules
  //
  // **A different price book is a different scenario**, and leaving these out
  // would let the view report "nothing changed" for two runs whose flocks were
  // sold at different prices - which is worse than not comparing them at all,
  // because it is a false assurance rather than a gap.
  //
  // By hash, and by whether there is one. The economics file drives the flock's
  // decisions, so a run without it is not the same farm with the money panel
  // hidden - it is a farm whose stock never advance.
  if (const BundleInput* economics = input_for(bundle, "economics"); economics != nullptr) {
    add("Money and rules", "prices", short_hash(economics->recorded_sha256));
  } else {
    add("Money and rules", "prices", "none, this farm keeps no books");
  }
  if (const BundleInput* regulation = input_for(bundle, "regulation"); regulation != nullptr) {
    add("Money and rules", "regional rule", short_hash(regulation->recorded_sha256));
  } else {
    add("Money and rules", "regional rule", "none, so no compliance is claimed");
  }

  // ---------------------------------------------------------------- Irrigation
  //
  // **Off reads as one line, not as five settings nobody used.** A rain-fed
  // bundle still has a trigger and a target sitting at their defaults, and
  // listing those beside a bundle that irrigates would put three spurious
  // differences beside the real one. SetupPanel::describe already made this
  // choice and it is the right one.
  if (!bundle.irrigation.has_value() || !bundle.irrigation->enabled) {
    add("Irrigation", "irrigation", "off");
  } else {
    const core::IrrigationPolicy rule = bundle.irrigation.value_or(core::IrrigationPolicy{});
    add("Irrigation", "irrigation", "on");
    add("Irrigation", "trigger", as_available_water(rule.trigger_depletion_fraction));
    add("Irrigation", "refill target", as_available_water(rule.target_depletion_fraction));
    add("Irrigation", "most at once", with_unit(rule.maximum_application_mm, "mm", 1));
    if (rule.minimum_return_days > 0) {
      add("Irrigation", "wait between", with_unit(rule.minimum_return_days, "days", 0));
    }
    // No space before the sign, unlike every other unit here: "100 %" is a
    // typesetting mistake and "100 mm" is not.
    add("Irrigation", "reaches the ground",
        tidy(bundle.irrigation_system.application_efficiency * 100.0, 0) + "%");
  }

  return settings;
}

InputComparison compare_inputs(const ScenarioBundle& before, const ScenarioBundle& after) {
  InputComparison comparison;
  comparison.before_name = before.name;
  comparison.after_name = after.name;

  const std::vector<InputSetting> left = scenario_inputs(before);
  const std::vector<InputSetting> right = scenario_inputs(after);

  const auto value_in = [](const std::vector<InputSetting>& settings, const std::string& category,
                           const std::string& label) -> const std::string* {
    const auto found =
        std::find_if(settings.begin(), settings.end(), [&](const InputSetting& setting) {
          return setting.category == category && setting.label == label;
        });
    return found == settings.end() ? nullptr : &found->value;
  };

  // Category by category, in display order, so the result does not depend on
  // which bundle happened to be passed first or on how either was written.
  for (const std::string& category : categories()) {
    bool changed = false;

    // Labels from the left in their own order, then any the right has and the
    // left does not - **so a setting that exists on one side only is a
    // difference rather than a silence.** A mob added to one scenario is
    // exactly that case.
    std::vector<std::string> labels;
    for (const std::vector<InputSetting>* side : {&left, &right}) {
      for (const InputSetting& setting : *side) {
        if (setting.category == category &&
            std::find(labels.begin(), labels.end(), setting.label) == labels.end()) {
          labels.push_back(setting.label);
        }
      }
    }

    for (const std::string& label : labels) {
      const std::string* was = value_in(left, category, label);
      const std::string* now = value_in(right, category, label);
      const std::string before_text = was == nullptr ? std::string("-") : *was;
      const std::string after_text = now == nullptr ? std::string("-") : *now;
      if (before_text == after_text) {
        continue;
      }
      changed = true;
      comparison.changes.push_back(InputChange{category, label, before_text, after_text});
    }

    (changed ? comparison.changed_categories : comparison.unchanged_categories).push_back(category);
  }

  return comparison;
}

std::string what_changed(const InputComparison& comparison) {
  std::ostringstream out;
  out << "  What changed\n"
      << "  ============\n"
      << "    " << comparison.before_name << "  ->  " << comparison.after_name << "\n";

  if (comparison.changes.empty()) {
    // **Not an empty heading.** Two identical scenarios is a real answer and a
    // fairly alarming one - it usually means an edit went to the wrong file -
    // so it is said rather than left as a blank space under a title.
    out << "\n    Nothing. These two scenarios are configured identically, so any\n"
           "    difference in their results would be a fault in the model.\n";
    return out.str();
  }

  // The widest label, so the values line up in a column. Worked out rather than
  // guessed at, because a scenario can name a mob anything.
  std::size_t width = 0;
  for (const InputChange& change : comparison.changes) {
    width = std::max(width, change.label.size());
  }

  std::string category;
  for (const InputChange& change : comparison.changes) {
    if (change.category != category) {
      category = change.category;
      out << "\n    " << category << "\n";
    }

    // **One line where one line fits, two where it does not.** Most values are
    // short - "off  ->  on", "25 mm" - and putting those on one line is what
    // makes the block small enough to read at a glance. A few are not: a farm's
    // north-west corner is two NZTM coordinates, and a mob is a count, a weight
    // and an age. Wrapped at the terminal's edge those become unreadable, and
    // shortening them would mean dropping something a reader needs, so they
    // stack instead.
    const std::string padding(width - change.label.size(), ' ');
    const std::string label = "      " + change.label + padding + "   ";
    if (label.size() + change.before.size() + change.after.size() + 6 <= kLineWidth) {
      out << label << change.before << "  ->  " << change.after << "\n";
      continue;
    }
    out << "      " << change.label << "\n"
        << "        " << change.before << "\n"
        << "     -> " << change.after << "\n";
  }

  // **Whether this is a controlled comparison, said in the view rather than
  // left to the reader.** One category differing is what makes the difference
  // in the results attributable to anything; two is a confounded experiment,
  // and the moment to say so is before somebody reads the outputs, not after.
  out << "\n";
  if (comparison.is_controlled()) {
    out << "    One category differs, so what the results do is attributable to it.\n";
  } else {
    out << "    " << comparison.changed_categories.size()
        << " categories differ, not one. A comparison that changes more than one\n"
           "    thing cannot attribute its result to either.\n";
    out << listed("    Changed:", comparison.changed_categories, "             ");
  }

  if (!comparison.unchanged_categories.empty()) {
    // **Named, not counted.** "Six categories unchanged" tells a reader that
    // something was checked; naming them tells the reader that the weather was
    // checked, which is the assurance they actually wanted.
    out << listed("    Unchanged:", comparison.unchanged_categories, "               ");
  }
  return out.str();
}

}  // namespace paddock::config
