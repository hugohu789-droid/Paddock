// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <paddock/config/ScenarioComparison.hpp>
#include <paddock/core/BudgetLedger.hpp>

namespace paddock::config {

namespace {

/// What one named process put into or took out of a budget over the run.
///
/// Read from the ledger rather than added up again here. The ledger is what the
/// conservation suite balances to 1e-9, so a table built from it cannot
/// disagree with the model it reports; a second summation could.
double process_flow(const core::BudgetLedger& ledger, core::Budget budget,
                    const std::string& process, bool inflow) {
  for (const core::ProcessEntry& entry : ledger.entries(budget)) {
    if (entry.process == process) {
      return inflow ? entry.inflow : entry.outflow;
    }
  }
  // Absent, not zero - but zero is the honest reading here: a process that
  // never ran moved nothing. A process that is missing because nothing models
  // it belongs in the table's caveats instead.
  return 0.0;
}

double rainfall_mm(const RunSummary& summary) {
  return process_flow(summary.ledger, core::Budget::Water, "rainfall", true);
}

/// Adds one row across every scenario, given how to read it from a run.
template <typename Fn>
void add_row(ComparisonTable& table, std::string group, std::string name, std::string unit,
             int decimals, const std::vector<ComparedScenario>& scenarios, Fn&& read) {
  MetricRow row;
  row.group = std::move(group);
  row.name = std::move(name);
  row.unit = std::move(unit);
  row.decimals = decimals;
  row.values.reserve(scenarios.size());
  for (const ComparedScenario& scenario : scenarios) {
    row.values.push_back(read(scenario));
  }
  table.metrics.push_back(std::move(row));
}

std::string number(double value, int decimals) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(decimals);
  out << value;
  return out.str();
}

/// A CSV field, quoted when it has to be.
std::string csv_field(const std::string& text) {
  if (text.find_first_of(",\"\n") == std::string::npos) {
    return text;
  }
  std::string quoted = "\"";
  for (const char character : text) {
    if (character == '"') {
      quoted += '"';
    }
    quoted += character;
  }
  quoted += '"';
  return quoted;
}

/// The change from the first scenario, as a sentence fragment, or empty when
/// there is nothing to say.
std::string difference(const MetricRow& row, std::size_t against) {
  if (row.values.size() <= against || row.values.empty()) {
    return {};
  }
  const double base = row.values.front();
  const double value = row.values[against];
  const double change = value - base;
  if (std::abs(change) < std::pow(10.0, -row.decimals) / 2.0) {
    return {};
  }

  std::string text = (change > 0.0 ? "+" : "") + number(change, row.decimals);
  if (!row.unit.empty()) {
    text += " " + row.unit;
  }
  // A percentage only where there is something to take a percentage of. Against
  // a base of zero every change is infinite, which is arithmetic rather than
  // information.
  if (std::abs(base) > 1e-9) {
    text += " (" + (change > 0.0 ? std::string("+") : std::string()) +
            number(100.0 * change / base, 0) + "%)";
  }
  return text;
}

}  // namespace

