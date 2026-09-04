// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Gejile Hu. All rights reserved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <paddock/config/DemoSummary.hpp>

namespace paddock::config {

namespace {

/// Below this, a percentage is arithmetic rather than information: a farm that
/// went from 0.4 mm to 40 mm did not improve by ten thousand per cent, it
/// started from nothing worth measuring.
constexpr double kTooSmallToDivideBy = 1e-6;

std::string fixed(double value, int places) {
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(places);
  out << value;
  return out.str();
}

/// Decimals for a quantity, from what the quantity is.
///
/// **Significance belongs to the number, not to whoever prints it.** Six
/// thousand kilograms of dry matter to two decimal places claims a precision
/// this model does not have, and a water use efficiency to none loses the
/// difference the whole page is about.
int places_for(const std::string& unit) {
  if (unit == "kg DM/ha" || unit == "days" || unit == "mm") {
    return 0;
  }
  return 1;
}

/// The named indicator from a dashboard, or nothing.
const Indicator* indicator_named(const FarmDashboard& board, const std::string& name) {
  const std::vector<Indicator> all = board.all_indicators();
  const auto found = std::find_if(all.begin(), all.end(), [&name](const Indicator& indicator) {
    return indicator.name == name;
  });
  // The vector is a copy, so a pointer into it would dangle. Found by index
  // into the board's own panels instead.
  if (found == all.end()) {
    return nullptr;
  }
  for (const DashboardPanel& panel : board.panels) {
    for (const Indicator& indicator : panel.indicators) {
      if (indicator.name == name) {
        return &indicator;
      }
    }
  }
  return nullptr;
}

/// One row, built from the same indicator on both sides.
OutcomeRow row_from(const std::string& shown_as, const Indicator& before, const Indicator& after) {
  OutcomeRow row;
  row.name = shown_as;
  row.unit = after.unit;
  row.before = before.value;
  row.after = after.value;
  row.difference = after.value - before.value;
  if (std::abs(before.value) > kTooSmallToDivideBy) {
    row.percent = 100.0 * row.difference / std::abs(before.value);
  }
  // From the indicator that was already built, never decided here.
  row.confidence = confidence_of(after);
  row.note = after.note;
  return row;
}

/// A sentence wrapped to fit a terminal, every line after the first indented.
///
/// The reasons a metric is off this page are the most important prose here and
/// the longest, so they are the one thing that must not run off the edge.
std::string wrapped(const std::string& text, const std::string& indent, std::size_t width) {
  std::istringstream words(text);
  std::ostringstream out;
  std::string word;
  std::size_t column = indent.size();
  bool first = true;
  out << indent;
  while (words >> word) {
    if (!first && column + 1 + word.size() > width) {
      out << "\n" << indent;
      column = indent.size();
      first = true;
    }
    if (!first) {
      out << ' ';
      ++column;
    }
    out << word;
    column += word.size();
    first = false;
  }
  out << "\n";
  return out.str();
}

/// A line of the table, padded into columns.
std::string padded(const std::string& text, std::size_t width, bool right) {
  if (text.size() >= width) {
    return text;
  }
  const std::string gap(width - text.size(), ' ');
  return right ? gap + text : text + gap;
}

}  // namespace

std::string to_string(Confidence level) {
  switch (level) {
    case Confidence::Benchmarked:
      return "benchmarked";
    case Confidence::Calibrated:
      return "calibrated";
    case Confidence::Conserved:
      return "conserved";
    case Confidence::Exploratory:
      return "exploratory";
    case Confidence::DoNotQuote:
      break;
  }
  return "do not quote";
}

std::string confidence_meaning(Confidence level) {
  switch (level) {
    case Confidence::Benchmarked:
      return "measured against a published figure, and independent of it";
    case Confidence::Calibrated:
      return "reproduces published observations, having been fitted to them";
    case Confidence::Conserved:
      // **"Computed from", not "is".** Days held back is a count, not a ledger
      // line, and saying it balances to 1e-9 would claim more than is true; it
      // is read off a water balance that does.
      return "no external benchmark; read off budgets the tests close to 1e-9";
    case Confidence::Exploratory:
      return "the source has not been confirmed - interesting, not quotable";
    case Confidence::DoNotQuote:
      break;
  }
  return "no evidence behind it; the model runs on it and nothing may be published from it";
}

Confidence confidence_of(const Indicator& indicator) {
  switch (indicator.trust) {
    case Provenance::Placeholder:
      return Confidence::DoNotQuote;
    case Provenance::Verify:
      return Confidence::Exploratory;
    case Provenance::Fitted:
      // **Fitted never reaches Benchmarked, band or no band.** A value
      // calibrated to a trial and then compared against that same trial is
      // checking its own arithmetic, not being tested against the world.
      return Confidence::Calibrated;
    case Provenance::Direct:
    case Provenance::Derived:
      break;
  }
  return indicator.low.has_value() || indicator.high.has_value() ? Confidence::Benchmarked
                                                                 : Confidence::Conserved;
}

std::vector<Confidence> DemoSummary::levels_used() const {
  std::vector<Confidence> levels;
  for (const Confidence level :
       {Confidence::Benchmarked, Confidence::Calibrated, Confidence::Conserved,
        Confidence::Exploratory, Confidence::DoNotQuote}) {
    const bool used = std::any_of(outcomes.begin(), outcomes.end(), [level](const OutcomeRow& row) {
      return row.confidence == level;
    });
    if (used) {
      levels.push_back(level);
    }
  }
  return levels;
}

DemoSummary demo_summary(const FarmDashboard& before, const FarmDashboard& after,
                         const RunSummary& before_run, const RunSummary& after_run) {
  DemoSummary summary;
  summary.before_name = before.label;
  summary.after_name = after.label;

  // **Six, chosen against one question: does this farm want irrigation.**
  // Everything here is grass or water, because those are the two things this
  // model has been checked on. The order runs from what the farm produced, to
  // how hard the summer was, to what the water cost and where it went.
  const std::vector<std::pair<std::string, std::string>> wanted = {
      {"Pasture grown", "Pasture grown"},
      {"Lowest cover", "Lowest pasture cover"},
      {"Days water-stressed", "Growth-limited days"},
      {"Water use efficiency", "Dry matter per mm"},
      {"Drainage", "Drainage below roots"},
  };

  for (const auto& [indicator_name, shown_as] : wanted) {
    const Indicator* was = indicator_named(before, indicator_name);
    const Indicator* now = indicator_named(after, indicator_name);
    if (was == nullptr || now == nullptr) {
      // **Left out, not shown as nought.** A run that reported nothing and a
      // run that reported zero are different facts, and a dash in a demo is
      // read as a zero every time.
      summary.omitted.push_back(
          {shown_as, "one of the two runs did not report it, so there is nothing to compare"});
      continue;
    }
    summary.outcomes.push_back(row_from(shown_as, *was, *now));
  }

  // **Irrigation applied is an input, not an outcome, and it belongs here
  // anyway**: it is what the farmer buys, and a page that showed what the water
  // produced without showing how much water it took would be selling something.
  //
  // It has no dashboard tile because a dashboard reports a farm rather than a
  // decision, so it is taken from the run's own tally - the same figure the
  // conservation suite balances the water budget against.
  OutcomeRow applied;
  applied.name = "Irrigation applied";
  applied.unit = "mm";
  applied.before = before_run.irrigation.effective_mm;
  applied.after = after_run.irrigation.effective_mm;
  applied.difference = applied.after - applied.before;
  if (std::abs(applied.before) > kTooSmallToDivideBy) {
    applied.percent = 100.0 * applied.difference / std::abs(applied.before);
  }
  applied.confidence = Confidence::Conserved;
  applied.note =
      "What the schedule put on, as a mean over the farm. A line in the water budget "
      "the conservation suite closes to 1e-9, and not a figure any trial has checked "
      "this model's scheduling against.";
  summary.outcomes.push_back(std::move(applied));

  // ------------------------------------------------------- what is not here
  //
  // Each of these is a number the model produces and this page will not put in
  // front of a customer. The reasons are the verification tracker's, not new
  // ones invented for the occasion.
  summary.omitted.push_back(
      {"Bought feed and supplement",
       "the model's purchases are unbounded - it buys whatever the deficit asks for - so the "
       "figure moves with the deficit rather than with what a farm would actually do (E71, E77)"});
  summary.omitted.push_back(
      {"Liveweight, stocking rate and lamb weights",
       "animal production is the least validated part of this model and must not headline a "
       "comparison"});
  summary.omitted.push_back(
      {"Money",
       "the water is free in the model - no consent, no pumping cost, no capital - so a closing "
       "balance is not a return on the irrigation"});
  summary.omitted.push_back(
      {"Nitrate leached",
       "the direction is real and the coefficients are placeholders, so it is a trade-off to "
       "discuss and not a number to file. `paddock dashboard` reports it with that status"});

  return summary;
}

std::string as_text(const DemoSummary& summary) {
  std::ostringstream out;
  out << "  What changed in the paddock\n"
      << "  ===========================\n"
      << "    " << summary.before_name << "  ->  " << summary.after_name << "\n\n";

  // Columns sized to their contents, so a long scenario name or a five-figure
  // value cannot run two columns together.
  std::size_t name_width = 0;
  std::size_t value_width = 0;
  std::size_t unit_width = 0;
  for (const OutcomeRow& row : summary.outcomes) {
    const int places = places_for(row.unit);
    name_width = std::max(name_width, row.name.size());
    value_width = std::max({value_width, fixed(row.before, places).size(),
                            fixed(row.after, places).size(), fixed(row.difference, places).size()});
    unit_width = std::max(unit_width, row.unit.size());
  }

  for (const OutcomeRow& row : summary.outcomes) {
    const int places = places_for(row.unit);
    const std::string sign = row.difference >= 0.0 ? "+" : "";

    out << "    " << padded(row.name, name_width, false) << " "
        << padded(fixed(row.before, places), value_width, true) << " -> "
        << padded(fixed(row.after, places), value_width, true) << " "
        << padded(row.unit, unit_width, false) << "  "
        << padded(sign + fixed(row.difference, places), value_width + 1, true) << "  ";

    if (row.percent.has_value()) {
      const std::string percent_sign = *row.percent >= 0.0 ? "+" : "";
      out << padded(percent_sign + fixed(*row.percent, 0) + "%", 6, true);
    } else {
      // **Not a blank and not a zero.** "from 0" says why there is no
      // percentage, which a reader would otherwise take for a missing figure.
      out << padded("from 0", 6, true);
    }
    out << "  " << to_string(row.confidence) << "\n";
  }

  // ---------------------------------------------------------- the key
  out << "\n  What these are worth\n";
  for (const Confidence level : summary.levels_used()) {
    // The label, then its meaning wrapped under a hanging indent - so a
    // wording longer than this line cannot be what pushes the page off a
    // terminal.
    const std::string label = "    " + padded(to_string(level), 12, false) + " ";
    const std::string meaning = wrapped(confidence_meaning(level), "", 80 - label.size());
    std::istringstream folded(meaning);
    std::string part;
    bool leading = true;
    while (std::getline(folded, part)) {
      out << (leading ? label : std::string(label.size(), ' ')) << part << "\n";
      leading = false;
    }
  }

  if (!summary.omitted.empty()) {
    out << "\n  Not on this page\n";
    for (const OmittedOutcome& left_out : summary.omitted) {
      out << "    " << left_out.name << "\n" << wrapped(left_out.reason, "      ", 80);
    }
  }
  return out.str();
}

}  // namespace paddock::config