ComparisonTable compare(const std::vector<ComparedScenario>& scenarios) {
  ComparisonTable table;
  if (scenarios.empty()) {
    return table;
  }

  for (const ComparedScenario& scenario : scenarios) {
    table.scenarios.push_back(scenario.name);
  }

  // ------------------------------------------------------ what was different
  //
  // Every setting any scenario named, kept only where the scenarios do not
  // agree. A setting missing from one of them counts as a difference: it means
  // that scenario was configured another way, and hiding that would be worse
  // than an awkward blank.
  std::vector<std::string> seen;
  for (const ComparedScenario& scenario : scenarios) {
    for (const auto& [name, value] : scenario.settings) {
      if (std::find(seen.begin(), seen.end(), name) == seen.end()) {
        seen.push_back(name);
      }
    }
  }
  for (const std::string& name : seen) {
    SettingRow row;
    row.name = name;
    for (const ComparedScenario& scenario : scenarios) {
      const auto found = std::find_if(
          scenario.settings.begin(), scenario.settings.end(),
          [&name](const std::pair<std::string, std::string>& pair) { return pair.first == name; });
      row.values.push_back(found == scenario.settings.end() ? std::string("-") : found->second);
    }
    const bool all_same =
        std::all_of(row.values.begin(), row.values.end(),
                    [&row](const std::string& value) { return value == row.values.front(); });
    if (!all_same) {
      table.differences.push_back(std::move(row));
    }
  }

  // ------------------------------------------------ pasture, and its use
  add_row(table, "Pasture", "Pasture grown", "kg DM/ha", 0, scenarios,
          [](const ComparedScenario& scenario) {
            return process_flow(scenario.summary.ledger, core::Budget::DryMatter, "pasture_growth",
                                true);
          });
  add_row(
      table, "Pasture", "Mean cover", "kg DM/ha", 0, scenarios,
      [](const ComparedScenario& scenario) { return scenario.summary.mean_cover_kg_dm_per_ha(); });
  add_row(table, "Pasture", "Lowest cover", "kg DM/ha", 0, scenarios,
          [](const ComparedScenario& scenario) {
            return scenario.summary.lowest_cover_kg_dm_per_ha();
          });
  add_row(table, "Pasture", "Closing cover", "kg DM/ha", 0, scenarios,
          [](const ComparedScenario& scenario) { return scenario.summary.closing_cover_kg_dm; });
  // **Divided by the area, because the row above it is.**
  //
  // What the pasture grew comes from the ledger, which works per hectare - the
  // grid averages its cells into it. What the stock ate comes from the mob's
  // energy requirement, which is the whole mob over the whole farm. Two rows in
  // one table on two different bases is worse than either: the first thing
  // anybody does with them is subtract one from the other, and on an eighty
  // hectare farm that is wrong by eighty times.
  add_row(table, "Pasture", "Eaten", "kg DM/ha", 0, scenarios,
          [](const ComparedScenario& scenario) {
            return scenario.hectares > 0.0 ? scenario.summary.eaten_kg_dm / scenario.hectares
                                           : scenario.summary.eaten_kg_dm;
          });
  add_row(table, "Pasture", "Days growth held back by dry soil", "days", 0, scenarios,
          [](const ComparedScenario& scenario) {
            return static_cast<double>(scenario.summary.days_water_stressed());
          });

  // ------------------------------------------------------------------ water
  add_row(table, "Water", "Rainfall", "mm", 0, scenarios,
          [](const ComparedScenario& scenario) { return rainfall_mm(scenario.summary); });
  add_row(table, "Water", "Irrigations", "", 0, scenarios, [](const ComparedScenario& scenario) {
    return static_cast<double>(scenario.summary.irrigation.events);
  });
  add_row(
      table, "Water", "Irrigation applied", "mm", 0, scenarios,
      [](const ComparedScenario& scenario) { return scenario.summary.irrigation.effective_mm; });
  add_row(table, "Water", "Water pumped", "ML", 1, scenarios, [](const ComparedScenario& scenario) {
    return scenario.summary.irrigation.pumped_megalitres(scenario.hectares);
  });
  add_row(table, "Water", "Drainage", "mm", 0, scenarios, [](const ComparedScenario& scenario) {
    return process_flow(scenario.summary.ledger, core::Budget::Water, "drainage", false);
  });
  add_row(table, "Water", "Evapotranspiration", "mm", 0, scenarios,
          [](const ComparedScenario& scenario) {
            return process_flow(scenario.summary.ledger, core::Budget::Water, "evapotranspiration",
                                false);
          });

  // -------------------------------------------------------- stock, and feed
  add_row(
      table, "Stock", "Closing liveweight", "kg/head", 1, scenarios,
      [](const ComparedScenario& scenario) { return scenario.summary.closing_liveweight_kg(); });
  add_row(table, "Stock", "Liveweight change", "kg/head", 1, scenarios,
          [](const ComparedScenario& scenario) { return scenario.summary.liveweight_change_kg(); });
  add_row(table, "Stock", "Days feed was short", "days", 0, scenarios,
          [](const ComparedScenario& scenario) {
            return static_cast<double>(scenario.summary.days_short);
          });
  add_row(table, "Stock", "Bought feed", "kg DM/ha", 0, scenarios,
          [](const ComparedScenario& scenario) {
            // Bought feed is a mob quantity too - see the note on Eaten.
            return scenario.hectares > 0.0
                       ? scenario.summary.bought_feed_kg_dm() / scenario.hectares
                       : scenario.summary.bought_feed_kg_dm();
          });

  // -------------------------------------------------------------- nitrogen
  add_row(table, "Nitrogen", "Fixed by legume", "kg N/ha", 1, scenarios,
          [](const ComparedScenario& scenario) {
            return process_flow(scenario.summary.ledger, core::Budget::Nitrogen, "legume_fixation",
                                true);
          });
  add_row(table, "Nitrogen", "Removed by grazing", "kg N/ha", 1, scenarios,
          [](const ComparedScenario& scenario) {
            return process_flow(scenario.summary.ledger, core::Budget::Nitrogen, "grazing_offtake",
                                false);
          });
  add_row(table, "Nitrogen", "Closing soil nitrogen", "kg N/ha", 1, scenarios,
          [](const ComparedScenario& scenario) { return scenario.summary.closing_nitrogen_kg; });

  // **What this table cannot tell you.**
  //
  // Said here rather than left to a reader to notice. A comparison that is
  // silent about nitrogen loss beside three nitrogen rows reads as a farm that
  // loses none, and that is not a result - it is a process nothing in this model
  // computes.
  table.caveats.emplace_back(
      "Nitrogen leaching is not modelled. The nitrogen rows account for what the clover fixed, "
      "what the stock removed and what the soil holds; none of them is a loss to water.");
  table.caveats.emplace_back(
      "Every scenario runs on the same recorded weather. A difference here is a difference "
      "between the rules, not between the seasons they met.");
  table.caveats.emplace_back(
      "The sward is not yet calibrated against measured New Zealand yields, so read the "
      "differences between these scenarios rather than the absolute figures.");

  return table;
}

std::string as_markdown(const ComparisonTable& table) {
  if (table.scenarios.empty()) {
    return {};
  }
  std::ostringstream out;

  out << "|  |";
  for (const std::string& name : table.scenarios) {
    out << ' ' << name << " |";
  }
  out << "\n| --- |";
  for (std::size_t i = 0; i < table.scenarios.size(); ++i) {
    out << " ---: |";
  }
  out << '\n';

  if (!table.differences.empty()) {
    out << "| **What differs** |";
    for (std::size_t i = 0; i < table.scenarios.size(); ++i) {
      out << " |";
    }
    out << '\n';
    for (const SettingRow& row : table.differences) {
      out << "| " << row.name << " |";
      for (const std::string& value : row.values) {
        out << ' ' << value << " |";
      }
      out << '\n';
    }
  }

  std::string group;
  for (const MetricRow& row : table.metrics) {
    if (row.group != group) {
      group = row.group;
      out << "| **" << group << "** |";
      for (std::size_t i = 0; i < table.scenarios.size(); ++i) {
        out << " |";
      }
      out << '\n';
    }
    out << "| " << row.name;
    if (!row.unit.empty()) {
      out << " (" << row.unit << ')';
    }
    out << " |";
    for (const double value : row.values) {
      out << ' ' << number(value, row.decimals) << " |";
    }
    out << '\n';
  }

  for (const std::string& caveat : table.caveats) {
    out << "\n> " << caveat << '\n';
  }
  return out.str();
}

std::string as_csv(const ComparisonTable& table) {
  if (table.scenarios.empty()) {
    return {};
  }
  std::ostringstream out;
  out << "group,metric,unit";
  for (const std::string& name : table.scenarios) {
    out << ',' << csv_field(name);
  }
  out << '\n';

  for (const SettingRow& row : table.differences) {
    out << "setting," << csv_field(row.name) << ',';
    for (const std::string& value : row.values) {
      out << ',' << csv_field(value);
    }
    out << '\n';
  }
  for (const MetricRow& row : table.metrics) {
    out << csv_field(row.group) << ',' << csv_field(row.name) << ',' << csv_field(row.unit);
    for (const double value : row.values) {
      out << ',' << number(value, row.decimals);
    }
    out << '\n';
  }
  return out.str();
}

std::string summarise(const ComparisonTable& table) {
  if (table.scenarios.size() < 2) {
    return "A comparison needs two scenarios before there is anything to say.";
  }

  std::ostringstream out;
  const std::string& base = table.scenarios.front();

  // The rows worth a sentence: what was grown, what it cost in water, what the
  // stock did, and how long the farm was dry. Not every row - a paragraph that
  // repeats the whole table is a table nobody reads twice.
  const std::vector<std::string> spoken{"Pasture grown", "Water pumped", "Liveweight change",
                                        "Days growth held back by dry soil", "Bought feed"};

  for (std::size_t i = 1; i < table.scenarios.size(); ++i) {
    out << table.scenarios[i] << " against " << base << ": ";
    std::vector<std::string> clauses;
    for (const std::string& name : spoken) {
      const auto found = std::find_if(table.metrics.begin(), table.metrics.end(),
                                      [&name](const MetricRow& row) { return row.name == name; });
      if (found == table.metrics.end()) {
        continue;
      }
      const std::string change = difference(*found, i);
      if (change.empty()) {
        continue;
      }
      std::string clause = found->name;
      clause[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(clause[0])));
      clause += " ";
      clause += change;
      clauses.push_back(std::move(clause));
    }
    if (clauses.empty()) {
      out << "no measured difference.";
    } else {
      for (std::size_t c = 0; c < clauses.size(); ++c) {
        out << clauses[c];
        if (c + 2 < clauses.size()) {
          out << ", ";
        } else if (c + 2 == clauses.size()) {
          out << " and ";
        }
      }
      out << '.';
    }
    out << '\n';
  }

  // **No recommendation, and the reason is worth stating rather than leaving as
  // an absence.** Choosing between more grass and less water needs a price for
  // each, and this project has a source for neither.
  out << "\nWhich of these is worth doing depends on what water costs and what the extra feed is "
         "worth, and neither price is in this model.";
  return out.str();
}

}  // namespace paddock::config
